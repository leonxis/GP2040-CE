#include "addons/lsm6dsr_imu.h"
#include "config.pb.h"
#include "storagemanager.h"
#include "peripheralmanager.h"
#include "gamepad.h"
#include "gamepad/GamepadState.h"
#include "hardware/gpio.h"
#include "pico/time.h"

// CS 片选：主动推挽驱动，避免 SPI select/deselect 的上拉/下拉驱动不足
#define LSM6DSR_CS_SELECT(cs)   do { gpio_put((uint)(cs), 0); } while (0)
#define LSM6DSR_CS_DESELECT(cs) do { gpio_put((uint)(cs), 1); } while (0)

// Register map (ST LSM6DSR, same for I2C/SPI)
#define LSM6DSR_WHO_AM_I      0x0FU
#define LSM6DSR_ID            0x6BU
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

// 陀螺仪零偏校准：静止采样数量与间隔（与 alpakka 思路一致，采样平均作为零偏）
#define LSM6DSR_GYRO_CAL_SAMPLES  500
#define LSM6DSR_GYRO_CAL_DELAY_MS 2

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

// 陀螺仪变化率限制（slew limit）：每帧允许的最大变化 LSB。量程 ±500 dps 下 17.5 mdps/LSB，2000 LSB ≈ 35 deg/s/帧 → 30～180 deg/s 的开关震动尖峰被摊平到多帧，峰值显著降低
#define LSM6DSR_GYRO_SLEW_LSB       2000

// 一欧元滤波：低通 alpha = 1/(1+tau/Te)，tau=1/(2*pi*fc)。ODR 1.66kHz → Te≈0.0006s，fc=5Hz → alpha≈0.018
#define LSM6DSR_ONE_EURO_TE_S        (1.0f / 1666.0f)
#define LSM6DSR_ONE_EURO_FC_HZ      5.0f

// 按需读取用（网页模式下 preprocess 不运行，API 调用时现场读一次）
static PeripheralSPI* s_spi = nullptr;
static int8_t s_csPin = -1;

static inline int16_t read16LE(const uint8_t* p) {
	return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void spiReadRegs(PeripheralSPI* spi, int8_t csPin, uint8_t reg, uint8_t* buf, size_t len) {
	if (len == 0) return;
	LSM6DSR_CS_SELECT(csPin);
	(void)spi->transfer(reg | LSM6DSR_SPI_READ);
	for (size_t i = 0; i < len; i++)
		buf[i] = spi->transfer(0);
	LSM6DSR_CS_DESELECT(csPin);
}

static void spiWriteReg(PeripheralSPI* spi, int8_t csPin, uint8_t reg, uint8_t val) {
	LSM6DSR_CS_SELECT(csPin);
	spi->transfer(reg);
	spi->transfer(val);
	LSM6DSR_CS_DESELECT(csPin);
}

bool LSM6DSRIMUAddon::available() {
	const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
	if (!opts.enabled || opts.csPin < 0) return false;
	uint8_t block = (uint8_t)opts.spiBlock;
	PeripheralSPI* spi = PeripheralManager::getInstance().getSPI(block);
	if (!PeripheralManager::getInstance().isSPIEnabled(block) || !spi || !spi->configured)
		return false;
	int8_t csPin = (int8_t)opts.csPin;
	gpio_init((uint)csPin);
	gpio_set_dir((uint)csPin, GPIO_OUT);
	gpio_put((uint)csPin, true);
	spi->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
	LSM6DSR_CS_SELECT(csPin);
	(void)spi->transfer(LSM6DSR_SPI_READ | LSM6DSR_WHO_AM_I);
	uint8_t id = spi->transfer(0);
	LSM6DSR_CS_DESELECT(csPin);
	spi->endTransaction();
	return (id == LSM6DSR_ID);
}

void LSM6DSRIMUAddon::setup() {
	s_spi = nullptr;
	s_csPin = -1;

	const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
	csPin = (int8_t)opts.csPin;
	uint8_t block = (uint8_t)opts.spiBlock;
	spi = PeripheralManager::getInstance().getSPI(block);
	offsetGyroX = opts.offsetGyroX;
	offsetGyroY = opts.offsetGyroY;
	offsetGyroZ = opts.offsetGyroZ;
	outputMode = opts.outputMode;
	engageMode = opts.engageMode;
	spikeFilterEnabled = opts.gyroSpikeFilterEnabled;
	oneEuroFilterEnabled = opts.gyroOneEuroFilterEnabled;
	engageKeysCount = opts.gyroEngageKeys_count <= 16 ? opts.gyroEngageKeys_count : 16;
	for (size_t i = 0; i < engageKeysCount; i++)
		engageKeys[i] = opts.gyroEngageKeys[i];
	buildEngageMasks();
	for (int i = 0; i < 3; i++) {
		filterG[i] = 0;
		oneEuroState[i] = 0.0f;
	}

	gpio_init((uint)csPin);
	gpio_set_dir((uint)csPin, GPIO_OUT);
	gpio_put((uint)csPin, true);

	// SW_RESET 干净启动：防异常 SPI 状态/FIFO/I3C 残留（WHO_AM_I 已在 available() 中校验）
	spi->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL3_C, 0x01);
	spi->endTransaction();
	busy_wait_ms(10);

	// Disable FIFO (避免延迟), I3C, High Performance, ODR 1666 Hz, 4g acc, 500 dps gyro, BDU+IF_INC
	spi->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
	// CTRL2_G: 0x84 = ODR 1.66kHz (0b10) + FS 500 dps (0b01) → 17.5 mdps/LSB
	spiWriteReg(spi, csPin, LSM6DSR_CTRL9_XL, 0x02);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL4_C, 0x06);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL6_C, 0x02);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL7_G, 0x00);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL8_XL, 0x00);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL3_C, 0x44);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL1_XL, 0xA8);
	spiWriteReg(spi, csPin, LSM6DSR_CTRL2_G, 0x84);  // 500 dps
	spi->endTransaction();

	s_spi = spi;
	s_csPin = csPin;
}

