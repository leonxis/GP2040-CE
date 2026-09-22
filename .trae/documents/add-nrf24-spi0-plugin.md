# 新增 nRF24 直连插件（SPI0）实现计划

## 摘要

在 RP2354 上新增一个 nRF24 无线发射插件 `NRF24LinkAddon`，通过 SPI0 直接驱动 nRF24L01+ 模块，绕过现有 ESP32 中转路径。保留现有 `uart_link.cpp` 不动。前端硬件配置页在"蓝牙无线"开关下方新增"无线模式"开关，持久化到 `GamepadOptions.nrf24LinkEnabled`，默认关闭。三个无线开关（`wirelessLinkEnabled` / `bluetoothLinkEnabled` / `nrf24LinkEnabled`）三选一互斥。SPI 通讯使用 `PeripheralSPI` 现有 FIFO 阻塞传输（`spi_write_read_blocking`），不启用 DMA（基于 16 字节负载 @4MHz 的分析，DMA 收益小于配置开销）。

## 当前状态分析

### 硬件接线（BoardConfig.h L57-95）

* `SPI0_ENABLED 0`（当前禁用）

* SPI0 引脚已分配给 nRF24，但 SPI0 外设未启用：

  * `SPI0_PIN_CE 0`（GPIO0，nRF24 CE 引脚，非 SPI CS）

  * `SPI0_PIN_CS 1`（GPIO1，nRF24 CSN）

  * `SPI0_PIN_SCK 2`、`SPI0_PIN_TX 3`、`SPI0_PIN_RX 4`

* GPIO0-4 已标记为 `ASSIGNED_TO_ADDON`

### ESP32 侧 nRF24 驱动参考（ESP32/nrf24.h）

* 15 字节静态载荷（`NRF24_PAYLOAD 15`）

* 信道 100（2.500 GHz）、2Mbps、0dBm、auto-ACK pipe0、3 次重试 250us

* 5 字节地址 `0xE7,0xE7,0xE7,0xE7,0xE7`

* 关键 API：`begin(spi,csn,ce)` / `writePacket(data)` / `readPacket(data)` / `startListening()` / `powerUp/Down()` / `setChannel(ch)` / `resetLink()`

* `writePacket` 阻塞等待 TX\_DS（成功）或 MAX\_RT（失败），最长 2ms

### 现有 uart\_link 插件（保留不动）

* `src/addons/uart_link.cpp` / `headers/addons/uart_link.h`

* INPUT 帧 19 字节：`0xAA + ver + type + len + 13B payload + 2B CRC16`

  * payload: buttons(2) dpad(1) lx(2) ly(2) rx(2) ry(2) lt(1) rt(1) = 13 字节

* STATUS 帧 25 字节，含 `linkMode`（0=nRF24, 1=BLE）

* 由 `wirelessLinkEnabled`（nRF24 路径）和 `bluetoothLinkEnabled`（BLE 路径）驱动 `available()`

* `postprocess()` 读取 `Gamepad.state` 并按节流策略发送

### SPI 外设管理（PeripheralManager / PeripheralSPI）

* `PeripheralManager::initSPI()` 仅当 `peripheralOptions.blockSPI0.enabled` 为真时调用 `blockSPI0.setConfig()`

* `config_utils.cpp` L563-572 用 `INIT_UNSET_PROPERTY` 从 `SPI0_ENABLED` 宏写入默认值

* `PeripheralSPI::transfer(tx,rx,count)` 内部调用 `spi_write_read_blocking`（已使用 FIFO，但阻塞 CPU 等待完成）

* `_UseDMA` 默认 false，无公共 setter；DMA 基础设施已存在但未启用

* MCP3208 使用 SPI1 + `beginTransaction(profile, MSB_FIRST, MODE0)` + `select(cs)/deselect()` 模式

### 前端硬件配置页（HardwareConfig.tsx）

* `wirelessLinkEnabled` / `bluetoothLinkEnabled` state（L76-77）

