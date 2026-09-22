# BLE 多模式切换断连修复（最小改动方案）

## Repository Research

### 用户接受的目标行为
- 不要求"配对一次、模式自动变形"；**接受每模式各自配对一次，之后切到已配对模式自动回连可用**。
- 要消除两个现象：① 切模式后旧设备节点不下线、不停断连/重连；② 断电再上电后新模式设备显示已连接却不停断连。

### 根因分析（代码级，已排除启动竞态：`bleDesired` 初值 false，首帧 STATUS 前不启 BLE；模式切换全流程仅一次 ESP.restart）

**现象①：旧节点长时间闪断——切换时 BLE 链路被硬切断，主机进入"异常丢失激进重连"。**
- [bleTask L545-547](file:///home/leonxis/GP2040/GP2040-CE/ESP32/esp32_lite.ino#L545-L547) 检测到 padType 变化后**直接 `ESP.restart()`**，当前活动 BLE 连接没有发送 LL_TERMINATE，广播也未停。
- 主机只能等监督超时（我方参数 timeout=600=6s）判定丢失，随后按"链路意外丢失"策略对旧地址持续激进重连 → 旧节点长时间"已连接/已断开"闪动、LED 同步闪。
- 修复：重启前走标准下线流程（停广播 + 以 0x13 REMOTE_USER_CONN_TERM 优雅断开在连主机 + 短暂等待信令发出），主机把旧节点标记为正常离线，快速退避。

**现象②：已配对新设备回连后断连循环——直构后端主动踢断"未加密"连接，破坏主机加密/重配对恢复。**
- [ble_pad_direct.cpp:109-116](file:///home/leonxis/GP2040/GP2040-CE/ESP32/ble_pad_direct.cpp#L109-L116)：`onAuthenticationComplete`（由 NimBLE `BLE_GAP_EVENT_ENC_CHANGE` 无条件触发）中一旦 `!isEncrypted()` 立即 `disconnect()`。
- ENC_CHANGE 在 SMP 中途、bond 密钥协商重试、加密状态临时变化时都可能以 encrypted=false 到达；主机（Win11 HID）本来会自行重试加密/重新配对（NimBLE 对 REPEAT_PAIRING 也已自动删旧 bond 重试），被设备秒踢后叠加 `advertiseOnDisconnect(true)` → 形成永久"连上就踢、踢了又连"循环。
- 佐证：Xbox 路径 [BleConnectionStatus.cpp:25-36](file:///home/leonxis/GP2040/GP2040-CE/ESP32/lib/ESP32-BLE-CompositeHID/BleConnectionStatus.cpp#L25-L36) 从不踢连接，Xbox 模式稳定。
- 修复：未加密时仅保持 `m_ready=false`（不进入就绪、不发报告），等待后续 encrypted=true 事件，绝不主动断开。NS/DS 的发送另有 CCCD 订阅门控（m_sub21/m_sub30/m_sub3F、m_sub01/m_sub31），未加密未订阅时 notify 静默丢弃，无误发风险。

### 明确不改动（控制工作量）
- 保留三模式独立 MAC + 独立 bond 命名空间（满足"每模式各配对一次"）。
- 不做单身份/GATT 变形/Service Changed/NVS 模式记录。
- 不改 MAX_BONDS（每模式独立命名空间各 3 个 host 名额，够用）。
- NS SPI 读越界写隐患（`arg[4]` 未钳制）与本问题无关，本次不动，后续单独处理。

## Files and Modules

- `ESP32/esp32_lite.ino`
  - bleTask 类型变化分支：`ESP.restart()` 前增加 `bleGracefulShutdownBeforeReboot()`：
    1. `NimBLEDevice::getAdvertising()->stop()`（直构路径先 `m_server->advertiseOnDisconnect(false)`，避免断开瞬间自动复活广播）；
    2. 遍历 `getConnectedCount()`，对每个 peer `disconnect(ci)`（默认原因码即 BLE_ERR_REM_USER_CONN_TERM=0x13）；
    3. 分段 vTaskDelay 累计约 300ms，保证 TERMINATE_IND 发出；
    4. `ESP.restart()`。
  - 用通用 NimBLEDevice API 实现，Xbox/DS/NS 三后端共用；NimBLE 未初始化时直接 restart。
- `ESP32/ble_pad_direct.cpp`
  - `onAuthenticationComplete`：`!isEncrypted()` 分支删除 `disconnect()`，改为仅注释说明"保持 m_connected 现状、m_ready=false，等待加密成功事件"；加密成功分支（含重连参数请求、m_ready=true）不动。

## Implementation Steps

1. 重读 bleTask L541-561 与相关 API 上下文后编辑 `esp32_lite.ino`，新增静态优雅下线函数并在 restart 分支调用；同步更新该分支注释。
2. 编辑 `ble_pad_direct.cpp` 的 onAuthenticationComplete（同文件串行编辑）。
3. `http_proxy=... pio run` 编译通过；GetDiagnostics 零问题；`cp .pio/build/esp32s3/firmware.bin /home/leonxis/FTP/esp32s3_firmware.bin`。
4. 按用户要求 git 提交。

## Dependencies and Considerations

- 刷入本版后，由于此前开发期 MAC/bond 策略多次变动，主机侧残留节点与设备密钥普遍失配（用户已实测"删除重配即稳定"）。**首次使用请删除 Windows 里全部相关旧节点，三个模式各配对一次**，之后按模式自动回连。
- 优雅下线等待 ~300ms，使模式切换总时间略增（远小于现状的 6s 监督超时+闪断）。
- 去掉强制踢后，真遇到长期不加密的异常连接，设备不会断开但也不发数据；HID 主机自身安全策略会发起加密，加密成功后 ENC_CHANGE 自然置 ready。

## Validation

- 编译/诊断零问题；固件产出到 FTP；提交完成。
- 用户硬件验收（Win11）：
  1. 删除全部旧节点；Xbox 模式配对并工作；
  2. 切 NS Pro：**Xbox 节点应在数秒内安静离线**（不再长时间闪断）；配对 Pro Controller 一次后稳定工作；
  3. NS 模式下断电再上电：Pro Controller **自动回连且稳定**，不再断连循环；
  4. 配 DS 一次；三模式来回切换，已配对模式均自动回连、无闪断。

## Risks

- 旧节点闪断若纯属主机对"暗地址"的策略性重试，优雅断开只能显著缩短/减轻、不能保证零闪动；如实测仍长时间闪断，再升级评估单身份方案（本目录前版计划已备）。
- 若 Win11 对已配对 HID 回连不主动加密（极端情况），去掉踢连接后该链路将静默无数据；实测如出现"连接稳定但无输入"，再在 bleTask 侧加延迟加密请求（而非断开），不在本次预判范围内。