void LSM6DSRIMUAddon::reinit() {
	// 陀螺仪校准数据针对设备全局，不随 profile 切换，无需重载；重新加载陀螺仪模拟方式、生效方式与生效按键
	const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
	outputMode = opts.outputMode;
	engageMode = opts.engageMode;
	spikeFilterEnabled = opts.gyroSpikeFilterEnabled;
	oneEuroFilterEnabled = opts.gyroOneEuroFilterEnabled;
	engageKeysCount = opts.gyroEngageKeys_count <= 16 ? opts.gyroEngageKeys_count : 16;
	for (size_t i = 0; i < engageKeysCount; i++)
		engageKeys[i] = opts.gyroEngageKeys[i];
	buildEngageMasks();
}

void LSM6DSRIMUAddon::applyOneEuroFilter() {
	const float tau = 1.0f / (2.0f * 3.14159265f * LSM6DSR_ONE_EURO_FC_HZ);
	const float alpha = 1.0f / (1.0f + tau / LSM6DSR_ONE_EURO_TE_S);
	for (int i = 0; i < 3; i++) {
		float in = (float)calG[i];
		oneEuroState[i] = alpha * in + (1.0f - alpha) * oneEuroState[i];
		int32_t out = (int32_t)(oneEuroState[i] + (oneEuroState[i] >= 0.0f ? 0.5f : -0.5f));
		if (out > 32767) out = 32767;
		else if (out < -32768) out = -32768;
		calG[i] = (int16_t)out;
	}
}

// 供网页「查看陀螺仪」等使用：raw - offset 后，根据配置应用尖峰滤波与一欧元滤波再返回，与 preprocess 输出一致
static int16_t s_apiPrevFilterG[3] = {0, 0, 0};
static float s_apiOneEuroState[3] = {0.0f, 0.0f, 0.0f};
static bool s_apiOneEuroInited = false;