* "2.4G无线"开关在 L385-415，"蓝牙无线"在 L417-447

* `handleHostSave`（L180-224）将两个开关写入 `GamepadOptions` via `WebApi.setGamepadOptions`

* 已有互斥逻辑：开无线关蓝牙+USB验证器；开蓝牙关无线+USB验证器

### Webconfig 后端（webconfig.cpp）

* L1102-1112：读取 `wirelessLinkEnabled`/`bluetoothLinkEnabled`，执行三互斥（无线 > 蓝牙 > USB验证器）

* L1170-1172：写入这两个字段

* L1066：`GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();`

### Proto 定义（proto/config.proto L39-42）

```
optional bool wirelessLinkEnabled = 40;   // 发射端使用（UARTLinkAddon 开关）；接收端闲置
optional bool wirelessPaired = 41;        // 接收端使用（配对标志）；发射端闲置
optional bool bluetoothLinkEnabled = 42;  // 发射端蓝牙输出路径开关；与无线连接/USB验证器互斥
```

### 插件注册（gp2040.cpp L1089）

* `addons.LoadAddon(new UARTLinkAddon());` 在 L1089

### CMakeLists.txt（L284-303）

* `src/addons/uart_link.cpp` 在 L303

### 配置初始化（config\_utils.cpp L413-414, L2042-2045）

* `INIT_UNSET_PROPERTY(config.gamepadOptions, wirelessLinkEnabled, DEFAULT_WIRELESS_LINK_ENABLED);`

* `INIT_UNSET_PROPERTY(config.gamepadOptions, bluetoothLinkEnabled, DEFAULT_BLUETOOTH_LINK_ENABLED);`

* L2042-2045：三互斥校验

## 设计决策

### 1. 数据包格式（15 字节 nRF24 载荷）

直接复用 uart\_link INPUT 帧的 13 字节 payload，去掉 6 字节帧头/CRC（nRF24 硬件 2 字节 CRC 已提供完整性保护），追加 2 字节元数据填满 15 字节：

| 偏移   | 字段           | 说明                       |
| ---- | ------------ | ------------------------ |
| 0-1  | buttons (LE) | 16 个按键位                  |
| 2    | dpad         | 8 方向 hat                 |
| 3-4  | lx (LE)      | 左摇杆 X                    |
| 5-6  | ly (LE)      | 左摇杆 Y                    |
| 7-8  | rx (LE)      | 右摇杆 X                    |
| 9-10 | ry (LE)      | 右摇杆 Y                    |
| 11   | lt           | 左扳机                      |
| 12   | rt           | 右扳机                      |
| 13   | inputMode    | 输入模式（XInput/PS4/NSPro 等） |
| 14   | reserved     | 预留/序号                    |

接收端按相同偏移解析。与 ESP32 nRF24 驱动的 `NRF24_PAYLOAD=15` 一致。

### 2. nRF24 寄存器配置（与 ESP32 端完全一致以确保互通）

* REG 0x00 CONFIG = 0x0E（PWR\_UP=1, PRIM\_RX=0, CRC=2字节）

* REG 0x01 EN\_AA = 0x01（pipe0 auto-ACK）

* REG 0x02 EN\_RXADDR = 0x01（仅 pipe0）

* REG 0x03 SETUP\_AW = 0x03（5 字节地址）

* REG 0x04 SETUP\_RETR = 0x13（250us, 3 次重试）

* REG 0x05 RF\_CH = 100（2.500 GHz）

* REG 0x06 RF\_SETUP = 0x0E（2Mbps, 0dBm）

* REG 0x10 TX\_ADDR = 0xE7\*5

* REG 0x0A RX\_ADDR\_P0 = 0xE7\*5

* REG 0x11 RX\_PW\_P0 = 15

### 3. SPI 访问模式（FIFO 阻塞，不启用 DMA）

基于 16 字节 TX 负载 @4MHz 分析：

