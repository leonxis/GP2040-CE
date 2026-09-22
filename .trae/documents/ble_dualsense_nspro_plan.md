# BLE 三手柄模式落地（DualSense / NS PRO）实施计划（修订版 v7，终稿）

## Repository Research

### 现状（wireless-tx 分支，HEAD f9a19b3a）

* 数据链路：Pico 经 UART1(GPIO8/9, 921600) 以二进制帧向 ESP32-S3 发送 INPUT(19B 帧)/STATUS(25B 帧)；ESP32 按 STATUS\[18] linkMode 在 nRF24 与 BLE 两个互斥输出后端间切换。**现行 BLE 与 nRF 模式共用同一 INPUT 帧格式（13B 载荷），Pico 不维护两套结构**。

* 帧格式（[uart\_link.cpp:54-80](file:///home/leonxis/GP2040/GP2040-CE/src/addons/uart_link.cpp#L54-L80) 实测）：`0xAA | ver | type | len | payload | CRC16(lo,hi)`，CRC 覆盖 ver 起至 payload 止。

* ESP32 固件 [esp32\_lite.ino](file:///home/leonxis/GP2040/GP2040-CE/ESP32/esp32_lite.ino) BLE 仅 Xbox Series X 落地（`blePadTypeForInputMode()` 全部映射 XBOX）；DualSense/NS PRO 分支空实现。

* **ESP32 核心版本：生产环境为 Arduino IDE core 3.3.10（tick=1000）**。本地 PIO 仅装 espressif32\@7.1.2（= core 2.0.17，tick=100，即 memory 记录的 vTaskDelay 忙循环/蓝牙延迟问题环境）→ 本计划将 PIO 对齐 3.3.10（pioarduino 平台），本地验证与生产一致；现有 ms 级 `vTaskDelay` 写法在 3.3.10 下正确，新代码不引入小值 vTaskDelay 依赖。

* NimBLE-Arduino 实际版本 **2.5.1**（platformio.ini ^2.3.3 语义化范围解析所得，用户确认）。补丁目标：`ble_store_nvs.c:45` `#define NIMBLE_NVS_NAMESPACE "nimble_bond"`。

* Pico IMU 数据通路（**不改动 IMU 插件**，[lsm6dsr\_imu.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/addons/lsm6dsr_imu.cpp) 实测）：

  * LSM6DSR addon 在 preprocess 读 SPI 并按 `outputMode`（网页"IMU 输出模式"）分派写 `gamepad->auxState.sensors`；uart\_link postprocess 经 `GetProcessedGamepad()` 读同一对象，同帧新鲜。

  * **输出模式=DS4 时**（[lsm6dsr\_imu.cpp:634-644](file:///home/leonxis/GP2040/GP2040-CE/src/addons/lsm6dsr_imu.cpp#L634-L644)）：inputMode=SWITCH\_PRO(15) → `outputGyroToSwitchPro` 填 `switchProImuData[36]`（3 组×12B，NS Pro 坐标/单位，第三组 \[24..35] 为当前帧）+ `switchProImuDataActive=true`，**同时清零** gyroscope/accelerometer；其余 inputMode → `outputGyroToDS4` 填 `gyroscope`/`accelerometer`（DS4 报告单位：陀螺 0.061°/s/LSB、加速度 8192/g，与 DS5 报告刻度一致，GP2040 PS4/P5General 驱动均为直接透传）+ enabled=active=true。

  * **输出模式=鼠标时**：`clearGyroOutput` 清 IMU auxState（[lsm6dsr\_imu.cpp:645-646](file:///home/leonxis/GP2040/GP2040-CE/src/addons/lsm6dsr_imu.cpp#L645-L646)）→ 无 IMU 数据。

  * **XINPUT/XBONE 等 Xbox 系模式**：available() 不含 + nativeUsb 门控（[lsm6dsr\_imu.cpp:564-572](file:///home/leonxis/GP2040/GP2040-CE/src/addons/lsm6dsr_imu.cpp#L564-L572)）双重拦截 → Xbox 模式 IMU 数据天然恒零 + flag 恒 0。

  * **BLE 陀螺数据源成立条件**：网页 IMU 插件开关开 + IMU 输出模式=DS4（鼠标模式无数据）+ 生效方式未暂停（engage 暂停时 flag=false）+ inputMode 为 PS4/PS4B（DS 路径）或 SWITCH\_PRO（NS 路径）。输入模式切换必重启，两种数据源互斥无残留。

  * NS(1) 模式：插件 available() 未涵盖 → 无陀螺（遵用户指令不改动 available()）。

* LSM6DSR 驱动未用硬件 FIFO；NS Pro 三组数据源于软件历史帧。

* 参考项目（/home/leonxis/myblegamepad）协议结论：

  * **DualSense(0x054C/0x0CE6)：0x31 完整报告只含 1 组 IMU**（`BTGetStateData`：counter + AngularVelocity + Acceleration 单组；0x01 精简报告 9B 无 IMU）→ 单组 UART 直传成立。feature 报告（0x05/0x09/0x20 等参考实现所建）静态应答，主机读 0x05 时切 0x31 模式；CRC32 = 先对 seed 头 0x31A1 再对数据（除末 4B）。

  * SwitchPro(0x057E/0x2009)：0x3F 简单报告（11B，标准 HID）；0x30 完整报告 48B 含 **3 组 IMU**（每组 12B）→ ESP32 将 Pico 单组复制 3 份填入；subcmd 经 output 0x01 下发、0x21 应答（0x02 设备信息/0x03 报告模式/0x10 SPI 校准/0x30 LED/0x40 IMU/0x48 振动/0x50 电压）。

  * 安全：bond+MITM+SC、IO CAP DISPLAY\_YESNO、passkey 123456、认证未加密断开；广播 0x1812 + appearance 0x03C4 + advertiseOnDisconnect；参考项目三模式 MAC 仅做简单字节偏移（mac\[1] += 类型号），**不动地址类型/OUI 位**。

### 安卓平台兼容性（本方案如何继承参考项目的兼容性）

* **根因**：Android 内核自带 `hid-playstation`（DualSense）与 `hid-nintendo`（Pro Controller）驱动族，按**原厂 VID/PID** 匹配内建键位布局并解析其标准 reportmap。参考项目三模式（含小米 10s 实测）全部如此构造；本方案逐字节移植其 reportmap/协议/VID/PID → 兼容性随之继承，这不是通用 HID 就能自动获得的（现 Xbox 模式即不可见）。

* **DS 陀螺在安卓可用**：安卓内核 hid-playstation 读 feature 0x05 → 我们应答后切 0x31 → 与真 DualSense 同路径；线性扳机为标准 HID 轴直接映射。

* **广播完备**：基类广播含 0x1812 服务 UUID + appearance 0x03C4 + 设备名（与参考一致）。

* **配对**：bond+MITM+SC，安卓弹数字比较框（显示 123456）用户确认。

* **MAC 规避项**：现有 Xbox 模式 `bleSetUniqueAddress` 设 `base[0] |= 0x02`（LAA 位）使 OUI 变为本机管理地址——广播内容已核实完备（0x1812+appearance+名，[BleCompositeHID.cpp:300-312](file:///home/leonxis/GP2040/GP2040-CE/ESP32/lib/ESP32-BLE-CompositeHID/BleCompositeHID.cpp#L300-L312)），该 LAA 位是安卓搜不到的候选原因之一（部分安卓栈过滤非规范公网地址）。**新 DS/NSPro 模式不设 LAA 位**（保持 efuse 派生合法公网地址 + 仅末字节偏移）；Xbox 路径维持现状不修（安卓不支持 Xbox BLE 协议属平台限制，本轮不承诺）。

### 用户已确认的决策

1. 进入路径：XINPUT/XINPUTB→蓝牙 Xbox；DS4/DS4B→蓝牙 DualSense；NS/NSPRO→蓝牙 NS Pro。不新增输入模式、不改动 IMU 插件 available()。
2. NimBLE = 2.5.1；ESP32 core = 3.3.10（生产）。
3. 不考虑旧配置兼容。
4. 多主机绑定完整方案（补丁化 NimBLE + 每模式独立 MAC/绑定命名空间）。
5. 不复制：高性能模式切换、偏移校准、振动。
6. NS Pro 单组 IMU 传输 + ESP32 侧复制 3 组；IMU 与模拟量同等 900µs 节流。
7. **UART INPUT 帧全模式统一 1 套 26B 含 IMU 格式（Pico 不区分 linkMode 组帧、单一数据结构）；ESP32 端按 linkMode 解析重构：BLE 全量使用，nRF 只取控制段打包现有无线包（无线包格式与接收端不动）；为未来 nRF 增加 IMU 预留扩展点。**

## UART INPUT 帧设计（字节级核实）

**RP2040 UART TX FIFO = 32 字节**。

| 场景                           | payload | 整帧      | 说明                           |
| ---------------------------- | ------- | ------- | ---------------------------- |
| **全部模式统一**（nRF 与 BLE 共用同一格式） | **26B** | **32B** | 恰好填满 FIFO 单突发零阻塞；Pico 单一数据结构 |

* 帧字节图（32B，**全部有效，无垃圾字节**）：

  ```
  [0] 0xAA  [1] ver  [2] type=0x01  [3] len=26
  [4..5] buttons u16LE   [6] dpad 掩码
  [7..14] lx/ly/rx/ry u16LE   [15] lt   [16] rt
  [17..22] gyroX/Y/Z i16LE（DS4 单位 或 NS Pro switchProImuData 当前组 12B 原样；不可用填 0）
  [23..28] accelX/Y/Z i16LE（同上）
  [29] flags：bit0=IMU 有效（真实数据；插件未启用/鼠标输出模式/Xbox 模式/暂停=0）
  [30..31] CRC16 LE（覆盖 [1..29]）
  ```

* Pico 端：`sendInputFrame` 恒发 26B 载荷；IMU 取值按 inputMode 从 auxState 读（与 linkMode 无关）；IMU 变化并入模拟量 900µs 节流（数字键变化仍立即发送）。IMU 恒零场景不因 IMU 触发发送，Xbox 模式流量与现状相当；IMU 使能且运动时最坏 \~1111 帧/s × 32B ≈ 38% UART 带宽（含静止时陀螺噪声持续触发，属预期）。

* ESP32 端 `onInputFrame` 渐进解析：len≥13 取控制段、len≥26 取 IMU 段（统一格式下恒为 26，渐进式为防御性处理）。

  * **BLE 路径**：全量使用（DS 0x31 单组直填；NS Pro 0x30 单组复制 3 份，组内差分信息损失对体感可接受，Switch 主要消费最新样本；flag=0 时报告填零）。

  * **nRF 路径**：radioTask 仅取控制段打包现有无线包，**nRF 无线包格式与接收端固件完全不动**；未来 nRF 加 IMU 时仅扩展无线包（nRF24 单包上限 32B，现有 \~15B，加 12B IMU+1B flag 后 \~28B 仍有余量），UART 协议无需再改。

* 32B 写入空 32B FIFO：`uart_write_blocking` 每字节立即可写，零等待不阻塞主循环。

## Files and Modules

### Pico 侧

* [headers/addons/uart\_link.h](file:///home/leonxis/GP2040/GP2040-CE/headers/addons/uart_link.h) / [src/addons/uart\_link.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/addons/uart_link.cpp)：

  * `sendInputFrame` 增加 IMU 参数（gyro\[3]/accel\[3]/valid），帧缓冲 19→32B，恒发 26B 载荷（唯一调用点 [uart\_link.cpp:193](file:///home/leonxis/GP2040/GP2040-CE/src/addons/uart_link.cpp#L193) 同步更新）；

  * postprocess：按 inputMode 从 auxState 读 IMU（PS4/PS4B→gyroscope/accelerometer，valid=enabled&\&active；SWITCH\_PRO→switchProImuData\[24..35]，valid=switchProImuDataActive；其余→零值 + flag 0），IMU 变化并入 900µs 节流。

* **不改** lsm6dsr\_imu、不改输入模式枚举、不改 nRF 无线包。

### ESP32 侧

* [ESP32/platformio.ini](file:///home/leonxis/GP2040/GP2040-CE/ESP32/platformio.ini)：平台对齐 pioarduino（arduino-esp32 3.3.10，实施时查 pioarduino 对应 release，github 走代理）；移除 NimBLE registry 依赖。

* **新增 vendor 库** `ESP32/lib/NimBLE-Arduino/`：复制 2.5.1（来自 .pio/libdeps）并打补丁（`ble_store_nvs.c` 的 `#define NIMBLE_NVS_NAMESPACE` → `extern const char *NIMBLE_NVS_NAMESPACE;`）。.ino 侧 `extern "C"` 定义，按模式赋 `xbox_bond`/`ds_bond`/`pro_bond`（首次 init 前）。

* **新增** `ESP32/ble_pad_direct.h/.cpp`：直构后端基类（移植参考 IMyGamepad，NimBLE 2.5.1 API：setReportMap/getInputReport/getOutputReport/getFeatureReport/setPnp/setHidInfo/getBatteryService，回调签名 `onConnect(server, connInfo)` 等）+ init 后 `ble_gap_set_prefered_default_le_phy(2M)`。

* **新增** `ESP32/ble_pad_dualsense.h/.cpp`：279B reportmap；0x01(9B)/0x31(77B) 双输入报告（0x31：counter、单组 IMU、PowerPercent=10、CRC32；轴序按参考 struct 逐字节照抄，注意 AngularVelocity 为 X/Z/Y 交错排列）；feature 报告静态应答（0x05 onRead 切 0x31 / 0x09 本机 MAC / 0x20 HardwareInfo=0x0000FF00+UpdateVersion=0x0458 等，以参考实现所建为准）；output 0x31-0x39 特征创建、回调留空（不做振动）；Pico 直传 DS4 单位原样填，flag=0 时 IMU 填零。

* **新增** `ESP32/ble_pad_switchpro.h/.cpp`：reportmap（输入 0x21/0x30/0x31/0x32/0x33/0x3F，输出 0x01/0x10/0x11/0x12）；0x3F 默认；0x30（12bit 摇杆压缩、参考实现的 A↔B/X↔Y 互换规则原样移植、单组 IMU 复制 3 份）；subcmd 0x21 应答族 + MAC 回填本机地址。

* [ESP32/esp32\_lite.ino](file:///home/leonxis/GP2040/GP2040-CE/ESP32/esp32_lite.ino)：

  * rxPayload 扩到 32；`onInputFrame` 渐进解析出控制段 + IMU 全局（lastGyro/lastAccel/lastImuSet/imuValid）；

  * `blePadTypeForInputMode()`：XINPUT(0)/XINPUTB(18)/XBONE(5)/default→XBOX（现状兜底不变）；PS4(4)/PS4B(17)→DUALSENSE；SWITCH(1)/SWITCH\_PRO(15)→NSPRO；

  * MAC 每模式独立：Xbox 维持现有派生不变（含 base\[0] LAA 位与 -3 偏移，Windows 零影响）；DS/NSPro **不设 LAA 位**（见安卓兼容节），仅末字节偏移 `base[5] = bt[5] - 5 / - 7`；

  * bleTask 双后端接线：XBOX→CompositeHID 路径不动（忽略 IMU 数据）；DS/NSPro→直构对象，发送节拍沿用 5ms 固定节流 + on-change 语义。共用既有设施：LED 任务、500ms 连接间隔治理（>6 重请 (6,6,0,600)）、ready 门控（直构路径 onAuthenticationComplete 后才发首帧）、断连边沿恢复广播、类型变化 ESP.restart() 收敛；

  * 设备名："Wireless Controller" / "Pro Controller"；

  * CPU 频率维持现文件 80MHz 不动（3.3.10 下用户实测无延迟；如 nRF 延迟复现一行改回 240MHz）。

* 交付：补丁版 `NimBLE-Arduino-2.5.1-patched.zip` 到 /home/leonxis/FTP。

## Implementation Steps

1. Pico：uart\_link 统一 26B 帧 + IMU 取值/节流；增量编译出 uf2。
2. PIO 平台对齐 3.3.10（查 pioarduino release、装平台，**触发全量重编译**，github 走代理）。
3. NimBLE 2.5.1 vendor 入库 + 补丁 + platformio.ini 调整；编译验证链接的是 lib/ 补丁版。
4. ESP32：rxPayload/渐进解析/映射/MAC/命名空间。
5. ble\_pad\_direct 基类 + DualSense（先 0x01 打通安卓，再 0x31/feature/CRC32/IMU）。
6. SwitchPro（先 0x3F，再 0x30+subcmd 族）。
7. ino 双后端接线与 bleTask 分派。
8. PIO 全量编译修错（esp-nimble-cpp→NimBLE 2.5.1 API 适配差异在此暴露）。
9. 同步 FTP：uf2、firmware.bin、补丁版 NimBLE ZIP；交付说明（安卓用 DS4/NSPRO 模式、DS 配对数字比较码 123456、BLE 陀螺要求 IMU 输出模式=DS4）。

## Dependencies and Considerations

* 代理 192.168.1.85:10808（github/pioarduino）；PIO registry 直连。

* 生产 core 3.3.10（tick=1000）：现有 ms 级 vTaskDelay 正确；新代码不引入对小值 vTaskDelay 的依赖；本地 PIO 与生产同版本，杜绝 2.0.17 忙循环类环境差异坑。

* UART0 纯二进制帧通道，禁止文本打印；库头文件无条件 #include。

* nRF 无线包格式与接收端本轮不动；未来 nRF+IMU 仅扩展无线包（32B 上限内有余量）。

* IMU 刻度：DS 直传 DS4 单位（GP2040 现有驱动约定）；NS Pro 直传 switchProImuData 协议单位；轴号/符号真机逐轴校正。

* NS(1) 模式无陀螺；DS/NS 路径陀螺要求数据源成立（见数据通路节）。

* 同一文件多个 Edit 必须串行。

## Validation

* 编译：Pico HML uf2；ESP32 PIO 全量（3.3.10）firmware.bin（确认链接的是 lib/ 补丁版 NimBLE）。

* Windows：Xbox 不回归（统一帧后 Xbox 路径行为不变）；DS 识别 Wireless Controller（按键/摇杆/线性扳机/陀螺）；NS Pro 0x3F 基本输入。

* 安卓（核心目标）：DS、NS Pro 均可被搜索发现 → 配对（DS 数字比较码 123456）→ 输入正确；DS 线性扳机+陀螺；Xbox 模式安卓行为如实记录（本轮不修）。

* 多绑定：同主机三模式切换免重配回连；两台主机各自绑定。

* nRF 回归（无线包格式不变，接收端无需重刷）；LED 语义两新路径成立；Switch 真机 0x30/subcmd 独立检查点。

## Risks

* NS subcmd/SPI 校准应答时序：逐字节照抄参考实现。

* DS CRC32/0x31 切换：严格照抄 seed 头与字节序。

* NimBLE 2.5.1 API 适配差异：编译期集中修正。

* `extern "C"` 链接命名空间：编译通过后用回连行为验证。

* DS/NS Pro 首次为新设备（MAC 变化）属预期；Xbox 偏移不变。

* pioarduino release 与 3.3.10 版本对应关系需实施时确认（取最接近 3.3.x；以 tick=1000 且兼容 vendored 库编译为准）。

* 陀螺轴向/缩放真机校正。

