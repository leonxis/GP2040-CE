#include "IMyGamepad.h"
#include "NimBLEDevice.h"
#include "ble_uuid.h"

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
    // Just Works pairing: no MITM, bond only. DS/NS Pro/Xbox BLE HID all
    // pair without a passkey; DisplayYesNo would pop an unusable dialog.
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(true, false, false);
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

int IMyGamepad::dirKey2DPadValue(int up, int right, int down, int left) {
    // Normalize to 0/1: callers may pass raw bitmask values (e.g. dpad & 0x08).
    uint8_t dir = (up ? 1 : 0) | (right ? 2 : 0) | (down ? 4 : 0) | (left ? 8 : 0);
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
    (void)reason;
}

void IMyGamepad::onMTUChange(uint16_t MTU, NimBLEConnInfo &connInfo) {
    (void)connInfo;
    (void)MTU;
}

uint32_t IMyGamepad::onPassKeyRequest() {
    return 123456;
}

bool IMyGamepad::onConfirmPIN(uint32_t pin) {
    (void)pin;
    return true;
}

void IMyGamepad::onAuthenticationComplete(NimBLEConnInfo &connInfo) {
    (void)connInfo;
    // Stay connected even if encryption did not complete. Disconnecting here
    // causes DS/NS Pro hosts to loop reconnection after a power cycle; the
    // host will retry pairing on its own while we simply send no data.
}

uint8_t getRptID(NimBLECharacteristic *pCharacteristic) {
    NimBLEDescriptor *desc =
        pCharacteristic->getDescriptorByUUID((uint16_t)UUID_GATT_REPORT_REF);
    uint8_t id =
        ((const NimBLEAttValue &)(desc->getValue(nullptr)))[0];
    return id;
}
