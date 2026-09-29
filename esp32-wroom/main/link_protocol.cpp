#include "link_protocol.h"

#include <string.h>

uint16_t lp_crc16_update(uint16_t crc, uint8_t b) {
    crc ^= (uint16_t)b << 8;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                             : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint16_t lp_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

void lp_parser_init(lp_parser_t *p,
                    lp_input_cb_t onInput,
                    lp_status_cb_t onStatus,
                    lp_activity_cb_t onActivity) {
    memset(p, 0, sizeof(*p));
    p->onInput = onInput;
    p->onStatus = onStatus;
    p->onActivity = onActivity;
}

static void lp_dispatch(lp_parser_t *p) {
    if (p->type == LP_TYPE_INPUT && p->len == 13) {
        if (p->onInput) {
            lp_input_t in;
            in.buttons = lp_le16(&p->payload[0]);
            in.dpad    = p->payload[2];
            in.lx      = lp_le16(&p->payload[3]);
            in.ly      = lp_le16(&p->payload[5]);
            in.rx      = lp_le16(&p->payload[7]);
            in.ry      = lp_le16(&p->payload[9]);
            in.lt      = p->payload[11];
            in.rt      = p->payload[12];
            p->onInput(&in);
        }
    } else if (p->type == LP_TYPE_STATUS && p->len == 3) {
        if (p->onStatus) {
            lp_status_t st;
            st.socdMode = p->payload[0];
            st.dpadMode = p->payload[1];
            st.inputMode = p->payload[2];
            p->onStatus(&st);
        }
    }
}

void lp_feed(lp_parser_t *p, uint8_t b) {
    // Any byte counts as link activity, valid frame or not.
    if (p->onActivity) p->onActivity();

    switch (p->state) {
        case 0:
            if (b == LP_MAGIC) p->state = 1;
            break;
        case 1:
            p->crcCalc = 0xFFFF;
            p->crcCalc = lp_crc16_update(p->crcCalc, b);
            p->state = (b == LP_VERSION) ? 2 : 0;
            break;
        case 2:
            p->type = b;
            p->crcCalc = lp_crc16_update(p->crcCalc, b);
            p->state = 3;
            break;
        case 3:
            p->len = b;
            p->crcCalc = lp_crc16_update(p->crcCalc, b);
            if (p->len > LP_MAX_PAYLOAD) { p->state = 0; break; }
            p->idx = 0;
            p->state = (p->len == 0) ? 5 : 4;
            break;
        case 4:
            p->payload[p->idx++] = b;
            p->crcCalc = lp_crc16_update(p->crcCalc, b);
            if (p->idx == p->len) p->state = 5;
            break;
        case 5:
            p->crcLo = b;
            p->state = 6;
            break;
        case 6:
            if (b == (uint8_t)(p->crcCalc >> 8) &&
                p->crcLo == (uint8_t)(p->crcCalc & 0xFF)) {
                lp_dispatch(p);
            }
            p->state = 0;
            break;
    }
}

size_t lp_build_link_status(uint8_t *buf, uint8_t bleConnected) {
    buf[0] = LP_MAGIC;
    buf[1] = LP_VERSION;
    buf[2] = LP_TYPE_LINK_STATUS;
    buf[3] = 1;
    buf[4] = bleConnected ? 1 : 0;
    uint16_t crc = 0xFFFF;
    for (int i = 1; i <= 4; i++) crc = lp_crc16_update(crc, buf[i]);
    buf[5] = (uint8_t)(crc & 0xFF);
    buf[6] = (uint8_t)(crc >> 8);
    return 7;
}
