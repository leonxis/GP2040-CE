#include "report_parser.h"
#include "gamepad_types.h"

// ===========================================================================
// hat 值 → dpad 位掩码
// 0=上, 1=右上, 2=右, 3=右下, 4=下, 5=左下, 6=左, 7=左上, 8/0x0F=居中
// ===========================================================================
uint8_t hat_to_dpad(uint8_t hat) {
    switch (hat & 0x0F) {
        case 0:  return GAMEPAD_MASK_UP;
        case 1:  return GAMEPAD_MASK_UP | GAMEPAD_MASK_RIGHT;
        case 2:  return GAMEPAD_MASK_RIGHT;
        case 3:  return GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT;
        case 4:  return GAMEPAD_MASK_DOWN;
        case 5:  return GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT;
        case 6:  return GAMEPAD_MASK_LEFT;
        case 7:  return GAMEPAD_MASK_UP | GAMEPAD_MASK_LEFT;
        default: return 0;  // 居中
    }
}

// ===========================================================================
// Switch 模式（HORI POKKEN 格式，8 字节，无 report ID）
// bytes[0..1]=buttons LE, [2]=hat, [3..6]=lx/ly/rx/ry, [7]=vendor
// ===========================================================================
void parse_switch_report(const uint8_t *buf, uint16_t len, GamepadState *out) {
    if (len < 8) return;
    uint16_t sw = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);

    out->buttons = 0;
    // bit0=Y→B3, bit1=B→B1, bit2=A→B2, bit3=X→B4
    if (sw & (1 << 0))  out->buttons |= GAMEPAD_MASK_B3;
    if (sw & (1 << 1))  out->buttons |= GAMEPAD_MASK_B1;
    if (sw & (1 << 2))  out->buttons |= GAMEPAD_MASK_B2;
    if (sw & (1 << 3))  out->buttons |= GAMEPAD_MASK_B4;
    // bit4=L→L1, bit5=R→R1, bit6=ZL→L2, bit7=ZR→R2
    if (sw & (1 << 4))  out->buttons |= GAMEPAD_MASK_L1;
    if (sw & (1 << 5))  out->buttons |= GAMEPAD_MASK_R1;
    if (sw & (1 << 6))  out->buttons |= GAMEPAD_MASK_L2;
    if (sw & (1 << 7))  out->buttons |= GAMEPAD_MASK_R2;
    // bit8=MINUS→S1, bit9=PLUS→S2, bit10=L3, bit11=R3
    if (sw & (1 << 8))  out->buttons |= GAMEPAD_MASK_S1;
    if (sw & (1 << 9))  out->buttons |= GAMEPAD_MASK_S2;
    if (sw & (1 << 10)) out->buttons |= GAMEPAD_MASK_L3;
    if (sw & (1 << 11)) out->buttons |= GAMEPAD_MASK_R3;
    // bit12=HOME→A1, bit13=CAPTURE→A2
    if (sw & (1 << 12)) out->buttons |= GAMEPAD_MASK_A1;
    if (sw & (1 << 13)) out->buttons |= GAMEPAD_MASK_A2;

    out->dpad = hat_to_dpad(buf[2]);
    out->lx = (uint16_t)buf[3] << 8;
    out->ly = (uint16_t)buf[4] << 8;
    out->rx = (uint16_t)buf[5] << 8;
    out->ry = (uint16_t)buf[6] << 8;
    // Switch 模式无独立扳机，ZL/ZR 是数字按键，lt/rt 置 0
    out->lt = 0;
    out->rt = 0;
}

