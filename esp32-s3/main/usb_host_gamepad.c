#include "usb_host_gamepad.h"
#include "board_config.h"
#include "gamepad_types.h"
#include "hid_report_parser.h"

#include "usb/usb_host.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "usb_host";

// 全局手柄状态（parse_task 写，nrf24_fwd 读）
static GamepadState g_state = {0};
static GamepadMode g_mode = GP_MODE_UNKNOWN;
static volatile bool g_connected = false;
static SemaphoreHandle_t g_state_mutex = NULL;

// USB Host 上下文
static usb_host_client_handle_t g_client_hdl = NULL;
static usb_device_handle_t g_dev_hdl = NULL;
static uint8_t g_interface_num = 0;
static usb_ep_desc_t g_in_ep_desc = {0};
static HidLayout g_hid_layout = {0};
static bool g_use_generic_hid = false;
static SemaphoreHandle_t g_ctrl_sem = NULL;

// 原始报告队列（Core1 URB 回调 → Core0 parse_task）
#define RAW_QUEUE_LEN 4
typedef struct {
    uint8_t data[64];
    uint16_t len;
} RawReport;
static QueueHandle_t g_raw_queue = NULL;

// VID/PID 快速匹配
static bool match_known_vid_pid(uint16_t vid, uint16_t pid, GamepadMode *mode) {
    if (vid == 0x054C && pid == 0x09CC) { *mode = GP_MODE_PS4; return true; }
    if (vid == 0x0F0D && pid == 0x0092) { *mode = GP_MODE_SWITCH; return true; }
    if (vid == 0x045E && pid == 0x028E) { *mode = GP_MODE_XINPUT; return true; }
    return false;
}

// 从配置描述符中查找目标接口
static bool find_target_interface(const uint8_t *cfg_desc, uint16_t len,
                                  uint8_t *iface_num,
                                  usb_ep_desc_t *in_ep,
                                  bool *is_xinput) {
    uint16_t offset = 0;
    while (offset + 2 <= len) {
        uint8_t desc_len = cfg_desc[offset];
        uint8_t desc_type = cfg_desc[offset + 1];
        if (desc_len == 0) break;

        if (desc_type == USB_W_VALUE_DT_INTERFACE && offset + 9 <= len) {
            const usb_intf_desc_t *iface = (const usb_intf_desc_t *)&cfg_desc[offset];
            bool target = false;
            bool xinput = false;

            if (iface->bInterfaceClass == 0xFF &&
                iface->bInterfaceSubClass == 0x5D &&
                iface->bInterfaceProtocol == 0x01) {
                target = true;
                xinput = true;
            } else if (iface->bInterfaceClass == 0x03) {
                target = true;
            }

            if (target) {
                *iface_num = iface->bInterfaceNumber;
                *is_xinput = xinput;
                uint16_t ep_offset = offset + desc_len;
                while (ep_offset + 2 <= len) {
                    uint8_t el = cfg_desc[ep_offset];
                    uint8_t et = cfg_desc[ep_offset + 1];
                    if (el == 0) break;
                    if (et == USB_W_VALUE_DT_INTERFACE) break;
                    if (et == USB_W_VALUE_DT_ENDPOINT && ep_offset + 7 <= len) {
                        const usb_ep_desc_t *ep = (const usb_ep_desc_t *)&cfg_desc[ep_offset];
                        if (ep->bEndpointAddress & USB_BM_REQUEST_TYPE_DIR_IN) {
                            memcpy(in_ep, ep, sizeof(usb_ep_desc_t));
                            return true;
                        }
                    }
                    ep_offset += el;
                }
                return true;
            }
        }
        offset += desc_len;
    }
    return false;
}

// 控制传输回调（信号量通知完成）
static void ctrl_xfer_cb(usb_transfer_t *transfer) {
    xSemaphoreGive(g_ctrl_sem);
}

