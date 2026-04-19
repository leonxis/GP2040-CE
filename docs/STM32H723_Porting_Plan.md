# GP2040-CE 移植 STM32H723 工作规划方案

本文档基于前期移植难度分析与简化条件讨论，为将 GP2040-CE 移植至 STM32H723 制定分阶段工作规划，作为后续移植实施的步骤依据。

---

## 一、移植范围与约束

### 1.1 目标平台

| 项目 | 说明 |
|------|------|
| MCU | STM32H723（或同系列双 USB 控制器型号） |
| 内核 | ARM Cortex-M7，单核 |
| USB | 双控制器：USB1 = Device（手柄），USB2 = Host（仅接认证 dongle） |
| 存储 | 片内 Flash **仅 1MB**，用于程序与配置区；可选外部 Flash 按需 |

**Flash 约束**：STM32H723 片内 Flash 为 1MB，而当前带**内置网页服务器**（RNDIS + lwIP + httpd + `www/` 前端资源）的固件体积在 **2MB 以上**，无法直接放入 1MB。因此**必须先完成 WebHID 前后端分离与固件瘦身**（在现有 RP2040 工程上实施），使固件体积降至 1MB 以内，再推进 STM32 工程与后续移植阶段。

### 1.2 功能范围（约定）

- **保留**：手柄输入（GPIO/ADC 等）、多模式协议（XInput / Switch / PS3/4/5 / DInput 等）、SOCD、映射与配置、**与 USB 验证器 dongle 的认证通信**。
- **保留并明确**：**USB 报告率**需具备 **8K 回报率能力**（即最高 8000 Hz）；并支持在前端（WebHID 配置界面）中**可配置切换 2K、4K、8K** 三种回报率，配置写入设备后生效（可要求重启或热生效，按实现而定）。
- **取消**：双核；Core1 上的 LED（NeoPico）、显示、震动、键盘 Host；通用 USB Host（仅保留 dongle 认证用 Host）；启动模式与网页配置；NeoPico、Bootsel 按钮。
- **替换**：配置方式由内置 Web 服务器改为 **WebHID**（PC 端通过 HID 读写配置）。

### 1.3 技术原则

- 使用 **条件编译**（如 `PLATFORM_STM32` / `PLATFORM_PICO`）在现有代码中替换 Pico 专用 API（GPIO、ADC、PWM、时间），避免大规模重写业务逻辑。
- 单核主循环合并原 Core0 与认证相关逻辑，**不引入多核锁与看门狗启动模式**。
- USB Device 与 USB Host 在硬件上分离（双 USB），便于调试与维护。

### 1.4 USB 栈选型：Device 用 TinyUSB，Host 用 ST 官方库

| 角色 | 选型 | 理由 |
|------|------|------|
| **USB Device（手柄）** | **TinyUSB** | 现有描述符、XInput/Switch/PS4/5/HID/DInput 等类与驱动层均基于 TinyUSB；TinyUSB 提供官方 **STM32 DCD**，仅换底层即可复用整条设备栈。若改用 ST 的 Device 库，需重写描述符格式、端点与传输 API 及所有类逻辑，工作量大且易引入兼容性问题。 |
| **USB Host（验证器 dongle）** | **ST 官方 USB Host 中间件** | TinyUSB 对 STM32 的 **HCD（Host 控制器驱动）** 支持有限或需自行移植/维护；ST 的 USB Host 库针对自家 H7 等芯片有完整支持与合规测试。Host 侧仅需枚举一个 HID/XInput 设备并收发 report，**写一层薄适配**：在 ST 的 HID 枚举/report 回调中调用现有 `USBHostManager::*_cb()`（如 `hid_report_received_cb`、`hid_set_report_complete_cb`），即可驱动现有 Auth listener，协议与状态机无需改动。 |

因此移植计划中**不采用「TinyUSB 同时做 Device 与 Host」**，而是：

- **Device**：继续使用 TinyUSB + STM32 DCD。
- **Host**：使用 **STM32 平台自身 USB Host 库**（STM32Cube 等），通过适配层对接 `USBHostManager` 与现有 Auth listener。

### 1.5 USB 报告率（2K / 4K / 8K）

- **适用范围**：**仅针对 USB Device（手柄）**。验证器所用的 **USB Host 不需要实现高回报率**，仅与 dongle 做认证通信，按常规 HID 轮询即可。
- **目标**：USB Device（手柄）具备 **8K 回报率能力**，且用户可在**前端配置**中切换 **2K、4K、8K**。
- **实现要点**：2K/4K/8K 需使用 **USB 高速（High-Speed）**；全速（Full-Speed）下最小间隔为 1ms，最高仅 1000Hz。在 USB HS 下，端点 bInterval 以**微帧**（125µs）为单位：bInterval=1 → 125µs → **8K**，bInterval=2 → 250µs → **4K**，bInterval=4 → 500µs → **2K**。设备端需：
  - 使用 STM32 的 **USB OTG HS**（或支持 HS 的实例）作为手柄 Device；
  - 在描述符或运行时根据配置项「报告率」设置对应 bInterval（或等效策略）；
  - 主循环与 `tud_task()` 调用频率需能跟上所选回报率（例如 8K 时每 125µs 可上报一次）。
