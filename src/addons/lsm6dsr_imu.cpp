#include "addons/lsm6dsr_imu.h"
#include "config.pb.h"
#include "storagemanager.h"
#include "peripheralmanager.h"
#include "gamepad.h"
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
#define LSM6DSR_FIFO_CTRL5    0x0BU
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
// LSM6DSR 在 ±500 dps 下 17.5 mdps/LSB → 1 LSB = 0.0175 deg/s，故 DS4_report = raw * 0.0175 / 0.061 = raw * 175/610
#define LSM6DSR_GYRO_500DPS_NUMER   175   // 17.5 * 10，用于 report = calG * 175 / 610
#define LSM6DSR_GYRO_500DPS_DENOM   610   // 61 * 10，与 DS4 0.061 deg/s/LSB 一致

// 按需读取用（网页模式下 preprocess 不运行，API 调用时现场读一次）
static PeripheralSPI* s_spi = nullptr;
static int8_t s_csPin = -1;

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
	if (!opts.enabled || !opts.has_csPin) return false;
	uint8_t block = opts.has_spiBlock ? (uint8_t)opts.spiBlock : 0;
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
	spi_ = nullptr;
	csPin_ = -1;
	offsetGyroX_ = offsetGyroY_ = offsetGyroZ_ = 0;
	s_spi = nullptr;
	s_csPin = -1;

	const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
	csPin_ = (int8_t)opts.csPin;
	uint8_t block = opts.has_spiBlock ? (uint8_t)opts.spiBlock : 0;
	spi_ = PeripheralManager::getInstance().getSPI(block);
	offsetGyroX_ = opts.has_offsetGyroX ? opts.offsetGyroX : 0;
	offsetGyroY_ = opts.has_offsetGyroY ? opts.offsetGyroY : 0;
	offsetGyroZ_ = opts.has_offsetGyroZ ? opts.offsetGyroZ : 0;

	gpio_init((uint)csPin_);
	gpio_set_dir((uint)csPin_, GPIO_OUT);
	gpio_put((uint)csPin_, true);

	// SW_RESET 干净启动：防异常 SPI 状态/FIFO/I3C 残留（WHO_AM_I 已在 available() 中校验）
	spi_->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL3_C, 0x01);
	spi_->endTransaction();
	busy_wait_ms(10);

	// Disable FIFO (避免延迟), I3C, High Performance, ODR 1666 Hz, 4g acc, 500 dps gyro, BDU+IF_INC
	spi_->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
	// CTRL2_G: 0x84 = ODR 1.66kHz (0b10) + FS 500 dps (0b01) → 17.5 mdps/LSB
	spiWriteReg(spi_, csPin_, LSM6DSR_FIFO_CTRL5, 0x00);
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL9_XL, 0x02);
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL4_C, 0x06);
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL6_C, 0x02);
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL7_G, 0x00);
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL8_XL, 0x00);
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL3_C, 0x44);
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL1_XL, 0xA8);
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL2_G, 0x84);  // 500 dps
	spi_->endTransaction();

	s_spi = spi_;
	s_csPin = csPin_;
}

void LSM6DSRIMUAddon::reinit() {
	// 陀螺仪校准数据针对设备全局，不随 profile 切换，无需重载
}

bool getLSM6DSRRawData(int16_t gyro[3], int16_t accel[3]) {
	if (!s_spi || s_csPin < 0) return false;
	const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
	int32_t offX = opts.has_offsetGyroX ? opts.offsetGyroX : 0;
	int32_t offY = opts.has_offsetGyroY ? opts.offsetGyroY : 0;
	int32_t offZ = opts.has_offsetGyroZ ? opts.offsetGyroZ : 0;

	s_spi->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
	uint8_t buf[12];
	spiReadRegs(s_spi, s_csPin, LSM6DSR_OUTX_L_G, buf, 12);
	int16_t r0 = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
	int16_t r1 = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
	int16_t r2 = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
	s_spi->endTransaction();

	gyro[0] = (int16_t)(-r0 - offX);
	gyro[1] = (int16_t)(-r1 - offY);
	gyro[2] = (int16_t)(r2 - offZ);
	accel[0] = -(int16_t)((uint16_t)buf[6] | ((uint16_t)buf[7] << 8));
	accel[1] = -(int16_t)((uint16_t)buf[8] | ((uint16_t)buf[9] << 8));
	accel[2] = (int16_t)((uint16_t)buf[10] | ((uint16_t)buf[11] << 8));
	return true;
}

