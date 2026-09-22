# 蓝牙模式独立开关改造 实施方案（v3）

## 一、需求与研究结论

分支：`wireless-tx`（工作树干净）。当前实现：蓝牙是手柄输入模式 `INPUT_MODE_BLE=19`，
由模式设置下拉框选择；ESP32 依据 STATUS 帧 `inputMode==19` 在 nRF24 / BLE 间互斥切换。

研究确认的现状：

1. **网页**：蓝牙入口在 [hmlInputModes.ts](file:///home/leonxis/GP2040/GP2040-CE/www/src/Pages/HMLSettings/constants/hmlInputModes.ts#L54)（value 19），
   提示块在 [ModeSettings.tsx](file:///home/leonxis/GP2040/GP2040-CE/www/src/Pages/HMLSettings/components/ModeSettings.tsx#L248-L260)；
   无线开关在 [HardwareConfig.tsx](file:///home/leonxis/GP2040/GP2040-CE/www/src/Pages/HMLSettings/components/HardwareConfig.tsx#L380-L408)，
   已与 USB 验证器前端互斥。
2. **miniled**：[GPFusionMenuScreen.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/display/ui/screens/GPFusionMenuScreen.cpp#L129-L145)
   有"验证器""无线连接"两个 OPT_BOOL 及互斥；模式列表 `INPUT_MAP`/`N_INPUT` 含 19。
3. **门控**：[gp2040.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp#L506-L519)
   中 `bluetoothLinkEnabled(o)` 判定为 `o.inputMode == INPUT_MODE_BLE`。
   现状：nRF 无线模式（XINPUT/1000Hz/USB 已挂载）**仍走门控**；BLE 模式因不在
   supportedMode 列表天然无门控；USB 未挂载时 `main_loop_wireless_link_active`
   使门控降级为自由运行。
4. **USB 清零**：[gp2040.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp#L1369-L1397)
   与 [gp2040.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp#L1497-L1544)
   两处 `suppressUsb = g_screenOperationActive || main_loop_wireless_link_active`，
   清零上报后 memcpy 恢复——主循环负载不变，无意义。
5. **UART**：[uart_link.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/addons/uart_link.cpp#L160-L192)
   当前为"变化发 + 50ms 心跳"，INPUT 帧 19B(payload13)、STATUS 帧 24B(payload18，
   `[2]=inputMode`)，921600 波特。
6. **参考项目 GP-combine**（/home/leonxis/GP2040/GP-combine，已核对）：
   同为"变化发 + 50ms 心跳"（src/addons/uart_link.cpp:397-406），无主循环门控、
   自由跑；不洪泛的原因是摇杆经死区处理后空闲时 nx=ny=0，输出值被钳为**恒定中点**
   （analog.cpp:126-149；本项目
   [unified_analog_processor.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/addons/unified_analog_processor.cpp#L270-L306)
   逻辑相同，死区内输出精确 0x8000）。
7. **ESP32**：[esp32_lite.ino](file:///home/leonxis/GP2040/GP2040-CE/ESP32/esp32_lite.ino#L184-L221)
   `onStatusFrame` 取 inputMode 调 `applyOutputMode()`；radioTask **2ms 固定节拍
   （500Hz）**读全局发 nRF，bleTask 5ms 节拍发 BLE（链路容量 133 事件/s）。
8. **配置**：`GamepadOptions.wirelessLinkEnabled` 为 proto 字段 40，字段 41 已被
   wirelessPaired 占用；新字段用 42。nanopb 构建期生成，无需手写生成文件。

**结论：可行。** 蓝牙从"输入模式"改为"输出路径开关"；手柄 USB 模式照常选择，
ESP32 按所选模式选 BLE 设备类型（当前仅 Xbox 实现，其余退化 Xbox）。

## 二、UART 发送策略（反馈 2/3 后定稿）

主循环实测：门控模式（nRF）由 USB 上报节拍驱动约 **960~1000Hz（周期 1.00~1.04ms）**；
蓝牙模式门控关闭后自由跑 1~4kHz。纯变化发在死区=0/噪声超死区时会按循环频率打帧，
需要与下游消费节拍匹配。下游 nRF radioTask 为 **2ms 固定节拍**：

端到端模拟量新增延迟 = 等下一帧 U + 等 radio 节拍 R(0~2ms)：

| 策略 | 最差延迟 | 平均 | 每 radio(2ms) 窗口新鲜帧 |
|---|---|---|---|
| 纯变化发（GP-combine） | ≈0（但无带宽保护，依赖死区） | ≈0 | 不保证 |
| UART 模拟量 2ms 限速 | 2+2 = **4ms**（两 2ms 网格相位漂移） | 2ms | 0~1 帧 |
| **UART 模拟量 0.9ms 限速（采纳）** | 0.9+2 = **2.9ms** | 1.45ms | **必有 ≥1 帧（常为 2 帧取最新）** |

**定稿混合策略：数字键变化即发（0 延迟）；模拟量变化发但最小间隔 900µs；
50ms 心跳保底。时间戳必须用微秒。**

```c
// postprocess() 每轮（门控时~1.04ms/次，自由跑 1~4kHz/次）
uint32_t nowUs = to_us_since_boot(get_absolute_time());   // 硬件定时器 1µs 分辨率
bool digitalChanged = (buttons != lastButtons || dpad != lastDpad);
bool analogChanged  = (lx/ly/rx/ry/lt/rt 任一变化);
if (digitalChanged ||
    (analogChanged && nowUs - lastSentUs >= 900) ||  // 模拟量最小 900µs
    nowUs - lastSentUs >= 50000) {                    // 50ms 心跳保底
    sendInputFrame(...);
    lastSentUs = nowUs;
}
```

**为何不用整数毫秒 / 不用 1ms 整数阈值**：现用 `to_ms_since_boot()` 为整数毫秒，
`>=1` 在循环抖动使两轮落入同一毫秒时会跳过一帧，下一帧间隔可被拉到 ~2ms、偶发
>2.08ms（1.04ms×2），可能导致 radio 2ms 空窗；0.9ms 在整数 ms 下更无法表达
>（`>=0` 等于不限速）。改微秒后 1.04ms 周期下每轮确定性发送（1040≥900），
> 阈值留 100µs 抖动余量。

**为何 900µs 而非更短**：门控模式下 postprocess 最快 1.0ms 才被调一次，
阈值低于循环周期即为空操作，900 与 800/500 行为相同；BLE 自由跑模式下 900µs
≈1111 帧/s，带宽 1111×19B×10bit÷921600 ≈ **23%**，安全，且多余帧被 BLE 协议栈
合并无副作用。

- 数字键边沿经消抖、天然稀疏，不受限速立即发（延迟 ≈0，与 GP-combine 手感一致）；
- 门控模式帧间隔 = 主循环周期 1.00~1.04ms，始终 < 2ms，每个 radio 窗口必有新帧；
- 空闲死区钳中点 → 零帧，仅 50ms 心跳维持 ACK/链路指示与抗丢帧；
- postprocess 自身耗时 ~2µs、uart_write_blocking 入 32 字节 FIFO ~2µs（满时单次
  最多等 1 字节移位 ≈10.9µs），不阻塞主循环。

## 三、总体设计

1. 新增持久化字段 `GamepadOptions.bluetoothLinkEnabled`（proto 42，默认 0）。
2. **三开关互斥**（硬件约束：无线/蓝牙占用 GPIO8/9 = USB0 D+/D-）：网页与 miniled
   即时互斥；config_utils 加载后归一化兜底（无线 ON → 关蓝牙+验证器；蓝牙 ON →
   关验证器）。
3. **门控仅依据蓝牙开关降级**（nRF 现状不动）：
   `shouldUseMainLoopGate()` 增加 `&& !bluetoothLinkEnabled`；USB 未挂载降级分支
   仍按 `无线 || 蓝牙` 缓存标志判定（保持 nRF 现有行为）。
4. **删除无线/蓝牙的 USB 清零**（两处），仅保留 miniled 菜单清零。此后 USB 口与
   无线链路并行输出真实按键。
5. **UART**：`available()` = 无线开关 || 蓝牙开关；INPUT 改第二节定稿混合策略；
   STATUS 帧新增 payload[18]=**linkMode**（0=nRF 路径，1=BLE 路径），帧 24→25B，
   变化即发 + 1s 心跳；inputMode 仍真实上报（nRF 包 pkt[0] + BLE 选设备类型）。
6. **BLE 设备类型按 inputMode 选择**：ESP32 新增选择器
   `blePadTypeForInputMode(mode)`：
   - XBOX（当前唯一已实现）：XINPUT(0)/XINPUTB(18)/XBONE(5) 及所有不支持模式
     （**不支持即退化 XBOX**，含键盘/PS3/GENERIC/各 mini 主机等）；
   - DUALSENSE（预留，本次不实现，先落 XBOX）：PS4(4)/PS4B(17)/PS5(13)/P5GENERAL(16)；
   - NSPRO（预留，本次不实现，先落 XBOX）：SWITCH_PRO(15)。
   选择器以枚举+switch 落地，后续补库支持时只需加分支；bleBegin 时按当前类型构造。
   Pico 侧切模式必重启，ESP32 收到新 STATUS 后若 BLE 运行中且类型变化，走一次
   stop→begin 重配（不同描述符/VID/PID，主机按新设备枚举，属预期）。
7. **不做旧配置兼容**：彻底删除 `INPUT_MODE_BLE=19`——enums.proto、
   drivermanager、gp2040、两个显示屏幕、miniled 模式表、网页下拉/提示、ESP32 宏
   全部清除；不做任何 19→其他值的迁移。

## 四、文件与改动清单

### Pico 固件

- [proto/enums.proto](file:///home/leonxis/GP2040/GP2040-CE/proto/enums.proto#L152)
  删除 `INPUT_MODE_BLE = 19;`。
- [proto/config.proto](file:///home/leonxis/GP2040/GP2040-CE/proto/config.proto#L39-L41)
  新增 `optional bool bluetoothLinkEnabled = 42;`。
- [headers/addons/uart_link.h](file:///home/leonxis/GP2040/GP2040-CE/headers/addons/uart_link.h)
  新增 `DEFAULT_BLUETOOTH_LINK_ENABLED 0`；协议注释加 STATUS payload[18]=linkMode；
  `sendStatusFrame` 加 linkMode 参数；新增 `lastLinkMode`。
- [src/addons/uart_link.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/addons/uart_link.cpp)
  - `available()`：`wirelessLinkEnabled || bluetoothLinkEnabled`；
  - `sendStatusFrame(inputMode, linkMode)`：25B、len=19、CRC 覆盖 payload[18]；
  - `postprocess()`：时间戳改 `to_us_since_boot` 微秒计时；数字立即发 /
    模拟 900µs 限速 / 50ms 心跳（成员 lastSent 改 lastSentUs）；
    STATUS 增加 linkMode 取值与变化即发（1s 心跳保留）。
- [src/config_utils.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/config_utils.cpp#L346)
  - 新增 `INIT_UNSET_PROPERTY(... bluetoothLinkEnabled, DEFAULT_BLUETOOTH_LINK_ENABLED)`；
  - `load()` 的 initUnset 之后加三互斥归一化（不做 19 迁移）。
- [src/webconfig.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/webconfig.cpp#L1170-L1176)
  readDoc/writeDoc bluetoothLinkEnabled；保存时互斥：无线 ON→蓝牙清 0+USB0 关；
  蓝牙 ON→USB0 关。
- [src/gp2040.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp)
  - 删除 `bluetoothLinkEnabled(o)` 中 inputMode==19 判定，改读
    `o.bluetoothLinkEnabled`（函数保留供缓存复用）；
  - `shouldUseMainLoopGate()` 增加蓝牙开关关闭条件；
  - 两处 suppressUsb 删除 `|| main_loop_wireless_link_active`，仅留
    `g_screenOperationActive`；更新相关注释。
- [src/drivermanager.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/drivermanager.cpp#L87-L92)
  删除 `case INPUT_MODE_BLE` 整段。
- [src/display/ui/screens/GPFusionMenuScreen.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/display/ui/screens/GPFusionMenuScreen.cpp)
  - `INPUT_MAP` 删 19、`N_INPUT` 删 "Bluetooth"、`INPUT_MAP_COUNT` 19→18；
  - 新增 `gBle/sBle`（读写 bluetoothLinkEnabled、needsReboot；开启时关无线+验证器）；
  - `sWireless` 开启时关蓝牙；`sUsbAuth` 开启时关蓝牙；
  - optHandle 在"无线连接"正下方加 `{"蓝牙模式", OPT_BOOL, ..., gBle, sBle}`。
- [LiteCustomLayoutScreen.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/display/ui/screens/LiteCustomLayoutScreen.cpp#L94)
  与 [ButtonLayoutScreen.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/display/ui/screens/ButtonLayoutScreen.cpp#L230)
  删除 INPUT_MODE_BLE case。
- [configs/HML/BoardConfig.h](file:///home/leonxis/GP2040/GP2040-CE/configs/HML/BoardConfig.h#L174-L184)
  新增 `#define DEFAULT_BLUETOOTH_LINK_ENABLED 0`（紧随无线默认值）。

### 网页前端（www）

- [hmlInputModes.ts](file:///home/leonxis/GP2040/GP2040-CE/www/src/Pages/HMLSettings/constants/hmlInputModes.ts#L54)
  删除 ble(19) 项。
- [ModeSettings.tsx](file:///home/leonxis/GP2040/GP2040-CE/www/src/Pages/HMLSettings/components/ModeSettings.tsx)
  删除 bleModeSpecifics 及其 switch case。
- [HardwareConfig.tsx](file:///home/leonxis/GP2040/GP2040-CE/www/src/Pages/HMLSettings/components/HardwareConfig.tsx)
  新增 bluetoothLinkEnabled state（fetch/保存合入 gamepad options）；无线开关下方加
  "蓝牙模式"开关；三方互斥联动（开任一→另外两个关）。
- zh-CN/en 的 SettingsPage.jsx：新增 `hml-bluetooth-link-label/hint`，
  删除 `hml-ble-mode-hint`（通用键 input-mode-options.ble 保留不动）。
- 构建：`npm run build`（build-proto + vite + makefsdata 刷新 fsdata.c）。

### ESP32 发射端

- [ESP32/esp32_lite.ino](file:///home/leonxis/GP2040/GP2040-CE/ESP32/esp32_lite.ino)
  - 删除 `BLE_INPUT_MODE 19` 宏及按 inputMode==19 的判定；
  - `onStatusFrame`：存 linkMode（payload[18]）与 inputMode（payload[2]），
    linkMode 变化调 applyOutputMode；**不做旧帧长兼容**（Pico/ESP32 同刷）；
  - 新增 `enum BlePadType { BLE_PAD_XBOX, BLE_PAD_DUALSENSE, BLE_PAD_NSPRO }` 与
    `blePadTypeForInputMode()` 选择器（本次仅 XBOX 分支实际构造，DUALSENSE/NSPRO
    列为预留 case 并 default→XBOX）；
  - bleBegin 按当前类型构造设备对象；STATUS 导致 BLE 运行中类型变化时
    bleTask 执行一次 stop→begin 重配；
  - 注释更新（linkMode 定路径，inputMode 定 nRF 模式字节/BLE 设备类型）。
  - PlatformIO 构建（~/.local/bin/pio，env 代理 192.168.2.90:10808）。

## 五、实施步骤（依赖顺序）

1. enums.proto 删 19 + config.proto 加字段 42；BoardConfig 默认值；
2. uart_link.h/.cpp：available、混合发送策略、STATUS linkMode；
3. config_utils 默认值与互斥归一化；webconfig.cpp 读写互斥；
4. gp2040.cpp 门控改蓝牙开关判定、删两处 USB 清零；drivermanager 删 case；
5. 两个显示屏幕删 case；GPFusionMenuScreen 菜单/模式表；
6. Pico 增量编译（cmake --build build，HML）；
7. ESP32 ino 改造 + pio 编译；
8. 网页 TSX/locales + npm run build；
9. Pico 重编（fsdata 更新后）出 UF2；ESP32 firmware.bin 与 UF2 同步 FTP。

## 六、依赖与注意事项

- 三开关均"保存并重启后生效"；门控/UART 在 setup 缓存，不做运行时切换。
- 门控仅蓝牙开关关闭；nRF 路径门控维持现状（postprocess 由 USB 节拍 960~1000Hz
  驱动，900µs 模拟阈值每轮必满足，帧间隔跟随循环 1.00~1.04ms）。
- 数字键立即发不受限速；限速只管连续变化的模拟量；空闲仅心跳，无额外流量。
- 时间比较一律微秒计时，避免整数毫秒量化在抖动时产生 ~2ms 双跳过。
- BLE 设备类型切换会更换 HID 描述符/VID/PID，主机按新设备枚举——仅发生在用户主动
  切换手柄模式（Pico 重启）时，符合预期。
- linkMode 放 STATUS（启动静态值、变化即发+心跳），不增加 INPUT 流量。
- 不保留任何 19 相关兼容代码；Pico 与 ESP32 固件须同时刷新。

## 七、验证

1. Pico/ESP32/网页三处构建通过；
2. 网页：三开关互斥联动；模式下拉无蓝牙项；选 PS4/PS5/SWITCH_PRO 等开蓝牙开关保存
   重启后，ESP32 实际仍以 Xbox 设备广播（退化逻辑），LED/链路正常；选 XINPUT 同理；
3. miniled：无线连接下方有"蓝牙模式"；模式列表无 Bluetooth；三开关互斥；
4. 按键延迟逻辑核查：空闲按数字键当轮 postprocess 即发（无等待）；持续打摇杆时
   门控模式帧间隔跟随循环 1.00~1.04ms（每轮必发），BLE 自由跑模式 ≤1111 帧/s
   （900µs 间隔），帧间隔始终 <2ms，每个 nRF 2ms 窗口必有新帧，UART 占用 ≤23%；
5. 回归：全关纯有线 1000Hz 门控正常；nRF 无线模式行为与改造前一致。

## 八、风险与处理

- **用户死区=0/噪声超死区**：900µs 限速兜底（微秒计时），UART 占用 ≤23%，不洪泛。
- **BLE 类型运行中切换**：stop→begin 重配，期间断连数百 ms；仅模式切换场景，可接受。
- **脏配置绕过 UI 互斥**：config_utils 加载归一化并落盘。
- **USB 并行输出**：取消清零后主机可能同时看到有线+蓝牙两设备，属本次设计预期。