// 同步控制传输：获取 HID 报告描述符
static bool get_hid_report_descriptor(usb_device_handle_t dev, uint8_t iface,
                                      uint8_t *buf, uint16_t buf_len, uint16_t *out_len) {
    usb_transfer_t *xfer = NULL;
    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + 256, 0, &xfer) != ESP_OK) {
        return false;
    }
    usb_setup_packet_t *setup = (usb_setup_packet_t *)xfer->data_buffer;
    setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_STANDARD |
                           USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
    setup->bRequest = USB_B_REQUEST_GET_DESCRIPTOR;
    setup->wValue = (0x22 << 8);  // HID Report descriptor type
    setup->wIndex = iface;
    setup->wLength = 256;
    xfer->device_handle = dev;
    xfer->bEndpointAddress = 0;
    xfer->callback = ctrl_xfer_cb;
    xfer->num_bytes = sizeof(usb_setup_packet_t) + 256;

    if (usb_host_transfer_submit_control(g_client_hdl, xfer) != ESP_OK) {
        usb_host_transfer_free(xfer);
        return false;
    }
    if (xSemaphoreTake(g_ctrl_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
        usb_host_transfer_free(xfer);
        return false;
    }
    if (xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        usb_host_transfer_free(xfer);
        return false;
    }
    uint16_t recv = xfer->actual_num_bytes - sizeof(usb_setup_packet_t);
    if (recv > buf_len) recv = buf_len;
    memcpy(buf, (uint8_t *)xfer->data_buffer + sizeof(usb_setup_packet_t), recv);
    *out_len = recv;
    usb_host_transfer_free(xfer);
    return recv > 0;
}

// IN 端点传输完成回调（运行在 Core1 usb_host_task 上下文）
// 仅做：拷贝原始报告入队 + 重新提交 URB。不做任何解析，保证 URB 重提交延迟恒定。
static void in_xfer_cb(usb_transfer_t *transfer) {
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        RawReport rr;
        rr.len = transfer->actual_num_bytes > sizeof(rr.data) ? sizeof(rr.data) : transfer->actual_num_bytes;
        memcpy(rr.data, transfer->data_buffer, rr.len);
        // 非阻塞入队：队列满则丢弃旧帧（恒流场景下旧帧无价值）
        if (xQueueSend(g_raw_queue, &rr, 0) != pdTRUE) {
            // 队列满，丢弃（不重提交也不影响，下帧会来）
        }
    } else {
        // 传输错误，清除端点 halt
        usb_host_endpoint_clear(transfer->device_handle, transfer->bEndpointAddress);
    }
    // 重新提交 URB 持续轮询
    usb_host_transfer_submit(transfer);
}

// 配置设备
static esp_err_t configure_device(usb_device_handle_t dev) {
    const usb_device_desc_t *dev_desc = NULL;
    ESP_RETURN_ON_ERROR(usb_host_get_device_descriptor(dev, &dev_desc), TAG, "get_dev_desc");
    uint16_t vid = dev_desc->idVendor;
    uint16_t pid = dev_desc->idProduct;
    ESP_LOGI(TAG, "Device VID:0x%04X PID:0x%04X", vid, pid);

    GamepadMode mode = GP_MODE_UNKNOWN;
    bool known = match_known_vid_pid(vid, pid, &mode);
    g_use_generic_hid = false;

    const usb_config_desc_t *cfg_desc = NULL;
    ESP_RETURN_ON_ERROR(usb_host_get_active_config_descriptor(dev, &cfg_desc), TAG, "get_cfg");

    uint8_t iface_num = 0;
    usb_ep_desc_t in_ep = {0};
    bool is_xinput = false;

    if (!find_target_interface((const uint8_t *)cfg_desc, cfg_desc->wTotalLength,
                               &iface_num, &in_ep, &is_xinput)) {
        ESP_LOGE(TAG, "No target interface found");
        return ESP_FAIL;
    }

    if (!known) {
        if (is_xinput) {
            mode = GP_MODE_XINPUT;
        } else {
            mode = GP_MODE_SWITCH;
            g_use_generic_hid = true;
        }
    } else if (is_xinput) {
        mode = GP_MODE_XINPUT;
    }

    g_mode = mode;
    g_interface_num = iface_num;
    memcpy(&g_in_ep_desc, &in_ep, sizeof(in_ep));

    ESP_RETURN_ON_ERROR(usb_host_interface_claim(g_client_hdl, dev, iface_num, 0), TAG, "claim iface");

    if (g_use_generic_hid || mode == GP_MODE_SWITCH) {
        uint8_t rep_desc[256];
        uint16_t rep_len = 0;
        if (get_hid_report_descriptor(dev, iface_num, rep_desc, sizeof(rep_desc), &rep_len)) {
            ESP_LOGI(TAG, "HID report descriptor len=%d", rep_len);
            hid_parse_report_descriptor(rep_desc, rep_len, &g_hid_layout);
            if (!g_hid_layout.valid) {
                ESP_LOGW(TAG, "HID layout parse failed, fallback to hardcoded");
                g_use_generic_hid = false;
            }
        }
    }

    usb_transfer_t *xfer = NULL;
    ESP_RETURN_ON_ERROR(usb_host_transfer_alloc(in_ep.wMaxPacketSize, 0, &xfer), TAG, "alloc xfer");
    xfer->device_handle = dev;
    xfer->bEndpointAddress = in_ep.bEndpointAddress;
    xfer->callback = in_xfer_cb;
    xfer->num_bytes = in_ep.wMaxPacketSize;

    ESP_RETURN_ON_ERROR(usb_host_transfer_submit(xfer), TAG, "submit xfer");

    g_connected = true;
    ESP_LOGI(TAG, "Gamepad connected: mode=%d xinput=%d", mode, is_xinput);
    return ESP_OK;
}