// ===========================================================================
// PS4 / DS4 模式（64 字节，report ID=0x01）
// [0]=id, [1..4]=lx/ly/rx/ry, [5]=dpad+W/S/E/N, [6]=L1/R1/L2/R2/Sel/Start/L3/R3,
// [7]=Home/Touchpad/counter, [8]=lt, [9]=rt
// ===========================================================================
void parse_ps4_report(const uint8_t *buf, uint16_t len, GamepadState *out) {
    if (len < 10) return;
    if (buf[0] != 0x01) return;  // report ID 必须为 1

    out->buttons = 0;
    uint8_t b5 = buf[5];
    uint8_t b6 = buf[6];
    uint8_t b7 = buf[7];

    // byte5: bit4=W(Square)→B3, bit5=S(Cross)→B1, bit6=E(Circle)→B2, bit7=N(Tri)→B4
    if (b5 & 0x10) out->buttons |= GAMEPAD_MASK_B3;
    if (b5 & 0x20) out->buttons |= GAMEPAD_MASK_B1;
    if (b5 & 0x40) out->buttons |= GAMEPAD_MASK_B2;
    if (b5 & 0x80) out->buttons |= GAMEPAD_MASK_B4;

    // byte6: bit0=L1, bit1=R1, bit2=L2, bit3=R2, bit4=Select→S1, bit5=Start→S2,
    //        bit6=L3, bit7=R3
    if (b6 & 0x01) out->buttons |= GAMEPAD_MASK_L1;
    if (b6 & 0x02) out->buttons |= GAMEPAD_MASK_R1;
    if (b6 & 0x04) out->buttons |= GAMEPAD_MASK_L2;
    if (b6 & 0x08) out->buttons |= GAMEPAD_MASK_R2;
    if (b6 & 0x10) out->buttons |= GAMEPAD_MASK_S1;
    if (b6 & 0x20) out->buttons |= GAMEPAD_MASK_S2;
    if (b6 & 0x40) out->buttons |= GAMEPAD_MASK_L3;
    if (b6 & 0x80) out->buttons |= GAMEPAD_MASK_R3;

    // byte7: bit0=Home→A1, bit1=Touchpad→A2
    if (b7 & 0x01) out->buttons |= GAMEPAD_MASK_A1;
    if (b7 & 0x02) out->buttons |= GAMEPAD_MASK_A2;

    out->dpad = hat_to_dpad(b5 & 0x0F);
    out->lx = (uint16_t)buf[1] << 8;
    out->ly = (uint16_t)buf[2] << 8;
    out->rx = (uint16_t)buf[3] << 8;
    out->ry = (uint16_t)buf[4] << 8;
    out->lt = buf[8];
    out->rt = buf[9];
}

// ===========================================================================
// XInput / Xbox 360 模式（20 字节，无 report ID）
// [0]=0, [1]=0x14, [2]=buttons1, [3]=buttons2, [4]=lt, [5]=rt,
// [6..7]=lx(int16 LE), [8..9]=ly, [10..11]=rx, [12..13]=ry
// ===========================================================================
void parse_xinput_report(const uint8_t *buf, uint16_t len, GamepadState *out) {
    if (len < 14) return;

    uint8_t b1 = buf[2];
    uint8_t b2 = buf[3];

    out->buttons = 0;
    out->dpad = 0;

    // buttons1: bit0=UP,1=DOWN,2=LEFT,3=RIGHT,4=START→S2,5=BACK→S1,6=L3,7=R3
    if (b1 & 0x01) out->dpad |= GAMEPAD_MASK_UP;
    if (b1 & 0x02) out->dpad |= GAMEPAD_MASK_DOWN;
    if (b1 & 0x04) out->dpad |= GAMEPAD_MASK_LEFT;
    if (b1 & 0x08) out->dpad |= GAMEPAD_MASK_RIGHT;
    if (b1 & 0x10) out->buttons |= GAMEPAD_MASK_S2;
    if (b1 & 0x20) out->buttons |= GAMEPAD_MASK_S1;
    if (b1 & 0x40) out->buttons |= GAMEPAD_MASK_L3;
    if (b1 & 0x80) out->buttons |= GAMEPAD_MASK_R3;

    // buttons2: bit0=LB→L1, bit1=RB→R1, bit2=HOME→A1, bit4=A→B1,
    //           bit5=B→B2, bit6=X→B3, bit7=Y→B4
    if (b2 & 0x01) out->buttons |= GAMEPAD_MASK_L1;
    if (b2 & 0x02) out->buttons |= GAMEPAD_MASK_R1;
    if (b2 & 0x04) out->buttons |= GAMEPAD_MASK_A1;
    if (b2 & 0x10) out->buttons |= GAMEPAD_MASK_B1;
    if (b2 & 0x20) out->buttons |= GAMEPAD_MASK_B2;
    if (b2 & 0x40) out->buttons |= GAMEPAD_MASK_B3;
    if (b2 & 0x80) out->buttons |= GAMEPAD_MASK_B4;

    out->lt = buf[4];
    out->rt = buf[5];

    // 摇杆 int16 LE → uint16（+32768 偏移）
    int16_t lx = (int16_t)((uint16_t)buf[6] | ((uint16_t)buf[7] << 8));
    int16_t ly = (int16_t)((uint16_t)buf[8] | ((uint16_t)buf[9] << 8));
    int16_t rx = (int16_t)((uint16_t)buf[10] | ((uint16_t)buf[11] << 8));
    int16_t ry = (int16_t)((uint16_t)buf[12] | ((uint16_t)buf[13] << 8));
    out->lx = (uint16_t)((int32_t)lx + 32768);
    out->ly = (uint16_t)((int32_t)ly + 32768);
    out->rx = (uint16_t)((int32_t)rx + 32768);
    out->ry = (uint16_t)((int32_t)ry + 32768);
}