- **配置**：在 WebHID 前端（阶段 P 的 PC 端配置工具/页面）中增加「报告率」或「轮询率」设置项，选项为 2K / 4K / 8K，写入设备配置存储；设备启动或切换时按该配置生效。

### 1.6 插件（addon）移植要求

GP2040-CE 的插件（addon）分为在 **Core0 主循环**加载的输入/逻辑类，与在 **Core1** 加载的显示/LED/震动等。STM32 单核且取消 Core1，要求如下：

- **不移植、不参与 STM32 编译的插件**
  - **Core1 用**：Display、NeoPico（neopicoleds）、PlayerLED、BoardLed、Buzzer、DRV8833Rumble、ReactiveLED；对应库 NeoPico 不链接。
  - **与 Host/启动相关**：BootselButton、KeyboardHost、GamepadUSBHost（及 GamepadUSBHostListener）。认证仍通过 USB Host + Auth listener 完成，但不保留「键盘 Host」「第三方手柄 Host」插件。
  - **约定不移植的 Core0 插件**：HETriggerAddon、FocusModeAddon、I2CAnalog1219Input、SPIAnalog1256Input、PCF8575Addon、DualDirectionalInput、RotaryEncoderInput。
- **需移植或条件编译保留的插件（原 Core0 addon）**
  - 输入与逻辑类（在 `GP2040::setup()` 中加载、主循环 Preprocess/Process/Postprocess 中执行）：AnalogInput、MCP3208ADCAddon、LSM6DSRIMUAddon、LinearTriggerAddon、TwoKeyTouchpadAddon、WiiExtensionInput、SNESpadInput、SliderSOCDInput、TiltInput、TG16padInput、ReverseInput、TurboInput、InputMacro。
  - 移植时：将其中对 `hardware/*`、`pico/*` 的引用改为平台层或 `#ifdef PLATFORM_STM32` 实现；若某插件依赖 Pico 专有外设（如 PIO）或不再需要，可改为条件编译不加载。
- **通用要求**
  - 所有**保留的 addon** 与 driver 需将 `#include "hardware/..."` 等改为平台抽象层或条件编译，以便在 STM32 上编译通过。
  - 不在「不移植」列表中的 addon，按需保留或按功能裁剪；新增 addon 时需同时考虑 Pico 与 STM32 的条件编译。

---

## 二、阶段划分总览

| 阶段 | 名称 | 主要产出 | 依赖 |
|------|------|----------|------|
| **P** | **WebHID 前后端分离与固件瘦身** | 固件 &lt;1MB、HID 配置接口、独立前端/工具 | **—（优先完成）** |
| 0 | 环境与工程骨架 | STM32 工程可编译、可烧录、可运行 | 阶段 P |
| 1 | 平台抽象层 | GPIO/ADC/PWM/时间 条件编译与 STM32 实现 | 阶段 0 |
| 2 | 存储与系统 | FlashPROM（STM32 Flash）、System 简化（无 BootMode） | 阶段 1 |
| 3 | 单核主循环与裁剪 | 单 main 循环、移除 Core1/addon 与 RNDIS/Web | 阶段 1 |
| 4 | USB Device（手柄） | TinyUSB Device + STM32 DCD，一种以上模式可用 | 阶段 2、3 |
| 5 | USB Host（验证器） | 第二 USB 作 Host、对接现有 Auth listener | 阶段 3、4 |
| 6 | WebHID 在 STM32 上验证 | 在阶段 P 基础上验证 HID 配置接口与前端 | 阶段 4 |
| 7 | 联调与收尾 | 多模式与 dongle 认证联调、文档与发布清单 | 阶段 5、6 |

**说明**：阶段 P 在**现有 RP2040 代码库**上完成，不依赖 STM32；完成后固件体积需 &lt;1MB，方可推进阶段 0 及后续。

---

## 三、阶段 P：WebHID 前后端分离与固件瘦身（前置，在 RP2040 上完成）

### 3.1 目标

- 将配置功能从「内置网页服务器（RNDIS + lwIP + httpd + 内嵌 `www/` 前端）」改为 **WebHID 前后端分离**：设备端仅提供 HID 配置接口，配置界面在 PC 端独立运行（浏览器 WebHID 或桌面工具）。
- **固件瘦身**：移除 RNDIS、lwIP、httpd 及 `www/` 编译产物后，固件体积降至 **1MB 以内**，满足 STM32H723 片内 Flash 限制，以便后续移植推进。

### 3.2 任务

1. **设备端：HID 配置接口**
   - 在现有 USB Device 描述符中增加「配置用」HID 接口（或专用 report id），与游戏手柄报告区分。
   - 定义配置协议：读/写命令、与现有 `config.pb` 或二进制块兼容的格式、可选 CRC/长度校验。
   - 在 TinyUSB HID 层处理 set_report/get_report（配置用 report id），内部调用 Storage/ConfigUtils 的 load/save 与 FlashPROM commit；与主循环访问互斥（单核下关中断或临界区）。

