#include "addons/lsm6dsr_imu.h"
#include "config.pb.h"
#include "enums.pb.h"
#include "storagemanager.h"
#include "peripheralmanager.h"
#include "gamepad.h"
#include "gamepad/GamepadState.h"
#include "pico/time.h"
#include <cmath>
#include <cstring>

// Register map (ST LSM6DSR, same for I2C/SPI)
#define LSM6DSR_CTRL1_XL      0x10U
#define LSM6DSR_CTRL2_G       0x11U
#define LSM6DSR_CTRL3_C       0x12U
#define LSM6DSR_CTRL4_C       0x13U
#define LSM6DSR_CTRL6_C       0x15U
#define LSM6DSR_CTRL7_G       0x16U  // bit7 G_HM_MODE=0 → Gyro 高性能
#define LSM6DSR_CTRL8_XL      0x17U  // LPF2 关闭 → 最小延迟
#define LSM6DSR_CTRL9_XL      0x18U
#define LSM6DSR_OUTX_L_G      0x22U

#define LSM6DSR_SPI_READ      0x80U

// 陀螺仪零偏校准：静止采样数量与间隔（与 alpakka 思路一致，采样平均作为零偏），总时长约 5 秒
#define LSM6DSR_GYRO_CAL_SAMPLES  2500
#define LSM6DSR_GYRO_CAL_DELAY_MS 2
// 加速度计零漂校准（alpakka 方法）：采样数量与 alpakka CFG_CALIBRATION_SAMPLES_ACCEL 同量级，无延迟以加快完成
#define LSM6DSR_ACCEL_CAL_SAMPLES  10000
// LSM6DSR 4g 量程下 1G 对应 LSB（与 alpakka BIT_14 在 2G 下等价：校准后 Z 减 1G）
#define LSM6DSR_ACCEL_1G_LSB_INT  8192

// DS4 陀螺仪单位换算：主机用 gyroResPerDeg (1000/61) 解析，即 report * (61/1000) = deg/s → 1 LSB = 0.061 deg/s
// LSM6DSR 量程为 ±500 dps（CTRL2_G 配置），此量程下 17.5 mdps/LSB → 1 LSB = 0.0175 deg/s，故 DS4_report = raw * 0.0175 / 0.061 = raw * 175/610
#define LSM6DSR_GYRO_500DPS_NUMER   175   // 17.5 * 10，用于 report = calG * 175 / 610
#define LSM6DSR_GYRO_500DPS_DENOM   610   // 61 * 10，与 DS4 0.061 deg/s/LSB 一致

// 陀螺仪模拟方式（与 HML 设置-体感设置-陀螺仪设置 下拉框一致）
#define LSM6DSR_OUTPUT_DS4          0
#define LSM6DSR_OUTPUT_LEFT_STICK   1
#define LSM6DSR_OUTPUT_RIGHT_STICK  2
#define LSM6DSR_OUTPUT_MOUSE        3


// 生效方式：0=一直生效, 1=按下按键生效, 2=按下按键暂停
#define LSM6DSR_ENGAGE_ALWAYS       0
#define LSM6DSR_ENGAGE_ON_KEY       1
#define LSM6DSR_ENGAGE_PAUSE_ON_KEY  2

// 一欧元滤波：低通 alpha = 1/(1+tau/Te)，tau=1/(2*pi*fc)。Te 使用运行时 dt（可变回报率）
#define LSM6DSR_ONE_EURO_TE_FALLBACK_S  (1e-3f)
#define LSM6DSR_ONE_EURO_TE_MIN_S       (2e-4f)
#define LSM6DSR_ONE_EURO_TE_MAX_S       (2e-2f)
#define LSM6DSR_ONE_EURO_FC_HZ      5.0f
// tau = 1/(2*pi*fc)，与 fc 绑定；alpha 仍按运行时 Te 计算
static constexpr float kOneEuroTau = 1.0f / (2.0f * 3.14159265f * LSM6DSR_ONE_EURO_FC_HZ);

// 点击抖动抑制窗口：检测到按键按下沿后，短时间屏蔽陀螺鼠标输出，抑制按键带来的手柄下压位移。
#define LSM6DSR_MOUSE_CLICK_SUPPRESS_US 15000u

// 按需读取用（网页模式下 preprocess 不运行，API 调用时现场读一次）
static PeripheralSPI* s_spi = nullptr;
static int8_t s_csPin = -1;