* SPI 传输时间：16\*8/4MHz = 32us/帧

* DMA 通道配置开销：10-20us（claim + config + start + wait）

* 主要瓶颈：nRF24 空中 ACK 等待 \~130us + CE 脉冲 20us = 150us

* DMA 收益：32us 中仅节省 CPU 等待，但配置开销抵消大部分收益

* 结论：使用 `PeripheralSPI::transfer()` 现有 FIFO 阻塞调用即可，代码简洁且实际 CPU 占用 < 5%

### 4. CE 引脚管理

* CE（GPIO0）是 nRF24 专用的"发射/接收使能"引脚，**不是 SPI CS**

* 在 `NRF24LinkAddon::setup()` 中用 `gpio_init/gpio_set_dir/gpio_put` 直接管理

* TX 模式：写 payload 后拉高 CE 20us 触发发射，然后拉低

* 不通过 `PeripheralSPI::select()` 管理（select 用于 CSN=GPIO1）

### 5. 主循环集成

* `available()`：仅当 `nrf24LinkEnabled` 为真时返回 true

* `setup()`：初始化 SPI0、CE 引脚、nRF24 寄存器

* `preprocess()`：空（与 uart\_link 一致）

* `process()`：空（无接收处理需求，发射端单向输出）

* `postprocess(sent)`：读取 `Gamepad::state`，按节流策略（数字键变化即发，模拟量 900us 间隔，50ms 心跳）调用 `writePacket()`

### 6. 互斥逻辑

新增 `nrf24LinkEnabled` 加入现有三互斥组（优先级：nRF24直连 > uart\_link nRF24 > 蓝牙 > USB验证器）。前端切换时自动关闭其他三个；后端 `webconfig.cpp` 和 `config_utils.cpp` 执行相同互斥校验。

## 实施步骤

### 步骤 1：BoardConfig.h 启用 SPI0 + 新增默认值宏

**文件**：`configs/HML2354/BoardConfig.h`

修改 L73-77 区域：

```cpp
// SPI0: nRF24 直连启用，RX=GPIO4, CS=GPIO1, SCK=GPIO2, TX=GPIO3, CE=GPIO0
#ifdef SPI0_ENABLED
#undef SPI0_ENABLED
#endif
#define SPI0_ENABLED 1
```

（其余 SPI0 引脚定义 L91-95 保持不变）

在 L228 附近（`DEFAULT_WIRELESS_LINK_ENABLED` 旁）新增：

```cpp
// --- nRF24 直连无线模式开关板级默认值为关闭（与无线连接/蓝牙三互斥）---
#define DEFAULT_NRF24_LINK_ENABLED 0
```

### 步骤 2：proto/config.proto 新增字段

**文件**：`proto/config.proto` L42 后新增：

```proto
optional bool nrf24LinkEnabled = 43;  // nRF24 直连无线模式开关（SPI0）；与 wirelessLinkEnabled/bluetoothLinkEnabled 三互斥
```

### 步骤 3：新增插件头文件

**文件**：`headers/addons/nrf24_link.h`（新建）

关键内容：

* `#include "gpaddon.h"` / `#include "peripheral_spi.h"` / `#include "BoardConfig.h"`

* `NRF24_LINK_ADDON_NAME "NRF24 Link"`

* 常量：`NRF24_HW_SPI_BLOCK = 0`、`NRF24_HW_CS_PIN = SPI0_PIN_CS`、`NRF24_HW_CE_PIN = SPI0_PIN_CE`、`NRF24_SPI_HZ = 4000000`

* nRF24 寄存器宏（CONFIG/EN\_AA/SETUP\_AW/SETUP\_RETR/RF\_CH/RF\_SETUP/RX\_PW\_P0 等）

* `NRF24_PAYLOAD = 15`、`NRF24_CHANNEL = 100`

* 节流常量：`NRF24_INPUT_ANALOG_MIN_US = 900`、`NRF24_INPUT_HEARTBEAT_US = 50000`