2. **固件侧：移除内置 Web 与网络栈**
   - 通过编译选项或条件编译**移除**：RNDIS、lwIP、httpd、`www/` 前端资源链接进固件的逻辑；`rndis_task()`、`NetDriver`、Web 配置模式入口（如 `ENTER_WEBCONFIG_MODE`、从启动进入网页模式的逻辑）。
   - 保留：根据存储配置选择输入模式、单一模式启动；进入配置的方式改为「由 PC 通过 WebHID 连接并读写配置」，不再通过「设备启动为 RNDIS + 浏览器打开内置页」。
   - 确认移除后 Pico 固件可正常编译，且体积 &lt;1MB（建议先以 `-DSKIP_WEBBUILD=1` 或等效方式不链接 www 资源，再彻底去掉 RNDIS/lwIP/httpd）。

3. **PC 端：独立配置前端/工具**
   - 提供独立于固件的配置界面：**WebHID 网页**（浏览器）或**桌面 HID 工具**，通过 WebHID API 枚举设备、选择配置接口、发送读/写 report，解析并展示/编辑配置（与现有 config 结构兼容）。
   - **前端需包含「报告率」设置**：用户可切换 **2K、4K、8K** 三种选项，写入设备配置存储；设备读取该配置后按 1.5 与阶段 4 实现生效（重启或热生效由实现决定）。
   - 文档：WebHID 使用步骤、report 格式与命令字、固件构建选项（无 Web 的构建方式）；报告率选项说明（2K/4K/8K 含义与生效方式）。

4. **验收与基线**
   - 在 RP2040 上构建「无 RNDIS/无 www」的固件，测量体积并记录为基线；确认 &lt;1MB。
   - 使用 PC 端 WebHID 工具/页面完成一次完整「读配置 → 修改 → 写回 → 重启生效」流程，确认功能等价于原网页配置（在保留的配置项范围内）。

### 3.3 交付物

- [x] 设备端 HID 配置接口与协议实现（可在现有 Pico 分支上合并或通过编译选项启用）。
- [x] 固件构建选项或分支：无 RNDIS、无 lwIP、无 httpd、不链接 `www/`，产出固件体积 **&lt;1MB**。
- [x] PC 端 WebHID 配置工具或网页 + 使用与协议文档。
- [x] **前端配置界面包含「报告率」设置项**：可选 2K、4K、8K，写入设备配置；文档说明含义与生效方式（为阶段 4 设备端实现 8K 能力及 2K/4K/8K 可配置提供配置来源）。
- [x] 验收报告：固件体积、配置读写流程通过。

---

## 四、阶段 0：环境与工程骨架

### 4.1 目标

- 建立可在本机与 CI 下编译的 STM32H723 工程。
- 实现最小可运行程序（如 LED 或串口），确认烧录与运行正常。

### 4.2 任务

1. **创建 STM32 工程目录**
   - 建议路径：`platform/stm32h723/` 或根目录下 `stm32/`，与现有 `configs/`、`src/` 并列，便于条件编译包含。
   - 包含：CMakeLists.txt、链接脚本（.ld）、启动文件（.s）、HAL/LL 或 CubeMX 生成代码的引用路径。

2. **工具链与依赖**
   - 使用 ARM GCC（arm-none-eabi-gcc）与 CMake；可选 CubeMX 生成初始化代码，仅保留必要外设（GPIO、ADC、TIM、USB、Flash）。
   - 确定 Pico 与 STM32 的切换方式：通过 CMake 选项（如 `-DPLATFORM=STM32H723`）或编译定义，使顶层 CMakeLists 可择一编译 Pico 或 STM32 目标。

3. **最小可运行验证**
   - 主函数仅做 GPIO 翻转或串口输出，确认时钟、烧录、运行正常。
   - 记录 Flash/RAM 占用与启动时间，供后续对比。

### 4.3 交付物

- [x] `platform/stm32h723/`（或等价）下可编译、可烧录的工程。
- [x] 顶层 CMake 或脚本支持 `-DPLATFORM=STM32H723` 并生成 stm32 目标。
- [x] 简要说明：如何配置工具链、烧录方式、推荐 IDE/调试方式。

---

## 五、阶段 1：平台抽象层（GPIO / ADC / PWM / 时间）

### 5.1 目标

- 将 Pico 专用的 `hardware/gpio.h`、`hardware/adc.h`、`hardware/pwm.h`、`pico/time.h` 等通过条件编译替换为 STM32 实现，使上层业务仅依赖统一接口或宏。

### 5.2 任务

1. **定义平台宏**
   - 在 CMake 中根据 `PLATFORM` 定义 `PLATFORM_PICO` 或 `PLATFORM_STM32`（及可选 `PLATFORM_STM32H723`）。
   - 确保 `src/` 与需参与编译的 `lib/` 在 STM32 构建下包含平台头文件。

2. **GPIO 抽象**
   - 新增 `platform/gpio.h`（或 `platform/stm32/gpio.h`）：声明 `gpio_init`、`gpio_set_dir`、`gpio_get`、`gpio_put`、`gpio_pull_up`、`gpio_get_all`、`gpio_deinit`、`gpio_set_function`、`gpio_get_function`、`gpio_is_dir_out` 等与 Pico 兼容的 API 或宏。
   - STM32 实现：内部映射到 HAL_GPIO_* 或 LL 库，引脚号统一为「端口+引脚」编码或与 BoardConfig 约定一致。
   - 在 `src/` 及 `lib/`（OneBitDisplay、SNESpad、PicoPeripherals 等）中，将 `#include "hardware/gpio.h"` 改为条件包含平台头；或由 `platform/gpio.h` 在 STM32 下包含 STM32 实现。

