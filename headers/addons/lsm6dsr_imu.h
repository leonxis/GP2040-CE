#ifndef _LSM6DSR_IMU_H
#define _LSM6DSR_IMU_H

#include "gpaddon.h"
#include "BoardConfig.h"

#ifndef LSM6DSR_IMU_ENABLED
#define LSM6DSR_IMU_ENABLED 0
#endif

#define LSM6DSR_IMU_ADDON_NAME "LSM6DSR IMU"

// 共用 SPI0：GPIO0=RX(MISO), GPIO2=SCK, GPIO3=TX(MOSI)；LSM6DSRTR CS=GPIO4；SPI 1.5MHz Mode0
// 作为摇杆补充，优先级低于 MCP3208；ODR 1.66kHz、高性能模式，量程/BDU/滤波/FIFO 后续配置
#define LSM6DSR_SPI_RX_PIN  0
#define LSM6DSR_SPI_SCK_PIN 2
#define LSM6DSR_SPI_TX_PIN  3
#define LSM6DSR_CS_PIN      4
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
	// 功能代码留空：加速度计/陀螺仪数据，ODR=1.66kHz；量程、BDU、数字滤波、FIFO 后续调整
	bool spiOk_;
};

#endif