* `class NRF24LinkAddon : public GPAddon`，实现 `available/setup/preprocess/process/postprocess/name/reinit`

* 私有成员：`PeripheralSPI* spi_`、`SPIBaudrateProfile spiProfile_`、`int8_t csPin_`、`int8_t cePin_`、`bool initialized`、缓存的上次状态字段（buttons/dpad/lx/ly/rx/ry/lt/rt/lastSentUs/lastInputMode）

* 私有方法：`writeReg/writeRegBuf/readReg/flushTx/writePacket/cePulse/sendInputFrame`

### 步骤 4：新增插件源文件

**文件**：`src/addons/nrf24_link.cpp`（新建）

实现要点：

1. `available()`：读 `GamepadOptions.nrf24LinkEnabled`，为真返回 true
2. `setup()`：

   * `PeripheralSPI* spi = PeripheralManager::getInstance().getSPI(NRF24_HW_SPI_BLOCK);`

   * 检查 `spi->configured`，缓存 `spi_` 和 `spiProfile_ = spi_->makeBaudrateProfile(NRF24_SPI_HZ)`

   * `spi_->beginTransaction(spiProfile_, SPI_MSB_FIRST, SPI_MODE0)`

   * `gpio_init(cePin_); gpio_set_dir(cePin_, GPIO_OUT); gpio_put(cePin_, 0);`

   * 按设计决策 2 写入 nRF24 寄存器序列

   * `delay(2)` 等待 PowerUp+OscSettling
3. `writeReg(reg, val)`：`spi_->select(csPin_)` → `spi_->transfer(0x20|reg)` → `spi_->transfer(val)` → `spi_->deselect()`
4. `writeReg(reg, data, len)`：类似，循环 `transfer(data[i])`
5. `readReg(reg)`：`select` → `transfer(reg)` → `v=transfer(0)` → `deselect` → `return v`
6. `writePacket(data)`：参考 ESP32/nrf24.h L34-48 实现，含 STATUS 轮询和 MAX\_RT 处理
7. `sendInputFrame(buttons,dpad,lx,ly,rx,ry,lt,rt)`：按数据包格式表组装 15 字节，调用 `writePacket`
8. `postprocess(sent)`：参考 uart\_link.cpp L165-213 实现，读 `Storage::getInstance().GetProcessedGamepad()->state`，按节流策略调用 `sendInputFrame`
9. `process()` / `preprocess()`：空实现
10. `reinit()`：调用 `setup()` 重新初始化

### 步骤 5：CMakeLists.txt 注册源文件

**文件**：`CMakeLists.txt` L303 后新增：

```cmake
src/addons/nrf24_link.cpp
```

### 步骤 6：gp2040.cpp 注册插件

**文件**：`src/gp2040.cpp`

L1089（`UARTLinkAddon` 注册行）后新增：

```cpp
addons.LoadAddon(new NRF24LinkAddon());
```

并在文件顶部 addon 头文件包含区添加 `#include "addons/nrf24_link.h"`。

### 步骤 7：config\_utils.cpp 初始化默认值 + 互斥校验

**文件**：`src/config_utils.cpp`

L414 后新增：

```cpp
INIT_UNSET_PROPERTY(config.gamepadOptions, nrf24LinkEnabled, DEFAULT_NRF24_LINK_ENABLED);
```

L2042-2045 区域扩展三互斥为四互斥（优先级：nRF24直连 > uart\_link nRF24 > 蓝牙 > USB验证器）：

```cpp
if (config.gamepadOptions.nrf24LinkEnabled) {
    config.gamepadOptions.wirelessLinkEnabled = false;
    config.gamepadOptions.bluetoothLinkEnabled = false;
    peripheralOptions.blockUSB0.enabled = 0;
} else if (config.gamepadOptions.wirelessLinkEnabled) {
    config.gamepadOptions.bluetoothLinkEnabled = false;
    peripheralOptions.blockUSB0.enabled = 0;
} else if (config.gamepadOptions.bluetoothLinkEnabled) {
    peripheralOptions.blockUSB0.enabled = 0;
}
```

