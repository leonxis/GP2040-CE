#include "addons/lsm6dsr_imu.h"
#include "config.pb.h"
#include "storagemanager.h"
#include "peripheralmanager.h"
#include "gamepad/GamepadState.h"
#include "gamepad.h"
#include "BoardConfig.h"
#include "eventmanager.h"
#include "GPEvent.h"
#include "GPStorageSaveEvent.h"

// LSM6DSR register map (alpakka / project convention; adjust per datasheet if needed)
#define LSM6DSR_WHO_AM_I   0x0F
#define LSM6DSR_CTRL1_XL   0x10
#define LSM6DSR_CTRL2_G    0x11
#define LSM6DSR_CTRL8_XL   0x17
#define LSM6DSR_OUTX_L_G   0x22
#define LSM6DSR_READ       0x80

#define LSM6DSR_CTRL1_XL_OFF  0x00
#define LSM6DSR_CTRL1_XL_2G   0xA2
#define LSM6DSR_CTRL8_XL_LP  0x00
#define LSM6DSR_CTRL2_G_OFF  0x00
#define LSM6DSR_CTRL2_G_500  0xA4

static const uint32_t LSM6DSR_CALIB_SAMPLES = 100;

enum LSM6DSROutputMode : int32_t {
    LSM6DSR_OUTPUT_DS4 = 0,
    LSM6DSR_OUTPUT_RIGHT_STICK = 1,
    LSM6DSR_OUTPUT_MOUSE = 2,
};

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

#if LSM6DSR_IMU_ENABLED
    const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
    if (!opts.enabled) return;
    if (!opts.has_csPin) return;
    csPin_ = (int8_t)opts.csPin;
    uint8_t block = opts.has_spiBlock ? (uint8_t)opts.spiBlock : 0;
    PeripheralSPI* spi = PeripheralManager::getInstance().getSPI(block);
    if (!spi || !spi->configured) return;
    spi_ = spi;

    gpio_init((uint)csPin_);
    gpio_set_dir((uint)csPin_, GPIO_OUT);
    gpio_put((uint)csPin_, true);
    spiOk_ = true;

    // 1.5 MHz transaction for LSM6DSR (mode 3 per alpakka/typical IMU)
    spi_->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE3);
    spi_->select(csPin_);
    uint8_t id = spi_->transfer(LSM6DSR_READ | LSM6DSR_WHO_AM_I);
    (void)spi_->transfer(0);
    spi_->deselect();
    spi_->endTransaction();

    if (id != 0x6A) {
        spiOk_ = false;
        return;
    }

    spi_->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE3);
    spi_->select(csPin_);
    spi_->transfer(LSM6DSR_CTRL1_XL);
    spi_->transfer(LSM6DSR_CTRL1_XL_2G);
    spi_->deselect();
    spi_->select(csPin_);
    spi_->transfer(LSM6DSR_CTRL8_XL);
    spi_->transfer(LSM6DSR_CTRL8_XL_LP);
    spi_->deselect();
    spi_->select(csPin_);
    spi_->transfer(LSM6DSR_CTRL2_G);
    spi_->transfer(LSM6DSR_CTRL2_G_500);
    spi_->deselect();
    spi_->endTransaction();
    imuOk_ = true;
#endif
}

void LSM6DSRIMUAddon::preprocess() {
#if LSM6DSR_IMU_ENABLED
    if (!spiOk_ || !spi_ || !imuOk_) return;

    AddonOptions& addonOpts = Storage::getInstance().getAddonOptions();
    LSM6DSROptions& opts = addonOpts.lsm6dsrOptions;

    if (opts.has_calibrateGyroRequested && opts.calibrateGyroRequested) {
        if (calibCount_ == 0) {
            calibSumX_ = calibSumY_ = calibSumZ_ = 0;
        }
        uint8_t buf[6];
        spi_->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE3);
        spi_->select(csPin_);
        spi_->transfer(LSM6DSR_READ | LSM6DSR_OUTX_L_G);
        for (int i = 0; i < 6; i++)
            buf[i] = spi_->transfer(0);
        spi_->deselect();
        spi_->endTransaction();
        int16_t rawX = (int16_t)((uint16_t)buf[1] << 8 | buf[0]);
        int16_t rawY = (int16_t)((uint16_t)buf[3] << 8 | buf[2]);
        int16_t rawZ = (int16_t)((uint16_t)buf[5] << 8 | buf[4]);
        rawX = (int16_t)-rawX;
        calibSumX_ += rawX;
        calibSumY_ += rawY;
        calibSumZ_ += rawZ;
        calibCount_++;
        if (calibCount_ >= LSM6DSR_CALIB_SAMPLES) {
            opts.offsetGyroX = (int32_t)(calibSumX_ / (int32_t)LSM6DSR_CALIB_SAMPLES);
            opts.offsetGyroY = (int32_t)(calibSumY_ / (int32_t)LSM6DSR_CALIB_SAMPLES);
            opts.offsetGyroZ = (int32_t)(calibSumZ_ / (int32_t)LSM6DSR_CALIB_SAMPLES);
            opts.has_offsetGyroX = opts.has_offsetGyroY = opts.has_offsetGyroZ = true;
            opts.calibrateGyroRequested = false;
            opts.has_calibrateGyroRequested = true;
            calibCount_ = 0;
            EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));
        }
        return;
    }

    int32_t outMode = opts.has_outputMode ? opts.outputMode : 0;
    if (outMode != LSM6DSR_OUTPUT_DS4) return;

    int32_t offX = opts.has_offsetGyroX ? opts.offsetGyroX : 0;
    int32_t offY = opts.has_offsetGyroY ? opts.offsetGyroY : 0;
    int32_t offZ = opts.has_offsetGyroZ ? opts.offsetGyroZ : 0;

    uint8_t buf[6];
    spi_->beginTransaction(LSM6DSR_SPI_HZ, SPI_MSB_FIRST, SPI_MODE3);
    spi_->select(csPin_);
    spi_->transfer(LSM6DSR_READ | LSM6DSR_OUTX_L_G);
    for (int i = 0; i < 6; i++)
        buf[i] = spi_->transfer(0);
    spi_->deselect();
    spi_->endTransaction();

    int16_t rawX = (int16_t)((uint16_t)buf[1] << 8 | buf[0]);
    int16_t rawY = (int16_t)((uint16_t)buf[3] << 8 | buf[2]);
    int16_t rawZ = (int16_t)((uint16_t)buf[5] << 8 | buf[4]);
    rawX = (int16_t)-rawX;
    int32_t gx = (int32_t)rawX - offX;
    int32_t gy = (int32_t)rawY - offY;
    int32_t gz = (int32_t)rawZ - offZ;
    gx = (gx < -32768) ? -32768 : ((gx > 32767) ? 32767 : gx);
    gy = (gy < -32768) ? -32768 : ((gy > 32767) ? 32767 : gy);
    gz = (gz < -32768) ? -32768 : ((gz > 32767) ? 32767 : gz);

    Gamepad* gamepad = Storage::getInstance().GetProcessedGamepad();
    gamepad->auxState.sensors.gyroscope.enabled = true;
    gamepad->auxState.sensors.gyroscope.active = true;
    gamepad->auxState.sensors.gyroscope.x = (uint16_t)(int16_t)gx;
    gamepad->auxState.sensors.gyroscope.y = (uint16_t)(int16_t)gy;
    gamepad->auxState.sensors.gyroscope.z = (uint16_t)(int16_t)gz;
#endif
}
