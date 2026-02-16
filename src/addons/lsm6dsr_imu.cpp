#include "addons/lsm6dsr_imu.h"
#include "storagemanager.h"
#include "gamepad.h"

bool LSM6DSRIMUAddon::available() {
#if LSM6DSR_IMU_ENABLED
	return true;
#else
	return false;
#endif
}

void LSM6DSRIMUAddon::setup() {
	// TODO: 初始化 SPI0（与 MCP3208 共用）、GPIO4 为 CS；配置 ODR=1.66kHz、高性能模式；量程/BDU/滤波/FIFO 留空
	spiOk_ = false;
}

void LSM6DSRIMUAddon::preprocess() {
	// TODO: 在 MCP3208 读取之后读取；拉低 CS(GPIO4)，读 LSM6DSR 寄存器，拉高 CS；数据供摇杆补充
	if (!spiOk_)
		return;
}