### 步骤 8：webconfig.cpp 读写 + 互斥校验

**文件**：`src/webconfig.cpp`

L1103 后新增读取：

```cpp
readDoc(gamepadOptions.nrf24LinkEnabled, doc, "nrf24LinkEnabled");
```

L1107-1112 三互斥扩展为四互斥（同步骤 7 逻辑）。

L1171 后新增写入：

```cpp
writeDoc(doc, "nrf24LinkEnabled", gamepadOptions.nrf24LinkEnabled ? 1 : 0);
```

### 步骤 9：前端 HardwareConfig.tsx 新增开关 + 互斥

**文件**：`www/src/Pages/HMLSettings/components/HardwareConfig.tsx`

L77 后新增 state：

```tsx
const [nrf24LinkEnabled, setNrf24LinkEnabled] = useState(0);
```

L170 后新增加载：

```tsx
setNrf24LinkEnabled(Number(gamepad?.nrf24LinkEnabled) ? 1 : 0);
```

L213-215 `gamepadToSave` 扩展：

```tsx
nrf24LinkEnabled: nrf24LinkEnabled ? 1 : 0,
```

在 L447（蓝牙开关之后）新增"无线模式"开关：

```tsx
{/* 无线模式开关（nRF24 直连 SPI0）*/}
<div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
    <Form.Check
        type="switch"
        id="nrf24-link-switch"
        label={t('SettingsPage:hml-nrf24-link-label')}
        checked={Boolean(nrf24LinkEnabled)}
        onChange={(e) => {
            const checked = e.target.checked ? 1 : 0;
            setNrf24LinkEnabled(checked);
            // 四互斥：开启 nRF24 直连时关闭 uart_link 无线、蓝牙、USB验证器
            if (checked) {
                setWirelessLinkEnabled(0);
                setBluetoothLinkEnabled(0);
                setPeripheralOptions((prev) => ({
                    ...prev,
                    peripheral: {
                        ...prev.peripheral,
                        usb0: { ...prev.peripheral?.usb0, enabled: 0 },
                    },
                }));
            }
        }}
    />
    <span className="text-muted">
        {t('SettingsPage:hml-nrf24-link-hint')}
    </span>
</div>
```

同时更新现有三个开关的 `onChange` 互斥逻辑：开启任一其他无线开关时同时 `setNrf24LinkEnabled(0)`。

### 步骤 10：前端 i18n 文案

**文件**：

* `www/src/Locales/zh-CN/SettingsPage.jsx`

* `www/src/Locales/en/SettingsPage.jsx`

* `www/src/Locales/ja-JP/SettingsPage.jsx`

在 `hml-bluetooth-link-hint` 后新增（以 zh-CN 为例）：

```jsx
'hml-nrf24-link-label': '无线模式',
'hml-nrf24-link-hint':
    '启用 nRF24 直连无线（SPI0），需在接收端插入配套 2.4G 接收器',
```

en：

```jsx
'hml-nrf24-link-label': 'Wireless Mode',
'hml-nrf24-link-hint':
    'Enable nRF24 direct wireless (SPI0). Requires a matching 2.4G receiver on the host',
```

ja-JP：

```jsx
'hml-nrf24-link-label': '無線モード',
'hml-nrf24-link-hint':
    'nRF24 直結無線（SPI0）を有効化。ホスト側に 2.4G レシーバーが必要',
```

## SPI FIFO / DMA 分析

### FIFO 使用情况

`PeripheralSPI::transfer(tx, rx, count)` 内部调用 Pico SDK 的 `spi_write_read_blocking()`，该函数已经使用 SPI 硬件 FIFO（RP2040/RP2354 的 SSP 外设各有 8 字节深度的 TX/RX FIFO）。FIFO 机制下：