// ===========================================================================
// 通用 HID 解析（基于描述符解析结果的动态偏移）
// ===========================================================================
void parse_generic_hid(const uint8_t *buf, uint16_t len, const HidLayout *layout,
                       GamepadState *out) {
    if (!layout->valid || len == 0) return;

    out->buttons = 0;
    out->dpad = 0;
    out->lt = 0;
    out->rt = 0;

    // 按钮位图（最多取 16 个按钮，按 1:1 映射到 GAMEPAD_MASK_*）
    if (layout->btn_bit_offset >= 0 && layout->btn_count > 0) {
        uint8_t cnt = layout->btn_count > 16 ? 16 : layout->btn_count;
        for (uint8_t i = 0; i < cnt; i++) {
            uint16_t bit_pos = layout->btn_bit_offset + i;
            uint16_t byte_idx = bit_pos / 8;
            uint8_t bit_idx = bit_pos % 8;
            if (byte_idx < len && (buf[byte_idx] & (1 << bit_idx))) {
                out->buttons |= (1U << i);
            }
        }
    }

    // Hat switch
    if (layout->hat_byte_offset >= 0 && layout->hat_byte_offset < len) {
        uint8_t hat = (buf[layout->hat_byte_offset] >> layout->hat_bit_offset) & 0x0F;
        out->dpad = hat_to_dpad(hat);
    }

    // 摇杆轴（8 位或 16 位）
    uint16_t mid = (layout->axis_bits == 16) ? 0x8000 : 0x80;
    if (layout->x_byte_offset >= 0 && layout->x_byte_offset < len) {
        if (layout->axis_bits == 16 && layout->x_byte_offset + 1 < len) {
            uint16_t v = (uint16_t)buf[layout->x_byte_offset] |
                         ((uint16_t)buf[layout->x_byte_offset + 1] << 8);
            out->lx = v;
        } else {
            out->lx = (uint16_t)buf[layout->x_byte_offset] << 8;
        }
    } else {
        out->lx = mid;
    }
    if (layout->y_byte_offset >= 0 && layout->y_byte_offset < len) {
        if (layout->axis_bits == 16 && layout->y_byte_offset + 1 < len) {
            uint16_t v = (uint16_t)buf[layout->y_byte_offset] |
                         ((uint16_t)buf[layout->y_byte_offset + 1] << 8);
            out->ly = v;
        } else {
            out->ly = (uint16_t)buf[layout->y_byte_offset] << 8;
        }
    } else {
        out->ly = mid;
    }
    if (layout->z_byte_offset >= 0 && layout->z_byte_offset < len) {
        if (layout->axis_bits == 16 && layout->z_byte_offset + 1 < len) {
            uint16_t v = (uint16_t)buf[layout->z_byte_offset] |
                         ((uint16_t)buf[layout->z_byte_offset + 1] << 8);
            out->rx = v;
        } else {
            out->rx = (uint16_t)buf[layout->z_byte_offset] << 8;
        }
    } else {
        out->rx = mid;
    }
    if (layout->rz_byte_offset >= 0 && layout->rz_byte_offset < len) {
        if (layout->axis_bits == 16 && layout->rz_byte_offset + 1 < len) {
            uint16_t v = (uint16_t)buf[layout->rz_byte_offset] |
                         ((uint16_t)buf[layout->rz_byte_offset + 1] << 8);
            out->ry = v;
        } else {
            out->ry = (uint16_t)buf[layout->rz_byte_offset] << 8;
        }
    } else {
        out->ry = mid;
    }
}
