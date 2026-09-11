#ifndef ESP32_BLE_CONNECTION_STATUS_H
#define ESP32_BLE_CONNECTION_STATUS_H
#include "sdkconfig.h"
#if defined(CONFIG_BT_ENABLED)

#include "nimconfig.h"
#if defined(CONFIG_BT_NIMBLE_ROLE_PERIPHERAL)

#include <NimBLEServer.h>
#include "NimBLECharacteristic.h"
#include "NimBLEConnInfo.h"

class BleConnectionStatus : public NimBLEServerCallbacks
{
public:
    BleConnectionStatus(void);
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo& connInfo, int reason) override;
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override;
    //NimBLECharacteristic *inputGamepad;
    bool isConnected();
    // 认证完成后为 true——CCCD 恢复(ble_gatts_bonding_restored)在此回调中完成，
    // 之后 notify() 才有订阅者。bleTask 应等此标志为 true 后再发首帧。
    bool isReady();
private:
    bool connected = false;
    bool ready = false;
};

#endif // CONFIG_BT_NIMBLE_ROLE_PERIPHERAL
#endif // CONFIG_BT_ENABLED
#endif // ESP32_BLE_CONNECTION_STATUS_H
