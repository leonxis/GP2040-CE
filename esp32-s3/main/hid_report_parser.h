#pragma once
#include <stdint.h>
#include <stdbool.h>

// ===========================================================================
// HID 报告描述符简化解析器
// 提取游戏手柄报告中按钮、hat、摇杆轴的位偏移与位宽
// 用于兼容非 GP2040 的第三方 HID 手柄
// ===========================================================================

typedef struct {
    // 按钮位图：起始位偏移（bit offset from report start），按钮数量
    int16_t btn_bit_offset;
    uint8_t btn_count;
    // Hat switch：字节偏移，位偏移
    int16_t hat_byte_offset;
    uint8_t hat_bit_offset;
    // 摇杆轴：字节偏移，位宽（8 或 16），-1 表示不存在
    int16_t x_byte_offset;
    int16_t y_byte_offset;
    int16_t z_byte_offset;     // L2 / 左扳机
    int16_t rz_byte_offset;    // R2 / 右扳机
    uint8_t axis_bits;         // 轴位宽（8 或 16）
    bool valid;                // 解析是否成功（至少找到 X/Y 轴）
} HidLayout;

// 解析 HID 报告描述符，填充 layout
void hid_parse_report_descriptor(const uint8_t *desc, uint16_t len, HidLayout *layout);