static inline int16_t read16LE(const uint8_t* p) {
	return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline float lsm6dsr_compute_dt_s(uint64_t nowUs, uint64_t& lastUs) {
	float dtS = LSM6DSR_ONE_EURO_TE_FALLBACK_S;
	if (lastUs != 0 && nowUs > lastUs) {
		dtS = (float)(nowUs - lastUs) * 1e-6f;
		if (dtS < LSM6DSR_ONE_EURO_TE_MIN_S || dtS > LSM6DSR_ONE_EURO_TE_MAX_S) {
			dtS = LSM6DSR_ONE_EURO_TE_FALLBACK_S;
		}
	}
	lastUs = nowUs;
	return dtS;
}

static void spiReadRegs(PeripheralSPI* spi, int8_t csPin, uint8_t reg, uint8_t* buf, size_t len) {
	// 与逐字节 transfer(0) 等价：首字节 RX 丢弃，后续 len 字节为寄存器数据（IF_INC）
	if (len == 0 || len > 12) return;
	spi->setMode(SPI_MODE3);
	spi->select(csPin);
	uint8_t tx[13];
	uint8_t rx[13];
	tx[0] = (uint8_t)(reg | LSM6DSR_SPI_READ);
	memset(tx + 1, 0, len);
	spi->transfer(tx, rx, 1 + len);
	memcpy(buf, rx + 1, len);
	spi->deselect();
}

static void spiWriteReg(PeripheralSPI* spi, int8_t csPin, uint8_t reg, uint8_t val) {
	spi->setMode(SPI_MODE3);
	spi->select(csPin);
	spi->transfer(reg);
	spi->transfer(val);
	spi->deselect();
}

bool LSM6DSRIMUAddon::available() {
	const InputMode inputMode = Storage::getInstance().getGamepadOptions().inputMode;
	if (inputMode != INPUT_MODE_PS4 && inputMode != INPUT_MODE_PS4B && inputMode != INPUT_MODE_SWITCH_PRO) {
		return false;
	}
	const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
	if (!opts.enabled) return false;
	PeripheralSPI* spi = PeripheralManager::getInstance().getSPI(LSM6DSR_HW_SPI_BLOCK);
	if (!PeripheralManager::getInstance().isSPIEnabled(LSM6DSR_HW_SPI_BLOCK) || !spi || !spi->configured)
		return false;
	return true;
}

void LSM6DSRIMUAddon::setup() {
	s_spi = nullptr;
	s_csPin = -1;

	const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
	if (!opts.enabled) {
		spi = nullptr;
		csPin = -1;
		return;
	}
	csPin = LSM6DSR_HW_CS_PIN;
	spi = PeripheralManager::getInstance().getSPI(LSM6DSR_HW_SPI_BLOCK);
	if (!spi || !spi->configured) {
		spi = nullptr;
		csPin = -1;
		return;
	}
	offsetGyroX = opts.offsetGyroX;
	offsetGyroY = opts.offsetGyroY;
	offsetGyroZ = opts.offsetGyroZ;
	offsetAccelX = opts.offsetAccelX;
	offsetAccelY = opts.offsetAccelY;
	offsetAccelZ = opts.offsetAccelZ;
	outputMode = opts.outputMode;
	engageMode = opts.engageMode;
	oneEuroFilterEnabled = opts.gyroOneEuroFilterEnabled;
	gyroMouseMapMode = opts.gyroMouseMapMode;
	gyroMouseInvert = opts.gyroMouseInvert;
	gyroMouseSensLR = (opts.gyroMouseSensLR > 0.0f) ? opts.gyroMouseSensLR : 1.0f;
	gyroMouseSensUD = (opts.gyroMouseSensUD > 0.0f) ? opts.gyroMouseSensUD : 1.0f;
	gyroMouseDeadzone = opts.gyroMouseDeadzone;
	engageKeysCount = opts.gyroEngageKeys_count <= 16 ? opts.gyroEngageKeys_count : 16;
	for (size_t i = 0; i < engageKeysCount; i++)
		engageKeys[i] = opts.gyroEngageKeys[i];
	buildEngageMasks();
	for (int i = 0; i < 3; i++) {
		oneEuroState[i] = 0.0f;
	}
	oneEuroInited = false;
	lastSampleUs = 0;
	mouseSubX = 0.0f;
	mouseSubY = 0.0f;
	prevButtons = 0;
	mouseSuppressUntilUs = 0;
	mouseOutputCleared = false;
	gyroOutputCleared = false;
	// 初始化 IMU 历史数据缓冲区
	imuHistoryIndex = 0;
	memset(imuHistory, 0, sizeof(imuHistory));

	// LSM6DSR requires MODE3 on shared SPI.
	spi->setBaudrate(LSM6DSR_SPI_HZ);
	spi->setMode(SPI_MODE3);

	// Disable FIFO (避免延迟), I3C, High Performance, ODR 1666 Hz, 4g acc, 500 dps gyro, BDU+IF_INC
	// CTRL2_G: 0x84 = ODR 1.66kHz (0b10) + FS 500 dps (0b01) → 17.5 mdps/LSB
	spiWriteReg(spi, csPin, LSM6DSR_CTRL9_XL, 0x02);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL4_C, 0x06);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL6_C, 0x00);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL7_G, 0x00);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL8_XL, 0x00);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL3_C, 0x44);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL1_XL, 0x88);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL2_G, 0x84);  // 500 dps

	// 原生陀螺仪路由在 preprocess 中按 Gamepad::options.inputMode 每帧判定（支持热切换）

	s_spi = spi;
	s_csPin = csPin;
}