3. **ADC 抽象**
   - 提供 `adc_init`、`adc_gpio_init`、`adc_select_input`、`adc_read` 等与 Pico 语义相近的接口；STM32 侧使用 HAL_ADC 或 LL，通道与 BoardConfig 中引脚对应。

4. **PWM 抽象**
   - 提供 `pwm_set_gpio_level`、`pwm_gpio_to_slice_num`、`pwm_gpio_to_channel`、`pwm_set_wrap`、`pwm_set_enabled`、`gpio_set_function(..., GPIO_FUNC_PWM)` 等；STM32 使用 TIM + PWM 输出，引脚与定时器通道映射需在 BoardConfig 或表中配置。

5. **时间与睡眠抽象（DWT 方案）**
   - **Pico 语义**：`get_absolute_time()` 返回 64 位微秒计数值（`absolute_time_t`）；`to_ms_since_boot(t)`/`to_us_since_boot(t)` 转为 ms/us；`sleep_us`/`sleep_ms` 阻塞延时；配套 `nil_time`、`is_nil_time(t)`、`time_reached(t)`、`make_timeout_time_ms(ms)`、`make_timeout_time_us(us)` 用于超时判断。
   - **本仓库使用**：`getMillis()`/`getMicro()`（gamepad.cpp）为全工程统一入口；驱动与认证（PS4/PS5/XInput/XBOne/P5/SwitchPro 等）用“当前时间”与超时；`sleep_*` 用于 USB Host 初始化、XBOne/tg16/ADS1256 等短延时。**不需要**实现 Pico 的 alarm 池（`add_alarm_in_ms` 仅 FlashPROM 用，STM32 FlashPROM 用独立延迟写入）。
   - **推荐实现**：用 **STM32 DWT 周期计数器（CYCCNT）** 扩展为 64 位微秒基准（CYCCNT 32 位约 10.7s 溢出，需结合 SysTick 或周期累加扩展），在此基础上实现下表 API；`sleep_us` 用 DWT 忙等，`sleep_ms` 短时可用 DWT、长时用 HAL_Delay/SysTick 避免阻塞 USB。

   | 原 Pico API | STM32 实现要点 |
   |-------------|----------------|
   | `get_absolute_time()` | 返回 `absolute_time_t`（typedef `uint64_t`），值 = `get_us_since_boot()` |
   | `to_ms_since_boot(t)` | `(uint32_t)((t) / 1000)` |
   | `to_us_since_boot(t)` | `(uint64_t)(t)` |
   | `getMillis()` / `getMicro()` | 同上或直接调用 `get_ms_since_boot()` / `get_us_since_boot()` |
   | `sleep_us(us)` | DWT 忙等：循环直到 `(DWT->CYCCNT - start) >= us * cycles_per_us`（溢出时差值仍正确） |
   | `sleep_ms(ms)` | 短延时 DWT 忙等；长延时用 HAL_Delay/SysTick |
   | `nil_time` | `((absolute_time_t)0)` 或与 Pico 一致哨兵值 |
   | `is_nil_time(t)` | `((t) == nil_time)` |
   | `time_reached(t)` | `(get_absolute_time() >= (t))` |
   | `make_timeout_time_ms(ms)` | `(get_absolute_time() + (uint64_t)(ms) * 1000)` |
   | `make_timeout_time_us(us)` | `(get_absolute_time() + (uint64_t)(us))` |

   **实现注意**：启动阶段使能 DWT 与 CYCCNT（CoreDebug、DWT 控制位）；64 位 us 需用 SysTick 或溢出累加避免 32 位 CYCCNT 约 10s 溢出；长时间 `sleep_ms` 可能阻塞主循环，优先短延时 + 状态机。详见本文档**附录 A：时间/睡眠 API 使用统计与 DWT 要点**（与 5.2 节第 5 条一致）。

6. **其他 Pico 依赖**
   - `pico/stdlib.h`：在 STM32 下提供最小实现（如 `sleep_ms`、常用类型）或条件编译掉不需要的引用。
   - `pico/rand.h`：STM32 可用 HAL RNG 或简单 LCG 实现。
   - `pico/platform.h`：用空宏或平台相关定义替代。

### 5.3 交付物

- [x] `platform/` 下统一头文件及 STM32 实现，与现有 `#ifdef PLATFORM_PICO` 分支并存。
- [x] 所有 `src/` 中直接引用 `hardware/*`、`pico/time.h` 等处改为通过平台层或条件编译后，STM32 目标能通过编译（可先不链接业务，仅验证平台层编译与链接）。
- [x] 文档：GPIO/ADC/PWM 引脚与 STM32 外设的对应关系（或指向 BoardConfig 说明）。
- [x] 时间层：按 5.2 节第 5 条及附录 A 实现 DWT 时间与睡眠 API。

---

## 六、阶段 2：存储与系统

### 6.1 目标