* TX：CPU 写入 `HWREG(spi->dr)`，FIFO 满时 CPU 等待

* RX：CPU 从 `HWREG(spi->dr)` 读取，FIFO 空时 CPU 等待

* 对于 16 字节 TX 负载，前 8 字节可连续写入 FIFO（无需等待），剩余 8 字节在 FIFO 排空后继续写入

### DMA 可行性与收益分析

PeripheralSPI 已内置 DMA 基础设施（`_UseDMA` / `_dmaRxChannel` / `_dmaTxChannel`），但默认关闭且无公共 setter。如要启用 DMA：

**可行方案**：在 `peripheral_spi.h` 新增 `void setUseDMA(bool enable)` 公共方法，setup() 中根据该标志配置 DMA 通道，transfer() 改用 `dma_channel_start()` + `dma_channel_wait_for_finish_blocking()`。

**收益分析（基于 nRF24 实际负载）**：

| 项目            | FIFO 阻塞                    | DMA                           |
| ------------- | -------------------------- | ----------------------------- |
| 16B TX 负载传输时间 | 32us（4MHz）                 | 32us（4MHz，硬件相同）               |
| CPU 占用        | 32us 阻塞轮询                  | \~5us 配置 + 0us 等待（中断/\_yield） |
| 单次配置开销        | 0us                        | 10-20us（claim+config+start）   |
| nRF24 ACK 等待  | 130us（必须阻塞）                | 130us（必须阻塞，与 SPI 无关）          |
| 单帧总 CPU 占用    | \~32us（SPI）+ 150us（CE+ACK） | \~15us（DMA 配置）+ 150us（CE+ACK） |
| 净收益           | —                          | 约 17us/帧，1000Hz 下 1.7% CPU    |

**结论**：

1. nRF24 主要瓶颈是 130us 空中 ACK 等待，与 SPI 传输方式无关
2. DMA 净收益仅 17us/帧（1.7% CPU @1000Hz），但引入额外复杂度（DMA 通道占用、缓冲区管理、中断同步）
3. 当前 nRF24 单帧 16 字节负载下，FIFO 阻塞已足够高效
4. 若未来扩展为多包连续发送或增大 payload，可再启用 DMA

**最终决策**：使用 FIFO 阻塞（`PeripheralSPI::transfer`），不启用 DMA。在 `nrf24_link.h` 中保留注释说明 DMA 扩展点，便于未来升级。

## 验证步骤

1. **编译验证**：

   ```bash
   cmake --build build/HML2354 --target GP2040-CE-HML2354
   ```

   确认 `.uf2` 和 `.elf` 生成成功

2. **前端构建**：

   ```bash
   cd www && npm run build
   ```

   确认 Vite 构建退出码为 0

3. **proto 生成验证**：确认 `nrf24LinkEnabled` 字段已生成到 `config.pb.h` / `config.pb.c`

4. **功能验证（需硬件）**：

   * 烧录固件后，网页打开硬件配置页，确认"无线模式"开关出现在"蓝牙无线"下方

   * 开启"无线模式"，确认其他三个无线/USB开关自动关闭

   * 保存后重启，确认开关状态持久化

   * 接收端插入配套 nRF24 接收器，确认手柄输入可达接收端

## 假设与约束

* 假设接收端使用与 ESP32/nrf24.h 相同的 RF 配置（信道 100、2Mbps、5 字节地址 0xE7\*5、15 字节负载、auto-ACK）

* 假设接收端按本计划定义的 15 字节 payload 格式解析（13B 游戏pad状态 + inputMode + reserved）

* `nrf24LinkEnabled` 默认关闭，不影响现有 uart\_link 用户

* nRF24 直连与 uart\_link 不应同时开启（三互斥已强制保证）

* 本计划不修改 ESP32 端代码（ESP32 侧 nRF24 驱动仅供参考）

* 本计划不修改 uart\_link.cpp / uart\_link.h（保留现有无线插件不动）