void LSM6DSRIMUAddon::reinit() {
	// 陀螺仪/加速度计校准数据针对设备全局，不随 profile 切换；重新加载模拟方式、生效方式与生效按键
	const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
	offsetGyroX = opts.offsetGyroX;
	offsetGyroY = opts.offsetGyroY;
	offsetGyroZ = opts.offsetGyroZ;
	offsetAccelX = opts.offsetAccelX;
	offsetAccelY = opts.offsetAccelY;
	offsetAccelZ = opts.offsetAccelZ;
	outputMode = opts.outputMode;
	engageMode = opts.engageMode;
	oneEuroFilterEnabled = opts.gyroOneEuroFilterEnabled;
	gyroMouseMapMode = opts.gyroMouseMapMode;
	gyroMouseInvert = opts.gyroMouseInvert;
	gyroMouseSensLR = (opts.gyroMouseSensLR > 0.0f) ? opts.gyroMouseSensLR : 1.0f;
	gyroMouseSensUD = (opts.gyroMouseSensUD > 0.0f) ? opts.gyroMouseSensUD : 1.0f;
	gyroMouseDeadzone = opts.gyroMouseDeadzone;
	engageKeysCount = opts.gyroEngageKeys_count <= 16 ? opts.gyroEngageKeys_count : 16;
	for (size_t i = 0; i < engageKeysCount; i++)
		engageKeys[i] = opts.gyroEngageKeys[i];
	buildEngageMasks();
	for (int i = 0; i < 3; i++) {
		oneEuroState[i] = 0.0f;
	}
	oneEuroInited = false;
	lastSampleUs = 0;
	mouseSubX = 0.0f;
	mouseSubY = 0.0f;
	prevButtons = 0;
	mouseSuppressUntilUs = 0;
	mouseOutputCleared = false;
	gyroOutputCleared = false;
	// 重置 IMU 历史数据缓冲区，避免切换模式时使用过期数据
	imuHistoryIndex = 0;
	memset(imuHistory, 0, sizeof(imuHistory));
}

void LSM6DSRIMUAddon::applyOneEuroFilter(float teS) {
	const float alpha = 1.0f / (1.0f + kOneEuroTau / teS);
	for (int i = 0; i < 3; i++) {
		float in = (float)calG[i];
		if (!oneEuroInited) {
			oneEuroState[i] = in;
		} else {
			oneEuroState[i] = alpha * in + (1.0f - alpha) * oneEuroState[i];
		}
		int32_t out = (int32_t)(oneEuroState[i] + (oneEuroState[i] >= 0.0f ? 0.5f : -0.5f));
		if (out > 32767) out = 32767;
		else if (out < -32768) out = -32768;
		calG[i] = (int16_t)out;
	}
	oneEuroInited = true;
}

// 供网页「查看陀螺仪」等使用：raw - offset 后，根据配置应用一欧元滤波再返回，与 preprocess 输出一致
static float s_apiOneEuroState[3] = {0.0f, 0.0f, 0.0f};
static bool s_apiOneEuroInited = false;
static uint64_t s_apiLastSampleUs = 0;