- 在 STM32 上实现与当前 EEPROM 语义兼容的配置存储（FlashPROM）。
- 简化 System：去掉 BootMode、watchdog scratch、多核锁；保留重启（可选）与 Flash/堆信息查询（若仍被使用）。

### 6.2 任务

1. **FlashPROM 移植**
   - 新建 `lib/FlashPROM/platform_stm32.cpp`（或条件编译块）：实现 `start()`、`commit()`、`reset()`。
   - 存储区：使用 STM32 片内 Flash 末尾固定扇区（或专用 Bank），大小与现有 `EEPROM_SIZE_BYTES` 一致；擦写按扇区，写入前读-改-写或双缓冲，避免频繁擦除。
   - 延迟写入：用 SysTick 或 HAL 定时器实现“延迟 N ms 后写 Flash”，取消 `multicore_lockout_*`、`spin_lock`；单核下写 Flash 时关中断或短临界区即可。
   - `EEPROM_ADDRESS_START` 在 STM32 上改为 Flash 存储区首地址（或抽象为 `getStorageBase()`）。

2. **System 移植**
   - `system.cpp`：`getTotalFlash`/`getUsedFlash` 改为基于 STM32 链接脚本与 Flash 布局；`getTotalHeap`/`getUsedHeap` 使用 `sbrk` 或 CMSIS 堆查询（若使用 heap_4 等则对应 API）。
   - `reboot()`：仅实现 NVIC_SystemReset()；删除对 `BootMode` 的写入。
   - 删除或条件编译 `takeBootMode()`、`watchdog_hw->scratch[5]`、watchdog_reboot 等；不再设置「下次启动模式」。

3. **Storagemanager 与 ConfigUtils**
   - 确认 `Storage::init()`、`save()`、`ConfigUtils::load/save` 仅依赖 FlashPROM 与 System 的抽象；必要时对 Flash 地址或擦写粒度做小范围适配。

### 6.3 交付物

- [x] STM32 下 FlashPROM 可正常 load/save 配置，重启后配置保持。
- [x] System 接口在 STM32 下编译通过且行为符合预期（无 BootMode 依赖）。
- [x] 文档：Flash 分区说明（程序区 / 配置区 / 边界）。

---

## 七、阶段 3：单核主循环与功能裁剪

### 7.1 目标

- 移除双核与 Core1 相关代码；将认证逻辑并入单核主循环。
- 删除网页配置、RNDIS、Bootsel、NeoPico 及不需要的 addon；为 WebHID 配置预留接口入口（设备端 HID 配置接口已在阶段 P 完成）。

### 7.2 任务

1. **主循环与入口**
   - `main.cpp`：删除 `multicore_launch_core1`、`core1()`；仅保留一个 `GP2040` 实例，在 `setup()` 后进入单一 `run()` 循环。
   - 在 `GP2040::run()` 中按顺序执行（示例）：
     - `USBHostManager::process()`（即 Host 轮询，见阶段 5）；
     - 输入采样与 addon 的 Preprocess；
     - `gamepad->process()`、热键与模式处理；
     - `inputDriver->process(gamepad)`；
     - `inputDriver->processAux()`（内部调用各 Auth 的 `process()`）；
     - addon 的 Process / Postprocess；
     - 驱动上报（USB Device 侧由 TinyUSB 在轮询中处理）。
   - 不再需要「Core1 ready」同步与多核锁。

2. **认证 listener 注册**
   - 原在 `GP2040Aux::setup()` 中：`inputDriver->get_usb_auth_listener()` 并 `USBHostManager::pushListener(listener)`。
   - 改为在 `GP2040::setup()` 末尾、在 `USBHostManager::start()` 之前，根据当前 `inputDriver` 调用 `get_usb_auth_listener()` 并 push；仅注册 Auth 类 listener，不注册 Keyboard / GamepadUSBHost。

3. **删除或条件编译的模块**
   - 移除/条件编译：`gp2040aux.cpp`；**不移植的插件与库**见 **1.6**（Core1 用 addon、BootselButton、KeyboardHost、GamepadUSBHost、NeoPico 库等）。
   - RNDIS / lwIP / httpd：删除对 `rndis.h`、`rndis_task()`、`NetDriver`、Web 服务器与 `www/` 的依赖；删除或条件编译 `ENTER_WEBCONFIG_MODE`、`ENTER_USB_MODE`、`BootAction::ENTER_WEBCONFIG_MODE` 等分支。
   - 启动逻辑：仅根据存储中的配置选择输入模式，固定以「游戏手柄模式」启动，无「进 Web 配置」的按键或模式切换。

4. **CMake 与链接**
   - STM32 目标下不编译上述已删除/条件编译的源文件；不链接 lwIP、RNDIS、NeoPico、pico_pio_usb 等；保留 CRC32、ArduinoJson、nanopb、FlashPROM、驱动与 Auth 相关源文件。

### 7.3 交付物

- [x] 单核主循环可编译、可运行；无 multicore、无 RNDIS、无 Web、无 NeoPico/Bootsel。
- [x] 清单：已移除或条件编译的文件/符号列表，便于后续回溯。

---

## 八、阶段 4：USB Device（手柄）

### 8.1 目标