bool getLSM6DSRRawData(int16_t gyro[3], int16_t accel[3]) {
	if (!s_spi || s_csPin < 0) return false;
	const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
	int32_t offX = opts.offsetGyroX;
	int32_t offY = opts.offsetGyroY;
	int32_t offZ = opts.offsetGyroZ;

	s_spi->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
	uint8_t buf[12];
	spiReadRegs(s_spi, s_csPin, LSM6DSR_OUTX_L_G, buf, 12);
	s_spi->endTransaction();

	int16_t calG[3];
	calG[0] = (int16_t)(read16LE(buf + 0) - offX);
	calG[1] = (int16_t)(read16LE(buf + 2) - offY);
	calG[2] = (int16_t)(read16LE(buf + 4) - offZ);
	accel[0] = read16LE(buf + 6);
	accel[1] = read16LE(buf + 8);
	accel[2] = read16LE(buf + 10);

	// 尖峰滤波（与 preprocess 中 applyGyroSlewLimit 一致）
	if (opts.gyroSpikeFilterEnabled) {
		for (int i = 0; i < 3; i++) {
			int32_t delta = (int32_t)calG[i] - (int32_t)s_apiPrevFilterG[i];
			if (delta > LSM6DSR_GYRO_SLEW_LSB) delta = LSM6DSR_GYRO_SLEW_LSB;
			else if (delta < -LSM6DSR_GYRO_SLEW_LSB) delta = -LSM6DSR_GYRO_SLEW_LSB;
			int32_t next = (int32_t)s_apiPrevFilterG[i] + delta;
			s_apiPrevFilterG[i] = (int16_t)next;
			calG[i] = (int16_t)next;
		}
	} else {
		for (int i = 0; i < 3; i++) s_apiPrevFilterG[i] = calG[i];
	}

	// 一欧元滤波（与 preprocess 中 applyOneEuroFilter 一致）
	if (opts.gyroOneEuroFilterEnabled) {
		const float tau = 1.0f / (2.0f * 3.14159265f * LSM6DSR_ONE_EURO_FC_HZ);
		const float alpha = 1.0f / (1.0f + tau / LSM6DSR_ONE_EURO_TE_S);
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

// 校准使用未经过尖峰滤波与一欧元滤波的原始 SPI 数据，仅对多帧 raw 取平均作为零偏
bool lsm6dsr_calibrate_gyro(int32_t* offsetX, int32_t* offsetY, int32_t* offsetZ) {
	if (!s_spi || s_csPin < 0 || !offsetX || !offsetY || !offsetZ) return false;
	int64_t sumX = 0, sumY = 0, sumZ = 0;
	const uint32_t n = LSM6DSR_GYRO_CAL_SAMPLES;
	for (uint32_t i = 0; i < n; i++) {
		s_spi->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
		uint8_t buf[12];
		spiReadRegs(s_spi, s_csPin, LSM6DSR_OUTX_L_G, buf, 12);
		s_spi->endTransaction();
		sumX += read16LE(buf + 0);
		sumY += read16LE(buf + 2);
		sumZ += read16LE(buf + 4);
		busy_wait_ms(LSM6DSR_GYRO_CAL_DELAY_MS);
	}
	// 陀螺仪 XYZ 均不取反，输出 = raw - offset，故存储 offset = avg(raw)
	*offsetX = (int32_t)(sumX / (int64_t)n);
	*offsetY = (int32_t)(sumY / (int64_t)n);
	*offsetZ = (int32_t)(sumZ / (int64_t)n);
	return true;
}

// 将 GpioAction 解析为 buttonMask/dpadMask（仅支持上下左右、B1-B4、L1/L2/R1/R2、S1/S2）；在 setup/reinit 时调用，运行时仅按位判断
void LSM6DSRIMUAddon::buildEngageMasks() {
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
	}
}

void LSM6DSRIMUAddon::applyGyroSlewLimit() {
	for (int i = 0; i < 3; i++) {
		int32_t delta = (int32_t)calG[i] - (int32_t)filterG[i];
		if (delta > LSM6DSR_GYRO_SLEW_LSB) delta = LSM6DSR_GYRO_SLEW_LSB;
		else if (delta < -LSM6DSR_GYRO_SLEW_LSB) delta = -LSM6DSR_GYRO_SLEW_LSB;
		int32_t next = (int32_t)filterG[i] + delta;
		filterG[i] = (int16_t)next;
		calG[i] = (int16_t)next;
	}
}

// 将陀螺仪/加速度计写入 DS4 协议（原生陀螺仪）
static void outputGyroToDS4(Gamepad* gamepad, const int16_t calG[3], const int16_t rawA[3]) {
	// 换算到 DS4 协议单位：主机 deg/s = report * (61/1000)，LSM6DSR @500dps 为 0.0175 deg/s/LSB → report = calG * 175/610
	int32_t ds4x = (int32_t)calG[0] * (int32_t)LSM6DSR_GYRO_500DPS_NUMER / (int32_t)LSM6DSR_GYRO_500DPS_DENOM;
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
	gamepad->auxState.sensors.accelerometer.x = (uint16_t)(int16_t)rawA[0];
	gamepad->auxState.sensors.accelerometer.y = (uint16_t)(int16_t)rawA[1];
	gamepad->auxState.sensors.accelerometer.z = (uint16_t)(int16_t)rawA[2];
}

// 将陀螺仪输出到摇杆：outputMode 1=左摇杆，2=右摇杆，功能后续补充
static void outputGyroToStick(Gamepad* gamepad, const int16_t calG[3], int outputMode) {
	(void)gamepad;
	(void)calG;
	(void)outputMode;
}

// 将陀螺仪输出到 HID 鼠标，功能后续补充
static void outputGyroToMouse(Gamepad* gamepad, const int16_t calG[3]) {
	(void)gamepad;
	(void)calG;
}

void LSM6DSRIMUAddon::preprocess() {
	spi->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
	spiReadRegs(spi, csPin, LSM6DSR_OUTX_L_G, readBuf, 12);
	spi->endTransaction();

	rawG[0] = read16LE(readBuf + 0);
	rawG[1] = read16LE(readBuf + 2);
	rawG[2] = read16LE(readBuf + 4);
	rawA[0] = read16LE(readBuf + 6);
	rawA[1] = read16LE(readBuf + 8);
	rawA[2] = read16LE(readBuf + 10);

	calG[0] = (int16_t)(rawG[0] - offsetGyroX);
	calG[1] = (int16_t)(rawG[1] - offsetGyroY);
	calG[2] = (int16_t)(rawG[2] - offsetGyroZ);

	if (spikeFilterEnabled)
		applyGyroSlewLimit();
	if (oneEuroFilterEnabled)
		applyOneEuroFilter();

	Gamepad* gamepad = Storage::getInstance().GetGamepad();
	if (!gamepad) return;

	// 根据生效方式决定是否执行陀螺仪输出（运行时仅按位判断预解析的 mask）
	bool anyEngageKeyPressed = false;
	for (size_t i = 0; i < engageKeysCount; i++) {
		if ((gamepad->state.buttons & engageButtonMask[i]) != 0 ||
		    (gamepad->state.dpad & engageDpadMask[i]) != 0) {
			anyEngageKeyPressed = true;
			break;
		}
	}
	bool shouldRun = (engageMode == LSM6DSR_ENGAGE_ALWAYS) ||
	                 (engageMode == LSM6DSR_ENGAGE_ON_KEY && anyEngageKeyPressed) ||
	                 (engageMode == LSM6DSR_ENGAGE_PAUSE_ON_KEY && !anyEngageKeyPressed);
	if (!shouldRun) return;

	switch (outputMode) {
	case LSM6DSR_OUTPUT_DS4:
		outputGyroToDS4(gamepad, calG, rawA);
		break;
	case LSM6DSR_OUTPUT_LEFT_STICK:
	case LSM6DSR_OUTPUT_RIGHT_STICK:
		outputGyroToStick(gamepad, calG, outputMode);
		break;
	case LSM6DSR_OUTPUT_MOUSE:
		outputGyroToMouse(gamepad, calG);
		break;
	default:
		outputGyroToDS4(gamepad, calG, rawA);
		break;
	}
}