bool getLSM6DSRRawData(int16_t gyro[3], int16_t accel[3]) {
	if (!s_spi || s_csPin < 0) return false;
	const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
	int32_t offX = opts.offsetGyroX;
	int32_t offY = opts.offsetGyroY;
	int32_t offZ = opts.offsetGyroZ;
	int32_t offAX = opts.offsetAccelX;
	int32_t offAY = opts.offsetAccelY;
	int32_t offAZ = opts.offsetAccelZ;

	uint8_t buf[12];
	spiReadRegs(s_spi, s_csPin, LSM6DSR_OUTX_L_G, buf, 12);

	// 与 preprocess 一致：角速度/加速度计都按传感器原始轴返回
	int16_t calG[3];
	calG[0] = (int16_t)(read16LE(buf + 0) - offX);
	calG[1] = (int16_t)(read16LE(buf + 2) - offY);
	calG[2] = (int16_t)(read16LE(buf + 4) - offZ);
	accel[0] = (int16_t)(read16LE(buf + 6) - offAX);
	accel[1] = (int16_t)(read16LE(buf + 8) - offAY);
	accel[2] = (int16_t)(read16LE(buf + 10) - offAZ);
	const float teS = lsm6dsr_compute_dt_s(time_us_64(), s_apiLastSampleUs);

	// 一欧元滤波（与 preprocess 中 applyOneEuroFilter 一致）
	if (opts.gyroOneEuroFilterEnabled) {
		const float alpha = 1.0f / (1.0f + kOneEuroTau / teS);
		for (int i = 0; i < 3; i++) {
			float in = (float)calG[i];
			if (!s_apiOneEuroInited) {
				s_apiOneEuroState[i] = in;
			} else {
				s_apiOneEuroState[i] = alpha * in + (1.0f - alpha) * s_apiOneEuroState[i];
			}
			int32_t out = (int32_t)(s_apiOneEuroState[i] + (s_apiOneEuroState[i] >= 0.0f ? 0.5f : -0.5f));
			if (out > 32767) out = 32767;
			else if (out < -32768) out = -32768;
			gyro[i] = (int16_t)out;
		}
		s_apiOneEuroInited = true;
	} else {
		s_apiOneEuroInited = false;
		for (int i = 0; i < 3; i++) gyro[i] = calG[i];
	}

	return true;
}

// 校准使用未经过一欧元滤波的原始 SPI 数据，仅对多帧 raw 取平均作为零偏
bool lsm6dsr_calibrate_gyro(int32_t* offsetX, int32_t* offsetY, int32_t* offsetZ) {
	if (!s_spi || s_csPin < 0 || !offsetX || !offsetY || !offsetZ) return false;
	int64_t sumX = 0, sumY = 0, sumZ = 0;
	const uint32_t n = LSM6DSR_GYRO_CAL_SAMPLES;
	for (uint32_t i = 0; i < n; i++) {
		uint8_t buf[12];
		spiReadRegs(s_spi, s_csPin, LSM6DSR_OUTX_L_G, buf, 12);
		sumX += read16LE(buf + 0);
		sumY += read16LE(buf + 2);
		sumZ += read16LE(buf + 4);
		busy_wait_ms(LSM6DSR_GYRO_CAL_DELAY_MS);
	}
	// 角速度按传感器原始轴：offset = avg(raw)
	*offsetX = (int32_t)(sumX / (int64_t)n);
	*offsetY = (int32_t)(sumY / (int64_t)n);
	*offsetZ = (int32_t)(sumZ / (int64_t)n);
	return true;
}

// 加速度计零漂校准（alpakka 方法）：静止放置，采样取平均；X/Y 为零偏，Z 为零偏减 1G（静止时重力向下）
bool lsm6dsr_calibrate_accel(int32_t* offsetX, int32_t* offsetY, int32_t* offsetZ) {
	if (!s_spi || s_csPin < 0 || !offsetX || !offsetY || !offsetZ) return false;
	int64_t sumX = 0, sumY = 0, sumZ = 0;
	const uint32_t n = LSM6DSR_ACCEL_CAL_SAMPLES;
	for (uint32_t i = 0; i < n; i++) {
		uint8_t buf[12];
		spiReadRegs(s_spi, s_csPin, LSM6DSR_OUTX_L_G, buf, 12);
		sumX += read16LE(buf + 6);
		sumY += read16LE(buf + 8);
		sumZ += read16LE(buf + 10);
	}
	// 按传感器原始轴计算 offset，协议层再做各自轴映射。
	*offsetX = (int32_t)(sumX / (int64_t)n);
	*offsetY = (int32_t)(sumY / (int64_t)n);
	*offsetZ = (int32_t)(sumZ / (int64_t)n) - LSM6DSR_ACCEL_1G_LSB_INT;
	return true;
}