- 使用 STM32 的**第一个 USB 控制器**作为 USB Device，实现至少一种输入模式（如 XInput 或 HID）与主机通信，验证按键/摇杆上报正常；并实现 **8K 回报率能力**及 **2K/4K/8K 可配置**（见 1.5）。**按 1.4 选型：Device 保持 TinyUSB，不采用 ST Device 库**，以复用现有描述符与类逻辑。

### 8.2 任务

1. **TinyUSB Device + STM32 DCD**
   - 确认 TinyUSB 中 STM32H7 的 DCD 支持 **USB OTG HS**（High-Speed）；手柄 Device 使用 HS 以实现 2K/4K/8K 回报率（全速 FS 最高仅 1000Hz）。
   - 在 STM32 工程中集成 TinyUSB device 栈：`tusb.c`、`device/*`、现有描述符与类实现（XInput、Switch、PS4/5、HID、DInput 等）保持不变，仅替换底层 DCD 为 TinyUSB 自带的 STM32 实现。
   - 实现 `tud_task()` 的调用时机：在主循环中**高频率**调用（或放在 USB 中断/任务中），使实际上报间隔能满足所选报告率（8K 时每 125µs 可上报一次）。

2. **报告率 2K / 4K / 8K 可配置**
   - 在配置存储中增加「报告率」或「轮询率」项，取值：2K、4K、8K（或等效枚举）。
   - 设备端根据当前配置设置 USB 端点 bInterval（HS 下微帧单位）：8K → bInterval=1（125µs），4K → bInterval=2（250µs），2K → bInterval=4（500µs）。若描述符在枚举时固定，则需在**运行时**或**重新连接/重启**时按配置生成或切换描述符/端点参数（视 TinyUSB 与 H7 DCD 支持情况）。
   - 主循环采样与上报节奏需与所选报告率一致（例如 8K 时每 125µs 执行一次输入采样并触发上报，若由主机轮询则保证 bInterval 与主机轮询间隔一致）。

3. **时钟与引脚**
   - 配置 USB 时钟与 GPIO（DP/DM）；与 BoardConfig 中「USB Device 引脚」一致。使用 HS 时需正确配置 ULPI 或内置 PHY（若 H723 为内置 HS PHY）。
   - 确保 USB 时钟与系统时钟满足所选最高报告率（8K）的时序要求。

4. **多模式描述符与驱动**
   - 保留现有各模式描述符与 `DriverManager` 切换逻辑；在 STM32 上先验证一种模式，再逐步打开其余模式；各模式下的报告率配置均需生效。

### 8.3 交付物

- [x] STM32 作为 USB 手柄被 PC/主机识别，至少一种模式（如 XInput）可正常识别并响应输入。
- [x] **USB 具备 8K 回报率能力**；设备端支持从配置中读取 2K/4K/8K 设置并正确设置 bInterval（或等效）与主循环节奏。
- [x] 文档：所用 USB 实例（建议 HS）、时钟源、DCD 文件清单；报告率配置项与 bInterval 对应关系。

---

## 九、阶段 5：USB Host（验证器 dongle）

### 9.1 目标

- 使用 STM32 的**第二个 USB 控制器**作为 USB Host，仅连接认证 dongle；将现有 Auth listener（XInput/PS4/PS5/XBOne/P5General 等）与 STM32 Host 栈对接，实现认证流程不变。
- **不要求高回报率**：验证器 Host 仅用于与 dongle 的认证通信（枚举、set_report/get_report 等），**不需要**实现 2K/4K/8K 等高报告率；按 ST Host 库默认或常规 HID 轮询间隔即可。

### 9.2 任务

1. **Host 控制器与栈（按 1.4 选型：采用 ST 官方 USB Host 库）**
   - 使用 **ST 官方 USB Host 中间件**（STM32Cube 中 H7 的 USB Host 库）：完成第二 USB 控制器的 Host 初始化、设备枚举、HID/XInput 类 report 收发。
   - **适配层**：在 ST 库的 HID 枚举成功、收到 report、set_report/get_report 完成等回调里，调用现有 `USBHostManager::*_cb()`（如 `hid_report_received_cb`、`hid_set_report_complete_cb`、`xinput_*_cb`），使 `usbhostmanager.cpp` 与各 Auth listener（XInputAuthUSBListener、PS4AuthUSBListener 等）的接口不变，仅底层由 `tuh_task()`/PIO 换为 ST Host 库的轮询或中断驱动。
   - 不采用 TinyUSB Host + STM32 HCD：TinyUSB 对 STM32 的 HCD 支持不完整或需自维护，ST 平台库在自家芯片上更成熟、合规性更好。
   - 工程中只编译「dongle 用」的 Host 栈与 HID/XInput 类逻辑，不包含键盘/其他 HID 设备逻辑。

2. **USBHostManager 与 PeripheralManager**
   - `USBHostManager::start()`：在 STM32 上改为初始化 **ST USB Host 库**（第二 USB 控制器），并注册适配层回调；不再调用 `tuh_init`/PIO 配置。
   - `USBHostManager::process()`：在 STM32 上改为调用 ST Host 库的轮询接口（或由 ST 库在中断中处理，process 仅做必要状态同步），不再调用 `tuh_task()`。
   - `PeripheralManager`：STM32 下可简化或移除「USB block 配置」（DP/5V 等），仅保留「是否启用 Host」的标志（若仍由配置项决定是否启用 dongle）。

