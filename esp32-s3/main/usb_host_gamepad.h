#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "report_parser.h"

// ===========================================================================
// USB Host 手柄枚举与输入采集
// 三任务双核架构：
//   Core1: usb_host_task (事件循环 + URB 回调，仅 memcpy 原始包入队)
//   Core0: parse_task    (从队列取原始包 → 解析 → 写 g_state)
//   Core0: nrf24_fwd     (1ms 恒流读 g_state 打包发送)
// ===========================================================================

// 初始化 USB Host 库（含创建原始报告队列与 parse_task）
void usb_gamepad_init(void);

// 获取最新手柄状态（线程安全）
void usb_gamepad_get_state(GamepadState *out);

// 获取当前识别到的手柄模式
GamepadMode usb_gamepad_get_mode(void);

// 手柄是否已连接（已成功枚举为可用手柄）
bool usb_gamepad_connected(void);

// 是否检测到 USB 设备（已枚举但可能无法识别为手柄）
bool usb_gamepad_device_present(void);

// 当前识别到的模式是否为已知可用模式
bool usb_gamepad_mode_recognized(void);
