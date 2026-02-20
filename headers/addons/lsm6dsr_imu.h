#ifndef _LSM6DSR_IMU_H
#define _LSM6DSR_IMU_H

#include "gpaddon.h"
#include "BoardConfig.h"
#include "peripheral_spi.h"

#ifndef LSM6DSR_IMU_ENABLED
#define LSM6DSR_IMU_ENABLED 0
#endif

#define LSM6DSR_IMU_ADDON_NAME "LSM6DSR IMU"

// SPI 引脚（RX/SCK/TX/CS）仅从「外设映射」与插件配置（spiBlock、csPin）获取
#define LSM6DSR_SPI_HZ      1500000u

// 供 webconfig 获取调试信息（WHO_AM_I、SPI/IMU 状态）
void getLSM6DSRImuDebug(uint8_t* whoAmI, bool* spiOk, bool* imuOk);
// 供 webconfig 按需读取 6 轴 RAW（网页模式下主循环不跑 addon preprocess，故 API 内做一次 SPI 读取）
bool getLSM6DSRRawData(int16_t gyro[3], int16_t accel[3]);

class LSM6DSRIMUAddon : public GPAddon {
public:
	virtual bool available();
	virtual void setup();
	virtual void preprocess();
	virtual void process() {}
	virtual void postprocess(bool) {}
	virtual std::string name() { return LSM6DSR_IMU_ADDON_NAME; }
	virtual void reinit() {}
private:
	PeripheralSPI* spi_;
	int8_t csPin_;
	bool spiOk_;
	bool imuOk_;
	uint32_t calibCount_;
	int32_t calibSumX_;
	int32_t calibSumY_;
	int32_t calibSumZ_;
};

#endif
