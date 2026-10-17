#include "hid_report_parser.h"
#include <string.h>

// HID item 类型
#define ITEM_TYPE_MAIN      0
#define ITEM_TYPE_GLOBAL    1
#define ITEM_TYPE_LOCAL     2

// Main items
#define MAIN_INPUT          0x80
#define MAIN_OUTPUT         0x90
#define MAIN_FEATURE        0xB0
#define MAIN_COLLECTION     0xA0
#define MAIN_END_COLLECTION 0xC0

// Global items
#define GLOBAL_USAGE_PAGE       0x04
#define GLOBAL_LOGICAL_MIN      0x14
#define GLOBAL_LOGICAL_MAX      0x24
#define GLOBAL_REPORT_SIZE      0x74
#define GLOBAL_REPORT_ID        0x84
#define GLOBAL_REPORT_COUNT     0x94
#define GLOBAL_PUSH             0xA4
#define GLOBAL_POP              0xB4

// Local items
#define LOCAL_USAGE         0x08
#define LOCAL_USAGE_MIN     0x18
#define LOCAL_USAGE_MAX     0x28

// Usage Pages
#define USAGE_PAGE_GENERIC_DESKTOP  0x01
#define USAGE_PAGE_BUTTON           0x09

// Generic Desktop usages
#define USAGE_X         0x30
#define USAGE_Y         0x31
#define USAGE_Z         0x32
#define USAGE_RX        0x33
#define USAGE_RY        0x34
#define USAGE_RZ        0x35
#define USAGE_HAT       0x39

static uint32_t get_item_data(const uint8_t *p, uint8_t bSize) {
    switch (bSize) {
        case 0: return 0;
        case 1: return p[0];
        case 2: return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
        case 3: return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
        default: return 0;
    }
}

void hid_parse_report_descriptor(const uint8_t *desc, uint16_t len, HidLayout *layout) {
    memset(layout, -1, sizeof(HidLayout));
    layout->btn_count = 0;
    layout->hat_bit_offset = 0;
    layout->axis_bits = 8;
    layout->valid = false;

    uint8_t  report_size = 0;
    uint8_t  report_count = 0;
    uint16_t usage_page = 0;
    uint16_t usage_min = 0;
    uint16_t usage_max = 0;
    uint16_t bit_pos = 0;  // 当前报告中的位偏移

    uint16_t i = 0;
    while (i < len) {
        uint8_t prefix = desc[i];
        uint8_t bSize = prefix & 0x03;
        if (bSize == 3) bSize = 4;  // 0x03 表示 4 字节
        uint8_t bType = (prefix >> 2) & 0x03;
        uint8_t bTag  = (prefix >> 4) & 0x0F;

        const uint8_t *data = &desc[i + 1];
        uint32_t val = get_item_data(data, bSize);

        if (bType == ITEM_TYPE_GLOBAL) {
            switch (bTag) {
                case 0: usage_page = (uint16_t)val; break;
                case 7: report_size = (uint8_t)val; break;
                case 9: report_count = (uint8_t)val; break;
                default: break;
            }
        } else if (bType == ITEM_TYPE_LOCAL) {
            switch (bTag) {
                case 0:  // Usage
                    if (usage_page == USAGE_PAGE_GENERIC_DESKTOP) {
                        uint16_t bytes = bit_pos / 8;
                        uint8_t bits = bit_pos % 8;
                        switch (val) {
                            case USAGE_X:  layout->x_byte_offset = bytes; break;
                            case USAGE_Y:  layout->y_byte_offset = bytes; break;
                            case USAGE_Z:  layout->z_byte_offset = bytes; break;
                            case USAGE_RZ: layout->rz_byte_offset = bytes; break;
                            case USAGE_HAT:
                                layout->hat_byte_offset = bytes;
                                layout->hat_bit_offset = bits;
                                break;
                            default: break;
                        }
                    }
                    break;
                case 1: usage_min = (uint16_t)val; break;  // Usage Minimum
                case 2: usage_max = (uint16_t)val; break;  // Usage Maximum
                default: break;
            }
        } else if (bType == ITEM_TYPE_MAIN) {
            switch (bTag) {
                case 0:  // Input / Output / Feature
                    // 检查是否是数据输入（非常量）
                    if ((bTag << 4) == MAIN_INPUT) {
                        if (usage_page == USAGE_PAGE_BUTTON) {
                            layout->btn_bit_offset = bit_pos;
                            layout->btn_count = report_count;
                        }
                        // 记录轴位宽
                        if (usage_page == USAGE_PAGE_GENERIC_DESKTOP &&
                            report_size >= 8) {
                            layout->axis_bits = report_size;
                        }
                    }
                    bit_pos += (uint16_t)report_size * report_count;
                    break;
                case 2: // Collection
                case 3: // Feature
                case 8: // End Collection
                    break;
                default: break;
            }
            // 清除 local 状态（usage_min/max 在 main item 后重置）
            usage_min = 0;
            usage_max = 0;
        }

        i += 1 + bSize;
    }

    // 有效性：至少有 X/Y 轴
    layout->valid = (layout->x_byte_offset >= 0 && layout->y_byte_offset >= 0);
}