// 将 GpioAction 解析为 buttonMask/dpadMask（仅支持上下左右、B1-B4、L1/L2/R1/R2、S1/S2）；在 setup/reinit 时调用，运行时仅按位判断
void LSM6DSRIMUAddon::buildEngageMasks() {
	engageButtonMaskAny = 0;
	engageDpadMaskAny = 0;
	hasEngageKeys = (engageKeysCount > 0);
	for (size_t i = 0; i < 16; i++) {
		engageButtonMask[i] = 0;
		engageDpadMask[i] = 0;
		if (i >= engageKeysCount) continue;
		switch ((GpioAction)engageKeys[i]) {
		case GpioAction::BUTTON_PRESS_UP:    engageDpadMask[i] = GAMEPAD_MASK_UP; break;
		case GpioAction::BUTTON_PRESS_DOWN:  engageDpadMask[i] = GAMEPAD_MASK_DOWN; break;
		case GpioAction::BUTTON_PRESS_LEFT:  engageDpadMask[i] = GAMEPAD_MASK_LEFT; break;
		case GpioAction::BUTTON_PRESS_RIGHT: engageDpadMask[i] = GAMEPAD_MASK_RIGHT; break;
		case GpioAction::BUTTON_PRESS_B1:    engageButtonMask[i] = GAMEPAD_MASK_B1; break;
		case GpioAction::BUTTON_PRESS_B2:    engageButtonMask[i] = GAMEPAD_MASK_B2; break;
		case GpioAction::BUTTON_PRESS_B3:    engageButtonMask[i] = GAMEPAD_MASK_B3; break;
		case GpioAction::BUTTON_PRESS_B4:    engageButtonMask[i] = GAMEPAD_MASK_B4; break;
		case GpioAction::BUTTON_PRESS_L1:    engageButtonMask[i] = GAMEPAD_MASK_L1; break;
		case GpioAction::BUTTON_PRESS_L2:    engageButtonMask[i] = GAMEPAD_MASK_L2; break;
		case GpioAction::BUTTON_PRESS_R1:    engageButtonMask[i] = GAMEPAD_MASK_R1; break;
		case GpioAction::BUTTON_PRESS_R2:    engageButtonMask[i] = GAMEPAD_MASK_R2; break;
		case GpioAction::BUTTON_PRESS_S1:    engageButtonMask[i] = GAMEPAD_MASK_S1; break;
		case GpioAction::BUTTON_PRESS_S2:    engageButtonMask[i] = GAMEPAD_MASK_S2; break;
		default:
			break;
		}
		engageButtonMaskAny |= engageButtonMask[i];
		engageDpadMaskAny |= engageDpadMask[i];
	}
	hasEngageKeys = hasEngageKeys && (engageButtonMaskAny != 0 || engageDpadMaskAny != 0);
}

// 将陀螺仪/加速度计写入 DS4 协议（原生陀螺仪）
static void outputGyroToDS4(Gamepad* gamepad, const int16_t calG[3], const int16_t rawA[3]) {
	// 换算到 DS4 协议单位：主机 deg/s = report * (61/1000)，LSM6DSR @500dps 为 0.0175 deg/s/LSB → report = calG * 175/610
	// DS4 协议轴约定：X 轴取反在此处处理（而非源数据层）。
	int32_t ds4x = -(int32_t)calG[0] * (int32_t)LSM6DSR_GYRO_500DPS_NUMER / (int32_t)LSM6DSR_GYRO_500DPS_DENOM;
	int32_t ds4y = (int32_t)calG[1] * (int32_t)LSM6DSR_GYRO_500DPS_NUMER / (int32_t)LSM6DSR_GYRO_500DPS_DENOM;
	int32_t ds4z = (int32_t)calG[2] * (int32_t)LSM6DSR_GYRO_500DPS_NUMER / (int32_t)LSM6DSR_GYRO_500DPS_DENOM;
	if (ds4x > 32767) ds4x = 32767; else if (ds4x < -32767) ds4x = -32767;
	if (ds4y > 32767) ds4y = 32767; else if (ds4y < -32767) ds4y = -32767;
	if (ds4z > 32767) ds4z = 32767; else if (ds4z < -32767) ds4z = -32767;
	gamepad->auxState.sensors.gyroscope.enabled = true;
	gamepad->auxState.sensors.gyroscope.active = true;
	gamepad->auxState.sensors.gyroscope.x = (uint16_t)(int16_t)ds4x;
	gamepad->auxState.sensors.gyroscope.y = (uint16_t)(int16_t)ds4y;
	gamepad->auxState.sensors.gyroscope.z = (uint16_t)(int16_t)ds4z;
	gamepad->auxState.sensors.accelerometer.enabled = true;
	gamepad->auxState.sensors.accelerometer.active = true;
	// DS4 协议轴映射：X 取反，Y/Z 交换。
	gamepad->auxState.sensors.accelerometer.x = (uint16_t)(int16_t)(-(int32_t)rawA[0]);
	gamepad->auxState.sensors.accelerometer.y = (uint16_t)(int16_t)rawA[2];
	gamepad->auxState.sensors.accelerometer.z = (uint16_t)(int16_t)rawA[1];
}

// 与 outputGyroToDS4 相同的角速度标定；加速度按 LSM6DSR 4g 与 Nintendo 官方 int16 标度（≈ raw/2）对齐 deku 文档
static void writeLe16(uint8_t* p, int16_t v) {
	p[0] = (uint8_t)(v & 0xFF);
	p[1] = (uint8_t)((uint16_t)(v) >> 8);
}

