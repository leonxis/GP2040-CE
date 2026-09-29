#include "IMyGamepad.h"
#include "NimBLEDevice.h"
#include "ble_uuid.h"
#include "esp_log.h"

static const char *TAG = "mygamepad";

static const uint8_t FIXED_BATTERY_LEVEL = 100;

class MyBattCharaCallback : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic *pCharacteristic,
                     NimBLEConnInfo &connInfo, uint16_t subValue) {
        (void)connInfo;
        // Send the (fixed) battery level once on subscription.
        if (subValue & 0x01) {
            uint8_t level = FIXED_BATTERY_LEVEL;
            pCharacteristic->notify(&level, 1, true,
                                    BLE_HCI_LE_CONN_HANDLE_MAX + 1);
        }
    }
};

IMyGamepad::IMyGamepad(const std::string &deviceName) {
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
    NimBLEDevice::setSecurityAuth(true, true, true);
    // Too low TX power increases latency; boosted again in start().
    NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(this);
    pHidDev = new NimBLEHIDDevice(pServer);
    NimBLEDevice::setDeviceName(deviceName);

    _battChara = pHidDev->batteryService()->getCharacteristic((uint16_t)UUID_BATT_LEVEL);
    _battChara->setCallbacks(new MyBattCharaCallback());
}

IMyGamepad::~IMyGamepad() {}

void IMyGamepad::start() {
    // High-performance parameters only.
    NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);

    pHidDev->setBatteryLevel(FIXED_BATTERY_LEVEL);
    pHidDev->startServices();
    pServer->advertiseOnDisconnect(true);
    pServer->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(NimBLEUUID((uint16_t)UUID_HID_SERV));
    pAdvertising->setAppearance(HID_GAMEPAD);
    pAdvertising->start(MAX_ADVERTISING_TIME_MS);
}

void IMyGamepad::stop() {
    pServer->advertiseOnDisconnect(false);
    pServer->stopAdvertising();
    for (auto cnnid : pServer->getPeerDevices()) {
        pServer->disconnect(cnnid, BLE_ERR_RD_CONN_TERM_PWROFF);
    }
}

size_t IMyGamepad::getConnectedCount() {
    return pServer->getConnectedCount();
}

void IMyGamepad::clearBond() {
    NimBLEDevice::deleteAllBonds();
    ESP_LOGW(TAG, "all bonds cleared");
}

int IMyGamepad::dirKey2DPadValue(int up, int right, int down, int left) {
    uint8_t dir = up | (right << 1) | (down << 2) | (left << 3);
    switch (dir) {
        case 1:
        case 11: return 1;   // up
        case 2:
        case 7:  return 3;   // right
        case 3:  return 2;   // up-right
        case 4:
        case 14: return 5;   // down
        case 6:  return 4;   // down-right
        case 8:
        case 13: return 7;   // left
        case 9:  return 8;   // up-left
        case 12: return 6;   // down-left
        default: return 0;   // centered / invalid
    }
}

void IMyGamepad::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) {
    pServer->updateConnParams(connInfo.getConnHandle(),
                              DESIRED_MIN_CONN_INTERVAL,
                              DESIRED_MAX_CONN_INTERVAL,
                              DESIRED_SLAVE_LATENCY,
                              SUPERVISION_TIMEOUT);
}

void IMyGamepad::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) {
    (void)pServer;
    (void)connInfo;
    ESP_LOGI(TAG, "disconnected, reason %d", reason);
}

void IMyGamepad::onMTUChange(uint16_t MTU, NimBLEConnInfo &connInfo) {
    (void)connInfo;
    ESP_LOGI(TAG, "MTU %u", MTU);
}

uint32_t IMyGamepad::onPassKeyRequest() {
    return 123456;
}

bool IMyGamepad::onConfirmPIN(uint32_t pin) {
    (void)pin;
    return true;
}

void IMyGamepad::onAuthenticationComplete(NimBLEConnInfo &connInfo) {
    if (!connInfo.isEncrypted()) {
        NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
        ESP_LOGW(TAG, "encryption failed; disconnecting");
        return;
    }
    ESP_LOGI(TAG, "authentication complete");
}

uint8_t getRptID(NimBLECharacteristic *pCharacteristic) {
    NimBLEDescriptor *desc =
        pCharacteristic->getDescriptorByUUID((uint16_t)UUID_GATT_REPORT_REF);
    uint8_t id =
        ((const NimBLEAttValue &)(desc->getValue(nullptr)))[0];
    return id;
}
