#ifndef _LSM6DSR_IMU_H
#define _LSM6DSR_IMU_H

#include "gpaddon.h"
#include "peripheral_spi.h"

#define LSM6DSR_IMU_ADDON_NAME "LSM6DSR IMU"

// SPI 引脚（RX/SCK/TX/CS）仅从「外设映射」与插件配置（spiBlock、csPin）获取
// 与 MCP3208 等共用 SPI 时若速率不同：改此处即可（如 5MHz 用 5000000u），每次访问已包 begin/endTransaction
#define LSM6DSR_SPI_HZ      1500000u

// 供 webconfig 按需读取 6 轴 RAW（已应用校准偏移；网页模式下主循环不跑 addon preprocess，故 API 内做一次 SPI 读取）
bool getLSM6DSRRawData(int16_t gyro[3], int16_t accel[3]);
// 陀螺仪零偏校准：静止采样取平均，写入 offsetX/Y/Z（int32），返回是否成功
bool lsm6dsr_calibrate_gyro(int32_t* offsetX, int32_t* offsetY, int32_t* offsetZ);

class LSM6DSRIMUAddon : public GPAddon {
public:
	virtual bool available();
	virtual void setup();
	virtual void preprocess();
	virtual void process() {}
	virtual void postprocess(bool) {}
	virtual std::string name() { return LSM6DSR_IMU_ADDON_NAME; }
	virtual void reinit();
private:
	PeripheralSPI* spi_;
	int8_t csPin_;
	int32_t offsetGyroX_;
	int32_t offsetGyroY_;
	int32_t offsetGyroZ_;
};

#endif