void LSM6DSRIMUAddon::outputGyroToSwitchPro(Gamepad* gamepad, const int16_t calG[3], const int16_t rawA[3]) {
	// 先换算到协议单位（与 DS4 同量纲，X 轴在此处完成协议向取反）。
	int32_t ds4x = -(int32_t)calG[0] * (int32_t)LSM6DSR_GYRO_500DPS_NUMER / (int32_t)LSM6DSR_GYRO_500DPS_DENOM;
	int32_t ds4y = (int32_t)calG[1] * (int32_t)LSM6DSR_GYRO_500DPS_NUMER / (int32_t)LSM6DSR_GYRO_500DPS_DENOM;
	int32_t ds4z = (int32_t)calG[2] * (int32_t)LSM6DSR_GYRO_500DPS_NUMER / (int32_t)LSM6DSR_GYRO_500DPS_DENOM;
	if (ds4x > 32767) ds4x = 32767; else if (ds4x < -32767) ds4x = -32767;
	if (ds4y > 32767) ds4y = 32767; else if (ds4y < -32767) ds4y = -32767;
	if (ds4z > 32767) ds4z = 32767; else if (ds4z < -32767) ds4z = -32767;

	// NS Pro 最终映射（与 PS4 物理运动一致）：
	// NS Pro 坐标系：X=向上, Y=向左, Z=向后
	// Gyro : gx=ds4y, gy=-ds4x, gz=-ds4z（X/Y互换，Z取反）
	// Accel: 先按 DS4 映射，再按 NS Pro 变换并除以2
	int16_t gx = (int16_t)(ds4y);
	int16_t gy = (int16_t)(-ds4x);
	int16_t gz = (int16_t)(-ds4z);
	int16_t ds4AccelX = (int16_t)(-(int32_t)rawA[0]);
	int16_t ds4AccelY = (int16_t)rawA[2];
	int16_t ds4AccelZ = (int16_t)rawA[1];
	int16_t ax = (int16_t)((int32_t)ds4AccelZ / 2);
	int16_t ay = (int16_t)(-(int32_t)ds4AccelX / 2);
	int16_t az = (int16_t)(-(int32_t)ds4AccelY / 2);

	// 将当前帧数据存储到历史缓冲区
	uint8_t currentFrame[12];
	writeLe16(currentFrame + 0, ax);
	writeLe16(currentFrame + 2, ay);
	writeLe16(currentFrame + 4, az);
	writeLe16(currentFrame + 6, gx);
	writeLe16(currentFrame + 8, gy);
	writeLe16(currentFrame + 10, gz);

	// Switch Pro 协议要求 IMU 数据包含三组不同时刻的采样（共36字节）
	// 数据顺序：从旧到新排列（第一组最旧，第三组最新）
	uint8_t* d = gamepad->auxState.sensors.switchProImuData;
	// 第一组：最旧帧
	memcpy(d + 0, imuHistory[(imuHistoryIndex + 1) % 3], 12);
	// 第二组：中间帧
	memcpy(d + 12, imuHistory[(imuHistoryIndex + 2) % 3], 12);
	// 第三组：当前帧（最新）
	memcpy(d + 24, currentFrame, 12);

	// 更新历史缓冲区（循环缓冲区）- 在输出之后再存储当前帧
	memcpy(imuHistory[imuHistoryIndex], currentFrame, 12);
	imuHistoryIndex = (imuHistoryIndex + 1) % 3;

	gamepad->auxState.sensors.switchProImuDataActive = true;
	gamepad->auxState.sensors.gyroscope.enabled = false;
	gamepad->auxState.sensors.gyroscope.active = false;
	gamepad->auxState.sensors.gyroscope.x = 0;
	gamepad->auxState.sensors.gyroscope.y = 0;
	gamepad->auxState.sensors.gyroscope.z = 0;
	gamepad->auxState.sensors.accelerometer.enabled = false;
	gamepad->auxState.sensors.accelerometer.active = false;
	gamepad->auxState.sensors.accelerometer.x = 0;
	gamepad->auxState.sensors.accelerometer.y = 0;
	gamepad->auxState.sensors.accelerometer.z = 0;
}

// 陀螺仪→鼠标基准灵敏度（LSM6DSR 17.5 mdps/LSB，与 alpakka CFG_GYRO_SENSITIVITY = 2^-9*1.45 一致）。
// 按「每帧 1 ms」标定，运行时按 dt 缩放，确保 250/500/1000Hz 手感一致。
#define GYRO_MOUSE_BASE_SENS  (1.45f / 512.0f)

// 低区平滑（alpakka hssnf）：小幅度角速度压缩，减少微小抖动，|x|<t 时应用
// hssnf(t,k,x) = x(1-k) / (1 - x*k/t)，t=1.0, k=0.5
static float hssnf(float t, float k, float x) {
	float a = x * (1.0f - k);
	float b = 1.0f - (x * k / t);
	return (b != 0.0f) ? (a / b) : x;
}

