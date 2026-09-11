#include "BleConnectionStatus.h"
#include <NimBLEDevice.h>

BleConnectionStatus::BleConnectionStatus(void)
{
}

void BleConnectionStatus::onConnect(NimBLEServer *pServer, NimBLEConnInfo& connInfo)
{
    // 尽早请求一次高速连接参数（部分主机会立即接受 → 上电即 125Hz）；
    // 若此刻被拒绝，onAuthenticationComplete 会再请求一次，bleTask 前 30 秒还会周期重试。
    // 间隔锁定 6 unit=7.5ms(133Hz 容量)：若给 max=7，Windows 常选 7(8.75ms)=114Hz，
    // 实测回报率被钉在 ~111Hz 跑不满 125。min=max=6 才能保证 >=125Hz。
    pServer->updateConnParams(connInfo.getConnHandle(), 6, 6, 0, 600);
    this->connected = true;
    this->ready = false;  // 等认证完成后 CCCD 才恢复
}

void BleConnectionStatus::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason)
{
    this->connected = false;
    this->ready = false;
}

void BleConnectionStatus::onAuthenticationComplete(NimBLEConnInfo& connInfo)
{
    // 认证完成时 NimBLE 内部会调用 ble_gatts_bonding_restored()：从 NVS 恢复 CCCD
    // 订阅状态并发送待处理通知。此后 notify() 才有有效订阅者。
    // 同时在此请求连接参数更新（连接已稳定，Windows 更容易接受）。
    NimBLEServer *pServer = NimBLEDevice::getServer();
    if (pServer) {
        // 锁定 6 unit=7.5ms 间隔（6*1.25ms，133Hz 容量），latency=0, timeout=6s
        pServer->updateConnParams(connInfo.getConnHandle(), 6, 6, 0, 600);
    }
    this->ready = true;
}

bool BleConnectionStatus::isConnected(){
    return this->connected;
}

bool BleConnectionStatus::isReady(){
    return this->ready;
}
