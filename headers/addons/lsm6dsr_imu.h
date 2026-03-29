#ifndef _LSM6DSR_IMU_H
#define _LSM6DSR_IMU_H

#include "gpaddon.h"
#include "peripheral_spi.h"
#include <cstddef>
#include <cstdint>

#define LSM6DSR_IMU_ADDON_NAME "LSM6DSR IMU"

// SPI 引脚（RX/SCK/TX/CS）仅从「外设映射」与插件配置（spiBlock、csPin）获取
// 与 MCP3208 等共用 SPI 时若速率相同，setup 阶段初始化一次即可；若速率不同需在访问前切换波特率
#define LSM6DSR_SPI_HZ      1500000u

// 供 webconfig 按需读取 6 轴 RAW（已应用校准偏移；网页模式下主循环不跑 addon preprocess，故 API 内做一次 SPI 读取）
bool getLSM6DSRRawData(int16_t gyro[3], int16_t accel[3]);
// 陀螺仪零偏校准：静止采样取平均，写入 offsetX/Y/Z（int32），返回是否成功
bool lsm6dsr_calibrate_gyro(int32_t* offsetX, int32_t* offsetY, int32_t* offsetZ);
// 加速度计零漂校准（alpakka 方法）：静止采样取平均，X/Y 为零偏，Z 为零偏减 1G，返回是否成功
bool lsm6dsr_calibrate_accel(int32_t* offsetX, int32_t* offsetY, int32_t* offsetZ);

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
	void buildEngageMasks();   // 根据 engageKeys 填充 engageButtonMask / engageDpadMask（仅支持上下左右、B1-B4、L1/L2/R1/R2、S1/S2）
	void applyGyroSlewLimit(float teS); // 尖峰滤波：对 calG/filterG 做变化率限制（按 dt 缩放），抑制微动开关震动引起的短时尖峰
	void applyOneEuroFilter(float teS); // 一欧元滤波：低通平滑，alpha = 1/(1+tau/Te)，在尖峰滤波之后应用
	void outputGyroToMouse(Gamepad* gamepad, const int16_t calG[3], float dtS); // 陀螺仪→HID 鼠标（按 dt 缩放）
	void clearMouseOutput(Gamepad* gamepad); // 仅在状态切换时清零鼠标输出
	void clearGyroOutput(Gamepad* gamepad);  // 仅在状态切换时清零陀螺仪/加速度与Switch Pro IMU输出
	PeripheralSPI* spi;
	int8_t csPin;
	int32_t offsetGyroX;
	int32_t offsetGyroY;
	int32_t offsetGyroZ;
	int32_t offsetAccelX;
	int32_t offsetAccelY;
	int32_t offsetAccelZ;
	int outputMode;   // 陀螺仪模拟方式：0=DS4, 3=鼠标（1/2 左/右摇杆已移除，遇则按 DS4 处理）
	int engageMode;   // 生效方式：0=一直生效, 1=按下按键生效, 2=按下按键暂停
	bool spikeFilterEnabled;  // 尖峰滤波开关：true 时对陀螺仪做变化率限制
	bool oneEuroFilterEnabled;  // 一欧元滤波开关：true 时在尖峰滤波后对陀螺仪做低通平滑
	int gyroMouseMapMode;   // 0=XY轴模拟, 1=XZ轴模拟
	int gyroMouseInvert;    // 0=无, 1=反转左右, 2=反转上下, 3=全部反转
	float gyroMouseSensLR;  // 左右灵敏度
	float gyroMouseSensUD;  // 上下灵敏度
	int32_t gyroMouseDeadzone; // 鼠标死区（LSB）
	int32_t engageKeys[16];  // 生效按键（GpioAction 枚举值），与前端体感设置一致
	size_t engageKeysCount;
	// 预解析生效按键为 mask，运行时仅按位判断（同四键触摸板优化）
	uint32_t engageButtonMask[16];
	uint8_t engageDpadMask[16];
	uint32_t engageButtonMaskAny;
	uint8_t engageDpadMaskAny;
	bool hasEngageKeys;
	// preprocess 复用缓冲区，避免每帧栈上分配
	uint8_t readBuf[12];
	int16_t rawG[3];
	int16_t rawA[3];
	int16_t calG[3];
	// 陀螺仪变化率限制（抑制微动开关震动引起的短时尖峰）：上一帧滤波后的角速度 LSB
	int16_t filterG[3];
	// 一欧元滤波内部状态（浮点，每轴一个）
	float oneEuroState[3];
	bool oneEuroInited;
	uint64_t lastSampleUs;
	// 鼠标亚像素累积（用于 1kHz 下的小速度积分）
	float mouseSubX;
	float mouseSubY;
	uint32_t prevButtons;
	uint64_t mouseSuppressUntilUs;
	bool mouseOutputCleared;
	bool gyroOutputCleared;
};

#endif