// 将陀螺仪输出到 HID 鼠标：mapMode、灵敏度、低区平滑(hssnf)、亚像素累积、轴向反转。
void LSM6DSRIMUAddon::outputGyroToMouse(Gamepad* gamepad, const int16_t calG[3], float dtS) {
	const int mapMode = gyroMouseMapMode;
	const int inv = gyroMouseInvert;
	const float sensLR = gyroMouseSensLR;
	const float sensUD = gyroMouseSensUD;

	int32_t lr_raw = (mapMode == 0) ? (int32_t)calG[1] : (int32_t)calG[2];
	// 保持既有鼠标体感方向：UD 轴继续使用 X 反向语义。
	int32_t ud_raw = -(int32_t)calG[0];
	if (lr_raw < gyroMouseDeadzone && lr_raw > -gyroMouseDeadzone) lr_raw = 0;
	if (ud_raw < gyroMouseDeadzone && ud_raw > -gyroMouseDeadzone) ud_raw = 0;

	const float dtScale = dtS / LSM6DSR_ONE_EURO_TE_FALLBACK_S;
	float lr_float = (float)lr_raw * GYRO_MOUSE_BASE_SENS * sensLR * dtScale;
	float ud_float = (float)ud_raw * GYRO_MOUSE_BASE_SENS * sensUD * dtScale;

	// 低区平滑（与 alpakka Gyro__report_incremental 一致：t=1.0, k=0.5，仅对 |v|<t 应用）
	const float hssnf_t = 1.0f;
	const float hssnf_k = 0.5f;
	if      (lr_float > 0.0f && lr_float < hssnf_t) lr_float =  hssnf(hssnf_t, hssnf_k, lr_float);
	else if (lr_float < 0.0f && lr_float > -hssnf_t) lr_float = -hssnf(hssnf_t, hssnf_k, -lr_float);
	if      (ud_float > 0.0f && ud_float < hssnf_t) ud_float =  hssnf(hssnf_t, hssnf_k, ud_float);
	else if (ud_float < 0.0f && ud_float > -hssnf_t) ud_float = -hssnf(hssnf_t, hssnf_k, -ud_float);

	lr_float += mouseSubX;
	ud_float += mouseSubY;

	float intPart;
	mouseSubX = std::modf(lr_float, &intPart);
	int dx = (int)intPart;
	mouseSubY = std::modf(ud_float, &intPart);
	int dy = (int)intPart;

	// 先全部反转，使前端默认「无反转」对应实际使用中需要的方向
	dx = -dx;
	dy = -dy;
	if (inv & 1) dx = -dx;
	if (inv & 2) dy = -dy;

	if (dx > 32767) dx = 32767;
	else if (dx < -32768) dx = -32768;
	if (dy > 32767) dy = 32767;
	else if (dy < -32768) dy = -32768;

	gamepad->auxState.sensors.mouse.x = (int16_t)dx;
	gamepad->auxState.sensors.mouse.y = (int16_t)dy;
	gamepad->auxState.sensors.mouse.enabled = true;
	gamepad->auxState.sensors.mouse.active = true;
	mouseOutputCleared = false;
}

void LSM6DSRIMUAddon::clearMouseOutput(Gamepad* gamepad) {
	if (mouseOutputCleared) return;
	gamepad->auxState.sensors.mouse.enabled = false;
	gamepad->auxState.sensors.mouse.active = false;
	gamepad->auxState.sensors.mouse.x = 0;
	gamepad->auxState.sensors.mouse.y = 0;
	mouseOutputCleared = true;
}

void LSM6DSRIMUAddon::clearGyroOutput(Gamepad* gamepad) {
	if (gyroOutputCleared) return;
	gamepad->auxState.sensors.gyroscope.enabled = false;
	gamepad->auxState.sensors.gyroscope.active = false;
	gamepad->auxState.sensors.gyroscope.x = 0;
	gamepad->auxState.sensors.gyroscope.y = 0;
	gamepad->auxState.sensors.gyroscope.z = 0;
	gamepad->auxState.sensors.accelerometer.enabled = false;
	gamepad->auxState.sensors.accelerometer.active = false;
	gamepad->auxState.sensors.accelerometer.x = 0;
	gamepad->auxState.sensors.accelerometer.y = 0;
	gamepad->auxState.sensors.accelerometer.z = 0;
	gamepad->auxState.sensors.switchProImuDataActive = false;
	gyroOutputCleared = true;
}