bool lsm6dsr_calibrate_gyro(int32_t* offsetX, int32_t* offsetY, int32_t* offsetZ) {
	if (!s_spi || s_csPin < 0 || !offsetX || !offsetY || !offsetZ) return false;
	int64_t sumX = 0, sumY = 0, sumZ = 0;
	const uint32_t n = LSM6DSR_GYRO_CAL_SAMPLES;
	for (uint32_t i = 0; i < n; i++) {
		s_spi->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
		uint8_t buf[12];
		spiReadRegs(s_spi, s_csPin, LSM6DSR_OUTX_L_G, buf, 12);
		s_spi->endTransaction();
		int16_t rx = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
		int16_t ry = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
		int16_t rz = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
		sumX += rx;
		sumY += ry;
		sumZ += rz;
		busy_wait_ms(LSM6DSR_GYRO_CAL_DELAY_MS);
	}
	// 与输出轴一致：X/Y 取反后减偏移，故存储 offset = -avg(raw)；Z 不取反，存储 offset = avg(raw)
	*offsetX = -(int32_t)(sumX / (int64_t)n);
	*offsetY = -(int32_t)(sumY / (int64_t)n);
	*offsetZ = (int32_t)(sumZ / (int64_t)n);
	return true;
}

void LSM6DSRIMUAddon::preprocess() {
	spi_->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);

	uint8_t buf[12];
	int16_t rawG[3], rawA[3];

	spiReadRegs(spi_, csPin_, LSM6DSR_OUTX_L_G, buf, 12);
	rawG[0] = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
	rawG[1] = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
	rawG[2] = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
	rawA[0] = (int16_t)((uint16_t)buf[6] | ((uint16_t)buf[7] << 8));
	rawA[1] = (int16_t)((uint16_t)buf[8] | ((uint16_t)buf[9] << 8));
	rawA[2] = (int16_t)((uint16_t)buf[10] | ((uint16_t)buf[11] << 8));

	spi_->endTransaction();

	int16_t calG[3] = {
		(int16_t)(-rawG[0] - offsetGyroX_),
		(int16_t)(-rawG[1] - offsetGyroY_),
		(int16_t)(rawG[2] - offsetGyroZ_),
	};

	// 换算到 DS4 协议单位：主机 deg/s = report * (61/1000)，LSM6DSR @500dps 为 0.0175 deg/s/LSB → report = calG * 175/610
	int32_t ds4x = (int32_t)calG[0] * (int32_t)LSM6DSR_GYRO_500DPS_NUMER / (int32_t)LSM6DSR_GYRO_500DPS_DENOM;
	int32_t ds4y = (int32_t)calG[1] * (int32_t)LSM6DSR_GYRO_500DPS_NUMER / (int32_t)LSM6DSR_GYRO_500DPS_DENOM;
	int32_t ds4z = (int32_t)calG[2] * (int32_t)LSM6DSR_GYRO_500DPS_NUMER / (int32_t)LSM6DSR_GYRO_500DPS_DENOM;
	if (ds4x > 32767) ds4x = 32767; else if (ds4x < -32767) ds4x = -32767;
	if (ds4y > 32767) ds4y = 32767; else if (ds4y < -32767) ds4y = -32767;
	if (ds4z > 32767) ds4z = 32767; else if (ds4z < -32767) ds4z = -32767;

	// 必须写入 GetGamepad()：主循环中 inputDriver->process(gamepad) 使用的是 gamepad，不是 processedGamepad
	Gamepad* gamepad = Storage::getInstance().GetGamepad();
	if (gamepad) {
		gamepad->auxState.sensors.gyroscope.enabled = true;
		gamepad->auxState.sensors.gyroscope.active = true;
		gamepad->auxState.sensors.gyroscope.x = (uint16_t)(int16_t)ds4x;
		gamepad->auxState.sensors.gyroscope.y = (uint16_t)(int16_t)ds4y;
		gamepad->auxState.sensors.gyroscope.z = (uint16_t)(int16_t)ds4z;
		gamepad->auxState.sensors.accelerometer.enabled = true;
		gamepad->auxState.sensors.accelerometer.active = true;
		gamepad->auxState.sensors.accelerometer.x = (uint16_t)(int16_t)(-rawA[0]);
		gamepad->auxState.sensors.accelerometer.y = (uint16_t)(int16_t)(-rawA[1]);
		gamepad->auxState.sensors.accelerometer.z = (uint16_t)(int16_t)rawA[2];
	}
}