static void release_device(void) {
    if (g_dev_hdl) {
        usb_host_interface_release(g_client_hdl, g_dev_hdl, g_interface_num);
        usb_host_device_close(g_client_hdl, g_dev_hdl);
        g_dev_hdl = NULL;
    }
    g_connected = false;
    g_mode = GP_MODE_UNKNOWN;
}

// USB Host 客户端事件回调
static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg) {
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV: {
            ESP_LOGI(TAG, "New device: addr=%d", event_msg->new_dev.address);
            usb_device_handle_t dev = NULL;
            if (usb_host_device_open(g_client_hdl, event_msg->new_dev.address, &dev) == ESP_OK) {
                g_dev_hdl = dev;
                configure_device(dev);
            }
            break;
        }
        case USB_HOST_CLIENT_EVENT_DEV_GONE: {
            ESP_LOGI(TAG, "Device gone");
            release_device();
            break;
        }
        default:
            break;
    }
}

// USB Host 事件处理任务（Core1）
static void usb_host_task(void *arg) {
    while (1) {
        usb_host_client_handle_events(g_client_hdl, pdMS_TO_TICKS(10));
    }
}

// 解析任务（Core0）：从原始报告队列取数据，按当前模式解析，写 g_state
static void parse_task(void *arg) {
    RawReport rr;
    while (1) {
        if (xQueueReceive(g_raw_queue, &rr, portMAX_DELAY) == pdTRUE) {
            GamepadState tmp = {0};
            GamepadMode mode = g_mode;

            switch (mode) {
                case GP_MODE_SWITCH:
                    if (g_use_generic_hid) {
                        parse_generic_hid(rr.data, rr.len, &g_hid_layout, &tmp);
                    } else {
                        parse_switch_report(rr.data, rr.len, &tmp);
                    }
                    break;
                case GP_MODE_PS4:
                    parse_ps4_report(rr.data, rr.len, &tmp);
                    break;
                case GP_MODE_XINPUT:
                    parse_xinput_report(rr.data, rr.len, &tmp);
                    break;
                default:
                    break;
            }

            if (xSemaphoreTake(g_state_mutex, 0) == pdTRUE) {
                g_state = tmp;
                xSemaphoreGive(g_state_mutex);
            }
        }
    }
}

void usb_gamepad_init(void) {
    g_state_mutex = xSemaphoreCreateMutex();
    g_ctrl_sem = xSemaphoreCreateBinary();
    g_raw_queue = xQueueCreate(RAW_QUEUE_LEN, sizeof(RawReport));

    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &g_client_hdl));

    // Core1: USB 事件循环 + URB 回调（仅 memcpy 入队）
    xTaskCreatePinnedToCore(usb_host_task, "usb_host", 4096, NULL, 4, NULL, 1);
    // Core0: 解析任务（优先级高于 nrf24 转发，保证最新数据被发送）
    xTaskCreatePinnedToCore(parse_task, "parse", 4096, NULL, 6, NULL, 0);

    ESP_LOGI(TAG, "USB Host initialized (3-task dual-core)");
}

void usb_gamepad_get_state(GamepadState *out) {
    if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
        *out = g_state;
        xSemaphoreGive(g_state_mutex);
    }
}

GamepadMode usb_gamepad_get_mode(void) {
    return g_mode;
}

bool usb_gamepad_connected(void) {
    return g_connected;
}