void LSM6DSRIMUAddon::preprocess() {
	Gamepad* gamepad = Storage::getInstance().GetGamepad();
	if (!gamepad) return;

	const InputMode inputMode = gamepad->getOptions().inputMode;

	// DS/NS 原生：仅 USB 为 PS4 / PS4B / NS PRO 时读 SPI；否则跳过本插件以省 CPU
	if (outputMode == LSM6DSR_OUTPUT_DS4) {
		const bool nativeUsb =
			(inputMode == INPUT_MODE_PS4 || inputMode == INPUT_MODE_PS4B || inputMode == INPUT_MODE_SWITCH_PRO);
		if (!nativeUsb) {
			clearGyroOutput(gamepad);
			return;
		}
	}

	// 根据生效方式决定是否执行陀螺仪输出（运行时仅按位判断预解析的 mask）
	const bool anyEngageKeyPressed = hasEngageKeys &&
		(((gamepad->state.buttons & engageButtonMaskAny) != 0) ||
		 ((gamepad->state.dpad & engageDpadMaskAny) != 0));
	const bool shouldRun = (engageMode == LSM6DSR_ENGAGE_ALWAYS) ||
	                       (engageMode == LSM6DSR_ENGAGE_ON_KEY && anyEngageKeyPressed) ||
	                       (engageMode == LSM6DSR_ENGAGE_PAUSE_ON_KEY && !anyEngageKeyPressed);
	if (!shouldRun) {
		if (outputMode == LSM6DSR_OUTPUT_MOUSE) {
			clearMouseOutput(gamepad);
			mouseSubX = 0.0f;
			mouseSubY = 0.0f;
		}
		if (outputMode == LSM6DSR_OUTPUT_DS4) {
			clearGyroOutput(gamepad);
		}
		prevButtons = gamepad->state.buttons;
		return;
	}

	// 左/右摇杆输出模式已废弃：避免无意义的 SPI 与滤波
	if (outputMode != LSM6DSR_OUTPUT_DS4 && outputMode != LSM6DSR_OUTPUT_MOUSE) {
		clearGyroOutput(gamepad);
		clearMouseOutput(gamepad);
		prevButtons = gamepad->state.buttons;
		return;
	}

	spiReadRegs(spi, csPin, LSM6DSR_OUTX_L_G, readBuf, 12);
	// 角速度/加速度计都保持传感器原始轴；协议映射在输出函数中处理
	rawG[0] = read16LE(readBuf + 0);
	rawG[1] = read16LE(readBuf + 2);
	rawG[2] = read16LE(readBuf + 4);
	rawA[0] = read16LE(readBuf + 6);
	rawA[1] = read16LE(readBuf + 8);
	rawA[2] = read16LE(readBuf + 10);

	const uint64_t nowUs = time_us_64();
	const float teS = lsm6dsr_compute_dt_s(nowUs, lastSampleUs);

	calG[0] = (int16_t)(rawG[0] - offsetGyroX);
	calG[1] = (int16_t)(rawG[1] - offsetGyroY);
	calG[2] = (int16_t)(rawG[2] - offsetGyroZ);

	if (oneEuroFilterEnabled)
		applyOneEuroFilter(teS);
	else
		oneEuroInited = false;

	// 鼠标模式：按键按下沿触发短窗口抑制，过滤点击引起的微位移。
	if (outputMode == LSM6DSR_OUTPUT_MOUSE) {
		const uint32_t pressedEdge = (gamepad->state.buttons & ~prevButtons);
		prevButtons = gamepad->state.buttons;
		if (pressedEdge != 0) {
			mouseSuppressUntilUs = nowUs + (uint64_t)LSM6DSR_MOUSE_CLICK_SUPPRESS_US;
		}
	} else {
		prevButtons = gamepad->state.buttons;
	}

	switch (outputMode) {
	case LSM6DSR_OUTPUT_DS4:
		clearMouseOutput(gamepad);
		if (inputMode == INPUT_MODE_SWITCH_PRO) {
			outputGyroToSwitchPro(gamepad, calG, rawA);
			gyroOutputCleared = false;
		} else {
			outputGyroToDS4(gamepad, calG, rawA);
			gyroOutputCleared = false;
		}
		break;
	case LSM6DSR_OUTPUT_MOUSE:
		clearGyroOutput(gamepad);
		if (nowUs < mouseSuppressUntilUs) {
			mouseSubX = 0.0f;
			mouseSubY = 0.0f;
			gamepad->auxState.sensors.mouse.x = 0;
			gamepad->auxState.sensors.mouse.y = 0;
			gamepad->auxState.sensors.mouse.enabled = true;
			gamepad->auxState.sensors.mouse.active = true;
			mouseOutputCleared = false;
		} else {
			outputGyroToMouse(gamepad, calG, teS);
		}
		break;
	default:
		clearGyroOutput(gamepad);
		clearMouseOutput(gamepad);
		break;
	}
}
