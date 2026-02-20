#include "addons/lsm6dsr_imu.h"
#include "config.pb.h"
#include "storagemanager.h"
#include "peripheralmanager.h"
#include "gamepad/GamepadState.h"
#include "gamepad.h"
#include "BoardConfig.h"
#include "hardware/gpio.h"

// CS 片选：主动推挽驱动，避免 SPI select/deselect 的上拉/下拉驱动不足
#define LSM6DSR_CS_SELECT(cs)   do { gpio_put((uint)(cs), 0); } while (0)
#define LSM6DSR_CS_DESELECT(cs) do { gpio_put((uint)(cs), 1); } while (0)

// Register map (ST LSM6DSR, same for I2C/SPI)
#define LSM6DSR_WHO_AM_I      0x0FU
#define LSM6DSR_ID            0x6BU
#define LSM6DSR_CTRL1_XL      0x10U
#define LSM6DSR_CTRL2_G       0x11U
#define LSM6DSR_FIFO_CTRL1    0x07U
#define LSM6DSR_FIFO_CTRL2    0x08U
#define LSM6DSR_FIFO_CTRL3    0x09U
#define LSM6DSR_FIFO_CTRL4    0x0AU
#define LSM6DSR_FIFO_CTRL5    0x0BU
#define LSM6DSR_CTRL3_C       0x12U
#define LSM6DSR_CTRL4_C       0x13U  // LPF1 关闭 → 最大数字滤波带宽; bit2 I2C_DISABLE
#define LSM6DSR_CTRL4_C_I2C_DISABLE  (1U << 2)
#define LSM6DSR_CTRL6_C       0x15U  // bit4 XL_HM_MODE=0 高性能, bit7 FTYP=0
#define LSM6DSR_CTRL7_G       0x16U  // bit7 G_HM_MODE=0 → Gyro 高性能
#define LSM6DSR_CTRL8_XL      0x17U  // LPF2 关闭 → 最小延迟
#define LSM6DSR_CTRL9_XL      0x18U   // bit1 = I3C_DISABLE
#define LSM6DSR_CTRL9_XL_I3C_DISABLE  (1U << 1)
#define LSM6DSR_OUTX_L_G      0x22U
#define LSM6DSR_OUTX_L_A      0x28U

#define LSM6DSR_SPI_READ      0x80U

// ODR 1666 Hz = 8; Accel 4g = 2 (ST enum fs_xl); Gyro 2000dps = 12
#define LSM6DSR_CTRL1_XL_1666_4G   (0x88U)  // odr_xl=8, fs_xl=2 (4g)
#define LSM6DSR_CTRL2_G_1666_2000  (0x8CU)  // odr_g=8, fs_g=12 (2000dps)
#define LSM6DSR_CTRL3_C_BDU_INC    0x44U   // BDU + IF_INC

// Debug for webconfig
static uint8_t s_debugWhoAmI = 0;
static bool s_debugSpiOk = false;
static bool s_debugImuOk = false;
// 按需读取用（网页模式下 preprocess 不运行，API 调用时现场读一次）
static PeripheralSPI* s_spi = nullptr;
static int8_t s_csPin = -1;

void getLSM6DSRImuDebug(uint8_t* whoAmI, bool* spiOk, bool* imuOk) {
#if LSM6DSR_IMU_ENABLED
	if (whoAmI) *whoAmI = s_debugWhoAmI;
	if (spiOk) *spiOk = s_debugSpiOk;
	if (imuOk) *imuOk = s_debugImuOk;
#else
	if (whoAmI) *whoAmI = 0;
	if (spiOk) *spiOk = false;
	if (imuOk) *imuOk = false;
#endif
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
#if LSM6DSR_IMU_ENABLED
	const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
	if (!opts.enabled) return false;
	uint8_t block = opts.has_spiBlock ? (uint8_t)opts.spiBlock : 0;
	return PeripheralManager::getInstance().isSPIEnabled(block);
#else
	return false;
#endif
}