3. **XInput Host 与 HID Host 回调**
   - 保留 `drivers/shared/xinput_host.cpp` 及现有 HID host 回调到 `USBHostManager` 的转发；确保 STM32 Host 栈在枚举到 XInput 或 HID 设备时，能按相同接口调用 `xinput_*_cb` 与 `hid_*_cb`，使 XInputAuthUSBListener、PS4AuthUSBListener 等无需改协议逻辑。

4. **测试**
   - 连接真实认证 dongle，在对应模式（如 XInput 或 PS4）下验证认证流程完成、手柄可正常使用。

### 9.3 交付物

- [x] 第二 USB 作为 Host 可枚举 dongle，并完成至少一种模式的认证（如 XInput 或 PS4）。
- [x] 文档：Host 栈选型、第二 USB 引脚与时钟、回调对接说明。

---

## 十、阶段 6：WebHID 在 STM32 上验证

### 10.1 目标

- 在**阶段 P 已完成**的 WebHID 前后端分离与设备端 HID 配置接口基础上，于 STM32 固件中验证：HID 配置接口与协议、PC 端 WebHID 工具/页面可正常连接并读写配置。内置 Web 与 RNDIS 已在阶段 P 移除，本阶段不涉及。

### 10.2 任务

1. **设备端**
   - 沿用阶段 P 在 RP2040 上实现的 HID 配置接口与协议；在 STM32 构建中确保该接口与 TinyUSB Device 一并编译并正确响应 set_report/get_report。

2. **验证**
   - 使用阶段 P 产出的 PC 端 WebHID 工具/页面连接 STM32 固件，完成读配置、修改、写回、重启生效的完整流程。
   - **报告率**：在前端中切换 2K、4K、8K 并写回设备，验证设备实际上报间隔或主机侧观测到的报告率符合设定（可选：用工具或主机测速验证 2K/4K/8K）。
   - 若有 STM32 特有配置或存储差异，在协议允许范围内做最小适配并更新文档。

3. **文档**
   - 确认 WebHID 使用步骤、report 格式与命令字在 STM32 上仍适用；若有差异则补充说明。

### 10.3 交付物

- [x] STM32 固件可通过 HID 配置接口被 PC 端 WebHID 工具识别并完成配置读写；重启后配置生效。
- [x] 文档（含与阶段 P 的差异说明，若有）。

---

## 十一、阶段 7：联调与收尾

### 11.1 目标

- 多输入模式与多认证模式联调；整理文档与发布清单；性能与稳定性检查。

### 11.2 任务

1. **模式与认证**
   - 逐项验证：XInput、Switch、PS3/4/5、DInput、键盘模式等（在保留范围内）在 STM32 上工作正常。
   - 对依赖 dongle 的模式（XInput/XBOne/PS4/PS5/P5General）做完整认证流程与长时间连接测试。

2. **性能与资源**
   - 测量轮询周期、输入延迟、**USB 报告率**：在 2K/4K/8K 各档位下验证实际报告率达标（如 8K 时约 8000 Hz，4K 时约 4000 Hz，2K 时约 2000 Hz）；确认 Flash/RAM 占用在目标范围内。
   - 若有未使用的功能（如原 Core1 的 LED/显示），确认已通过条件编译完全关闭，无残留引用。

3. **文档与发布**
   - 更新 README 或新增 `docs/STM32H723_Build_And_Flash.md`：构建命令、依赖、烧录方法、推荐硬件连接（USB1/USB2 用途）。
   - 发布清单：支持的板型（STM32H723 参考板）、支持的模式、已知限制、WebHID 配置说明。

### 11.3 交付物

- [x] 多模式与 dongle 认证联调通过，无已知严重缺陷。
- [x] 构建与烧录文档、WebHID 配置文档、发布清单。

---

## 十二、文件与模块清单（参考）

### 12.1 新增或平台目录

| 路径 | 说明 |
|------|------|
| `platform/` 或 `platform/stm32h723/` | CMake、链接脚本、启动文件、HAL 引用 |
| `platform/gpio.h`、`platform/adc.h`、`platform/pwm.h`、`platform/time.h`（含 DWT 时间/睡眠） | 平台抽象头与 STM32 实现 |
| `lib/FlashPROM/platform_stm32.cpp` 或等效 | STM32 FlashPROM 实现 |
| `docs/STM32H723_Porting_Plan.md` | 本文档 |
| `docs/STM32H723_Build_And_Flash.md` | 阶段 7 产出 |

### 12.2 需条件编译或修改的现有文件（示例）

