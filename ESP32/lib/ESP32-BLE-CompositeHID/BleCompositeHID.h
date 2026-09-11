#ifndef ESP32_BLE_MULTI_HID_H
#define ESP32_BLE_MULTI_HID_H
#include "sdkconfig.h"
#if defined(CONFIG_BT_ENABLED)

#include "nimconfig.h"
#if defined(CONFIG_BT_NIMBLE_ROLE_PERIPHERAL)

#include "BleConnectionStatus.h"
#include "NimBLEHIDDevice.h"
#include "NimBLECharacteristic.h"

#include "BLEHostConfiguration.h"
#include "BaseCompositeDevice.h"

#include <vector>
#include "SafeQueue.hpp"

class BleCompositeHID
{
public:
    BleCompositeHID(std::string deviceName = "ESP32 BLE Composite HID", std::string deviceManufacturer = "Espressif", uint8_t batteryLevel = 100);
    ~BleCompositeHID();
    void begin();
    void begin(const BLEHostConfiguration& config);
    void end();

    void addDevice(BaseCompositeDevice* device);
    bool isConnected();
    // 认证/CCCD 恢复完成——bleTask 应等此为 true 后再发首帧
    bool isReady();
    // 向所有已连接主机请求高速连接参数（7.5-8.75ms → 125Hz）。
    // 可在连接建立后的前几秒周期调用，直到主机接受为止。
    void requestFastConnectionParams();

    void queueDeviceDeferredReport(std::function<void()> && reportFunc);
    void sendDeferredReports();

    void setBatteryLevel(uint8_t level);
    uint8_t batteryLevel;
    std::string deviceManufacturer;
    std::string deviceName;

protected:
    virtual void onStarted(NimBLEServer *pServer){};

private:
    static void taskServer(void *pvParameter);
    static void timedSendDeferredReports(void *pvParameter);

    BLEHostConfiguration _configuration;
    BleConnectionStatus* _connectionStatus;
    NimBLEHIDDevice* _hid;

    std::vector<BaseCompositeDevice*> _devices;
    SafeQueue<std::function<void()>> _deferredReports;
    TaskHandle_t _autoSendTaskHandle;
};

#endif // CONFIG_BT_NIMBLE_ROLE_PERIPHERAL
#endif // CONFIG_BT_ENABLED
#endif // ESP32_BLE_MULTI_HID_H
