#include "addons/lsm6dsr_imu.h"
#include "config.pb.h"
#include "storagemanager.h"
#include "peripheralmanager.h"
#include "gamepad.h"

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
	spiOk_ = false;
	spi_ = nullptr;
	csPin_ = -1;

#if LSM6DSR_IMU_ENABLED
	const LSM6DSROptions& opts = Storage::getInstance().getAddonOptions().lsm6dsrOptions;
	if (!opts.enabled) return;
	if (!opts.has_csPin) return;
	csPin_ = (int8_t)opts.csPin;
	uint8_t block = opts.has_spiBlock ? (uint8_t)opts.spiBlock : 0;
	PeripheralSPI* spi = PeripheralManager::getInstance().getSPI(block);
	if (!spi || !spi->configured) return;
	spi_ = spi;
	spiOk_ = true;
	// TODO: 配置 ODR=1.66kHz、高性能模式；量程/BDU/滤波/FIFO 后续调整
#endif
}

void LSM6DSRIMUAddon::preprocess() {
	if (!spiOk_ || !spi_)
		return;
	// TODO: 拉低 CS(csPin_)，读 LSM6DSR 寄存器，拉高 CS；数据供摇杆补充
}