void LSM6DSRIMUAddon::setup() {
	spi_ = nullptr;
	csPin_ = -1;
	spiOk_ = false;
	imuOk_ = false;
	calibCount_ = 0;
	calibSumX_ = calibSumY_ = calibSumZ_ = 0;
	s_debugWhoAmI = 0;
	s_debugSpiOk = false;
	s_debugImuOk = false;

#if LSM6DSR_IMU_ENABLED
	const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
	if (!opts.enabled || !opts.has_csPin) return;

	csPin_ = (int8_t)opts.csPin;
	uint8_t block = opts.has_spiBlock ? (uint8_t)opts.spiBlock : 0;
	PeripheralSPI* spi = PeripheralManager::getInstance().getSPI(block);
	if (!spi || !spi->configured) return;
	spi_ = spi;

	gpio_init((uint)csPin_);
	gpio_set_dir((uint)csPin_, GPIO_OUT);
	gpio_put((uint)csPin_, true);
	spiOk_ = true;
	s_debugSpiOk = true;

	// 1.5 MHz, SPI mode 0（与 MCP3208 共用总线时一致）
	spi_->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
	LSM6DSR_CS_SELECT(csPin_);
	(void)spi_->transfer(LSM6DSR_SPI_READ | LSM6DSR_WHO_AM_I);
	uint8_t id = spi_->transfer(0);
	LSM6DSR_CS_DESELECT(csPin_);
	spi_->endTransaction();

	s_debugWhoAmI = id;
	if (id != LSM6DSR_ID) {
		spiOk_ = false;
		s_debugSpiOk = false;
		return;
	}

	// Disable FIFO (避免延迟), I3C, High Performance, ODR 1666 Hz, 2g acc, 2000 dps gyro, BDU+IF_INC
	spiWriteReg(spi_, csPin_, LSM6DSR_FIFO_CTRL5, 0x00);  // 禁用 FIFO
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL9_XL, LSM6DSR_CTRL9_XL_I3C_DISABLE);
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL4_C, LSM6DSR_CTRL4_C_I2C_DISABLE);  // 关闭 I2C、LPF1 关闭，最大带宽
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL6_C, 0x00);  // XL_HM_MODE=0, FTYP=0 → Acc 高性能
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL7_G, 0x00);  // G_HM_MODE=0 → Gyro 高性能
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL8_XL, 0x00); // LPF2 关闭，最小延迟
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL3_C, LSM6DSR_CTRL3_C_BDU_INC);
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL1_XL, LSM6DSR_CTRL1_XL_1666_4G);
	spiWriteReg(spi_, csPin_, LSM6DSR_CTRL2_G, LSM6DSR_CTRL2_G_1666_2000);

	imuOk_ = true;
	s_debugImuOk = true;
	s_spi = spi_;
	s_csPin = csPin_;
#endif
}

bool getLSM6DSRRawData(int16_t gyro[3], int16_t accel[3]) {
#if LSM6DSR_IMU_ENABLED
	if (!s_spi || s_csPin < 0 || !s_debugImuOk) return false;
	uint8_t buf[6];
	s_spi->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
	spiReadRegs(s_spi, s_csPin, LSM6DSR_OUTX_L_G, buf, 6);
	gyro[0] = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
	gyro[1] = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
	gyro[2] = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
	spiReadRegs(s_spi, s_csPin, LSM6DSR_OUTX_L_A, buf, 6);
	accel[0] = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
	accel[1] = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
	accel[2] = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
	s_spi->endTransaction();
	return true;
#else
	(void)gyro;
	(void)accel;
	return false;
#endif
}

void LSM6DSRIMUAddon::preprocess() {
#if LSM6DSR_IMU_ENABLED
	if (!spiOk_ || !spi_ || !imuOk_) return;

	uint8_t buf[6];
	int16_t rawG[3], rawA[3];

	spi_->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE0);
	spiReadRegs(spi_, csPin_, LSM6DSR_OUTX_L_G, buf, 6);
	rawG[0] = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
	rawG[1] = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
	rawG[2] = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));

	spiReadRegs(spi_, csPin_, LSM6DSR_OUTX_L_A, buf, 6);
	rawA[0] = (int16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
	rawA[1] = (int16_t)((uint16_t)buf[2] | ((uint16_t)buf[3] << 8));
	rawA[2] = (int16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
	spi_->endTransaction();

	Gamepad* gamepad = Storage::getInstance().GetProcessedGamepad();
	if (gamepad) {
		gamepad->auxState.sensors.gyroscope.enabled = true;
		gamepad->auxState.sensors.gyroscope.active = true;
		gamepad->auxState.sensors.gyroscope.x = (uint16_t)(int16_t)rawG[0];
		gamepad->auxState.sensors.gyroscope.y = (uint16_t)(int16_t)rawG[1];
		gamepad->auxState.sensors.gyroscope.z = (uint16_t)(int16_t)rawG[2];
		gamepad->auxState.sensors.accelerometer.x = (uint16_t)(int16_t)rawA[0];
		gamepad->auxState.sensors.accelerometer.y = (uint16_t)(int16_t)rawA[1];
		gamepad->auxState.sensors.accelerometer.z = (uint16_t)(int16_t)rawA[2];
	}
#endif
}
