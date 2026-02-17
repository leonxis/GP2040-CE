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
};

#endif