| 文件 | 修改要点 |
|------|----------|
| `src/main.cpp` | 删除 multicore，单 GP2040 + 单 run 循环 |
| `src/gp2040.cpp` | 合并 Auth listener 注册；删除 RNDIS、BootMode、reset_usb_boot 等分支；条件编译 Pico 专用 API |
| `src/gp2040aux.cpp` | 仅 Pico 编译；STM32 不参与编译 |
| `src/system.cpp` | STM32 实现 Flash/Heap/reboot；删除 BootMode、watchdog scratch |
| `src/usbhostmanager.cpp` | STM32 下 start() 初始化第二 USB Host，不调用 pio_usb |
| `src/storagemanager.cpp` | 条件编译 watchdog_reboot（STM32 用 NVIC_SystemReset） |
| `src/gamepad.cpp` | getMillis/getMicro、条件编译 Pico 头；reboot 调用适配 |
| `src/webconfig.cpp` | 删除或大幅裁剪；RNDIS/网页逻辑仅 Pico；STM32 仅保留 WebHID 相关或移至新文件 |
| `CMakeLists.txt` | 增加 PLATFORM 选项、STM32 目标、条件编译源与库 |
| `configs/` | 新增 STM32H723 板型配置（引脚、USB 分配） |
| 各 addon 与 driver | 将 `#include "hardware/..."` 等改为平台层或条件编译 |

### 12.3 不参与 STM32 编译的模块（示例）

- **插件与库**：详见 **1.6**。不移植的 addon 及库包括：`lib/NeoPico`、`src/addons/neopicoleds.cpp`、`bootsel_button.cpp`、`display.cpp`、`board_led.cpp`、`buzzerspeaker.cpp`、`drv8833_rumble.cpp`、`reactiveleds.cpp`、`playerleds.cpp`、`keyboard_host*.cpp`、`gamepad_usb_host*.cpp`；以及 `he_trigger.cpp`、`focus_mode.cpp`、`i2canalog1219.cpp`、`spi_analog_ads1256.cpp`、`i2c_gpio_pcf8575.cpp`、`dualdirectional.cpp`、`rotaryencoder.cpp`。
- **其他**：`lib/pico_pio_usb`、`lib/rndis`、`lib/lwip-port`、`lib/httpd`、`www/` 与 RNDIS/NetDriver 相关源。

---

## 附录 A：时间/睡眠 API 使用统计与 DWT 要点

（以下内容供阶段 1 实现时间层时参考；与 5.2 节第 5 条一致。）

### A.1 核心入口与直接使用

- **getMillis() / getMicro()**（gamepad.cpp）：全工程统一“自启动 ms/us”，必须实现。
- **直接“当前时间”**：PS4/PS4B/SwitchPro、P5General、XInputAuth、XBOne/XBOneAuth、reactiveleds、lsm6dsr_imu、TinyUSB board_api.h 等做报告间隔/超时；STM32 保留的模块均依赖，用 64 位 us（或 32 位 ms + 子 ms）即可。
- **sleep_ms / sleep_us**：usbhostmanager(10us)、XBOne(50ms/间隔)、tg16(1ms)、ADS1256(若干 us/ms)、TinyUSB osal；STM32 侧均需实现，量不大。
- **超时辅助**（absolute_time_t、time_reached、make_timeout_*、nil_time）：gp2040 热键、gamepad focus、PicoPeripherals I2C；STM32 保留部分需保留；neopicoleds/playerleds/animationstation 已裁剪可不实现。

### A.2 不需要实现

- **Pico alarm 池**（`add_alarm_in_ms`、`cancel_alarm` 等）：仅 FlashPROM 使用；STM32 FlashPROM 用定时器或主循环延迟写入，不实现 alarm。
- 多定时器、回调：工程未依赖，DWT + 最小 API 集即可覆盖。

### A.3 小结

| 问题 | 结论 |
|------|------|
| 重要吗？ | 重要，getMillis/getMicro 与超时为通用依赖。 |
| 用得多吗？ | 调用点不少，但模式统一，无需完整 Pico time 栈。 |
| 能精简并用 DWT 代替吗？ | 可以。DWT（+ SysTick 扩展 64 位 us）+ 上表最小 API，无需 alarm。 |

---

## 十三、风险与依赖

| 风险 | 缓解 |
|------|------|
| ST Host 适配层与现有 USBHostManager 回调对接有误 | 按 1.4 与阶段 5 明确适配点（枚举/report/set_report/get_report），单元测试各 cb 触发与参数 |
| 双 USB 占用引脚与现有板型冲突 | 在 BoardConfig 中明确 USB1/USB2 引脚，与硬件设计对齐 |
| WebHID 浏览器兼容性 | 提供桌面端 HID 工具作为备选，并文档化支持的浏览器/OS |
| FlashPROM 擦写寿命 | 延续现有「延迟合并写入」策略，并文档化擦写次数与寿命 |

---

## 十四、版本与维护

- 本文档随移植推进可增补「已完成项」与子任务拆解。
- 建议在仓库中为 STM32 移植单独建分支或标签，便于与主线 Pico 开发并行。
- 完成阶段 7 后，可将本规划归档为「已执行」，并在 README 中链接 STM32 构建与 WebHID 配置文档。

---

*文档版本：1.4 | 基于前期 STM32F722 难度分析与 STM32H723 双 USB 简化方案讨论；时间/睡眠 API 与 DWT 已合并入阶段 1 与附录 A；USB 栈选型明确为 Device 用 TinyUSB、Host 用 ST 官方库；因 STM32H723 Flash 仅 1MB、当前带网页固件 >2MB，新增前置阶段 P「WebHID 前后端分离与固件瘦身」；要求 USB Device 具备 8K 回报率能力，且前端可配置 2K/4K/8K（见 1.2、1.5、阶段 P/4/6/7）。*
