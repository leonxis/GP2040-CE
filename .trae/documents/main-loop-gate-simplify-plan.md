# 门控逻辑简化修改计划

## 目标

取消门控逻辑中的摇杆延迟采样、WCET 统计、时间触发门控，恢复插件统一调度，确保每帧真实数据可报为最优先要求，以 IN 令牌触发后尽快跑完主循环为第一要务。

## 当前状态分析

### 问题
1. `shouldUseMainLoopGate()` 不检查无线模式，无线模式通过 `wirelessLinkActive()` 走 `TIME_LOCKED` 时间触发路径
2. 门控模式下用 `PreprocessGateEarlyAddons()` 拆分前处理，非门控用 `PreprocessAddons()`
3. 有 `sleepUntilLateSampleDeadline()` 在 `gamepad->process()` 后 sleep，延迟主循环
4. 有 `sampleMainLoopGateLateAnalog()` 延迟采样，WCET 不全时丢弃摇杆数据
5. 大量 WCET 统计（5个 `MainLoopGateRollingMax` 实例 + deadline 调度 + bound 降级）
6. 摇杆插件 `process()` 空跑，逻辑在显式调用中（`processAnalog()`/`processTravelKey()`/`applyFinalProcess()`）
7. `MCP3208ADCAddon::process()` 空跑，采样在 `sampleGateLateAnalog()` 中

### 涉及文件
- `src/gp2040.cpp` — 门控核心逻辑、主循环
- `headers/gp2040.h` — `MainLoopGateStats` 结构体
- `headers/gpaddon.h` — `GateLateAnalogSource` 枚举、GPAddon 虚接口
- `headers/addonmanager.h` — `AddonManager` GateLateAnalog 方法
- `src/addonmanager.cpp` — `AddonManager` GateLateAnalog 方法实现
- `headers/usbdriver.h` — `isMainLoopGateTimeTriggered()` 声明
- `src/drivers/ps4/PS4Driver.cpp` — `isMainLoopGateTimeTriggered()` 调用
- `src/drivers/ps4b/PS4BDriver.cpp` — `isMainLoopGateTimeTriggered()` 调用
- `src/addons/mcp3208_adc.cpp` — `process()` 实现、GateLateAnalog 方法
- `headers/addons/mcp3208_adc.h` — GateLateAnalog 声明
- `src/addons/unified_analog_processor.cpp` — `process()` 实现
- `src/addons/unified_joystick_travel_key.cpp` — `process()` 实现
- `src/addons/axis_tilt_overlay.cpp` — `process()` 实现

## 修改步骤

### 步骤 1：修改 `shouldUseMainLoopGate()` 启用条件

**文件**: [src/gp2040.cpp:365-380](file:///home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp#L365-L380)

将启用条件改为：仅 `PS4/PS4B/XInput/XInputB` + `reportRate == 1000Hz` 且三个无线模式均关闭：

```cpp
static inline bool shouldUseMainLoopGate() {
    const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
    const GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();
    const InputMode inputMode = DriverManager::getInstance().getInputMode();
    const bool supportedMode =
        (inputMode == INPUT_MODE_PS4 || inputMode == INPUT_MODE_PS4B ||
         inputMode == INPUT_MODE_XINPUT || inputMode == INPUT_MODE_XINPUTB);
    const bool noWireless =
        !gamepadOptions.wirelessLinkEnabled &&
        !gamepadOptions.bluetoothLinkEnabled &&
        !gamepadOptions.nrf24LinkEnabled;
    return (addonOptions.reportRate == MAIN_LOOP_GATE_REPORT_RATE_HZ) && supportedMode && noWireless;
}
```

**原因**: 无线模式不在门控启用逻辑中，自由跑动。门控仅用于有线模式。

### 步骤 2：删除时间触发门控

**文件**: `src/gp2040.cpp`

删除以下内容：
- 变量: `main_loop_gate_time_triggered` (L68), `main_loop_gate_last_frame_start_us` (L69)
- 常量: `MAIN_LOOP_GATE_WIRELESS_FRAME_INTERVAL_US` (L85)
- 状态: `TIME_LOCKED` (L120)
- 函数: `isMainLoopGateTimeTriggered()` (L361-363)
- 函数: `wirelessLinkActive()` (L635-638)
- 函数: `prepareMainLoopGateTimeTriggeredFrameSchedule()` (L568-604)
- setup() 中 `main_loop_gate_time_triggered = wirelessLinkActive();` (L1372)

修改 `getMainLoopGateAction()`：删除所有 `main_loop_gate_time_triggered` 分支，恢复为 be6ac11e 的简单逻辑（USB 未挂载 → WaitUSB，挂起 → ScanSuspended）

修改 `mainLoopGateReportAttempted()`：删除 `main_loop_gate_time_triggered` 分支

修改 `resetMainLoopGateForSnapshot()`：删除 `main_loop_gate_time_triggered` 分支

修改 `mainLoopGateEventPending()`：删除 `main_loop_gate_time_triggered` 分支

**文件**: `headers/usbdriver.h` — 删除 `isMainLoopGateTimeTriggered()` 声明 (L42)

**文件**: `src/drivers/ps4/PS4Driver.cpp` (L704) 和 `src/drivers/ps4b/PS4BDriver.cpp` (L678)
将 `if (tud_suspended() && !isMainLoopGateTimeTriggered())` 改回 `if (tud_suspended())`

**原因**: 无线模式不在门控中，不需要时间触发。有线模式挂起时自动 remote wakeup。

### 步骤 3：删除延迟采样与 WCET 统计

**文件**: `src/gp2040.cpp`

删除以下函数和变量：
- `MainLoopGateRollingMax` 结构体 (L137-194)（含 `secondMaximum()`）
- 所有 `MainLoopGateRollingMax` 实例: `mcp3208_burst_setup_wcet`, `mcp3208_sample_wcet`, `final_process_wcet`, `endpoint_arm_wcet`, `postprocess_wcet` (L224-228)
- `mainLoopGateADCWcet()`, `mainLoopGateADCBurstSetupWcet()` (L260-280)
- `mainLoopGateSchedulingMeasurementsReady()` (L282-296)
- `resetMainLoopGateMeasurements()` (L298-314)
- `MainLoopGateResolvedBounds` 结构体 (L464-469)
- `resolveMainLoopGateBounds()` (L477-506)
- `prepareMainLoopGateFrameSchedule()` (L508-562)
- `sleepUntilLateSampleDeadline()` (L872-899)
- `sampleMainLoopGateLateAnalog()` (L901-985)
- `recordMainLoopGateFrameTiming()` (L987-1042)
- `MainLoopGateLateSampleResult` 结构体 (L203-207)
- `MainLoopGateFrameSchedule` 结构体 (L196-201)
- `main_loop_gate_frame_schedule` (L229)
- 所有 WCET 统计变量: `sample_age_last_us`, `sample_age_max_us`, `deadline_miss_count`, `phase_mutation_count`, `late_sample_set_count`, `repeated_sample_frame_count`, `frame_without_fresh_sample_count`, `max_sample_sets_per_frame`, `bound_degraded_count` (L230-239)
- `main_loop_gate_analog_source` (L222-223)
- 常量: `MAIN_LOOP_GATE_SAFETY_MARGIN_US`, `MAIN_LOOP_GATE_WCET_WINDOW`, `MAIN_LOOP_GATE_COMPLETION_TO_TOKEN_GUARD_US`, `MAIN_LOOP_GATE_ENDPOINT_READY_GUARD_US`（如仍存在）

简化 `getMainLoopGateStats()`：删除所有 WCET 相关字段，仅保留 `deadlineSchedulingActive`, `analogSource`, `stableCompletions`, `phaseMinUs`, `phaseMaxUs`

**文件**: `headers/gp2040.h` — 从 `MainLoopGateStats` 中删除 WCET 字段 (L27-L42)，仅保留 `deadlineSchedulingActive`, `analogSource`, `stableCompletions`, `phaseMinUs`, `phaseMaxUs`

### 步骤 4：恢复插件统一调度

**文件**: `src/gp2040.cpp` 主循环 `run()` (L1504+)

删除 `splitGateFrame` 分支，统一使用 `PreprocessAddons()`：

```cpp
// 删除 splitGateFrame 变量
// 删除 PreprocessGateEarlyAddons() 调用
// 统一调用:
addons.PreprocessAddons();
```

删除主循环中的显式调用代码：
- 删除 `sleepUntilLateSampleDeadline()` 调用 (L1650-1652)
- 删除 `sampleMainLoopGateLateAnalog()` 调用 (L1658-1659)
- 删除 `finalProcessStartUs`/`endpointArmStartUs`/`endpointArmEndUs` 时间打点 (L1661-1662, L1750-1751, L1756-1757)
- 删除 `analogProc->processAnalog()` 显式调用 (L1666-1670)（逻辑已移入 `process()`，由 `ProcessAddons()` 统一调用）
- 删除 `travelKey->processTravelKey()` 显式调用 (L1672-1676)（同上）
- 删除 `axisTiltOverlay->applyFinalProcess(gamepad)` 显式调用 (L1721-1724)（同上）
- 删除 `recordMainLoopGateFrameTiming()` 调用 (L1758-1766)
- 删除 `restoreGateSPIProfile()` 调用 (L1774-1776)
- 删除 `postprocessStartUs`/`postprocess_wcet.record()` (L1771-1772, L1792-1800)

简化后的主循环 RunFrame 路径：
```
debounceGpioGetAll → gamepad->read → USBHostManager.process
→ PreprocessAddons → gamepad->process → ProcessAddons
→ gamepad->hotkey → rebootHotkeys.process
→ 双向交换 → checkProcessedState → memcpy processedGamepad
→ suppressUsb (if screenOperationActive)
→ inputDriver->process → mainLoopGateReportAttempted
→ tud_task → PostprocessAddons → checkSaveRebootState
```

### 步骤 5：摇杆插件函数直接改名为 process()，避免层层调用

不采用在 `process()` 中调用原函数的间接方式，而是直接将原函数改名为 `process()`，减少栈深度。

**文件**: `src/addons/mcp3208_adc.cpp` + `headers/addons/mcp3208_adc.h`
- 删除空的 `process()` (L219-221)
- 将 `sampleStickSnapshot()` 改名为 `process()`，去掉 `GateLateAnalogSampleRequest` 参数（deadline 检查已不需要）
- 在 `process()` 内部去掉对 `request` 参数的引用：`publishStickSnapshot()` 中的 `gateDeadlineReached(request, ...)` 检查删除，直接发布快照
- 删除 `gateDeadlineReached()` 辅助函数 (L14-20)
- `reinit()` 中对 `sampleStickSnapshot()` 的调用改为 `process()`
- 删除 GateLateAnalog 方法: `beginGateLateAnalogBurst()`, `sampleGateLateAnalog()`, `endGateLateAnalogBurst()`, `gateLateAnalogCompletedTimeUs()` (L198-217)
- 删除 `isGateLateAnalogProvider()` 声明和 `gateLateAnalogSource()` 声明
- 删除 `gateLateBurstActive_` 成员变量
- `preprocess()` 保持为空

**文件**: `src/addons/unified_analog_processor.cpp` + `headers/addons/unified_analog_processor.h`
- 删除空的 `process()` (L302-305)
- 将 `processAnalog()` 直接改名为 `process()`
- 更新头文件声明：删除 `virtual void processAnalog();`，保留 `virtual void process();`（已是基类纯虚函数）

**文件**: `src/addons/unified_joystick_travel_key.cpp` + `headers/addons/unified_joystick_travel_key.h`
- 删除空的 `process()` (L157-159)
- 将 `processTravelKey()` 直接改名为 `process()`
- 更新头文件声明：删除 `virtual void processTravelKey();`，保留 `virtual void process();`

**文件**: `src/addons/axis_tilt_overlay.cpp` + `headers/addons/axis_tilt_overlay.h`
- 当前 `process()` 为空 `{}`，`applyFinalProcess(Gamepad*)` 包含实际逻辑
- 将 `applyFinalProcess(Gamepad*)` 逻辑直接移入 `process()`，在函数开头通过 `Storage::getInstance().GetGamepad()` 获取 gamepad 指针
- 删除 `applyFinalProcess(Gamepad*)` 函数声明和定义
- 更新头文件：删除 `void applyFinalProcess(Gamepad* gamepad);`

**原因**: 避免层层调用增加栈深度。直接将实际逻辑函数改名为 `process()`，通过 `ProcessAddons()` 统一调用。

### 步骤 6：调整插件注册顺序

**文件**: `src/gp2040.cpp` setup() (L1259-1288)

将 MCP3208ADCAddon、UnifiedAnalogProcessorAddon、UnifiedJoystickTravelKeyAddon 移到 AxisTiltOverlayInput 之前（所有非摇杆插件之后）：

```
addons.LoadAddon(new AnalogInput());
// ... 所有非摇杆插件 ...
addons.LoadAddon(new ReverseInput());
addons.LoadAddon(new TurboInput());
// 摇杆采样与后处理（按依赖顺序）
addons.LoadAddon(new MCP3208ADCAddon());        // 采样
addons.LoadAddon(new UnifiedAnalogProcessorAddon());  // 处理
addons.LoadAddon(new UnifiedJoystickTravelKeyAddon()); // 后处理
addons.LoadAddon(new AxisTiltOverlayInput());   // 最终叠加
addons.LoadAddon(new InputMacro());             // 覆盖摇杆值
addons.LoadAddon(new UARTLinkAddon());
addons.LoadAddon(new NRF24LinkAddon());
```

删除 `main_loop_gate_analog_source = addons.GetGateLateAnalogSource();` (L1287-1288)

**原因**: `ProcessAddons()` 按 `addons` 向量顺序调用 `process()`。摇杆采样与后处理放在最后，确保所有其他插件先执行。InputMacro 在摇杆后处理之后执行以覆盖摇杆值（约束: InputMacro::process() must be called after analog processing and before bidirectional swapping）。

### 步骤 7：删除 GateLateAnalog 接口

**文件**: `headers/gpaddon.h`
- 删除 `GateLateAnalogSource` 枚举 (L10-13)
- 删除 `GateLateAnalogSampleRequest` 结构体 (L15-18)
- 删除 GPAddon 中的虚方法: `preprocessGateEarly()`, `isGateLateAnalogProvider()`, `gateLateAnalogSource()`, `beginGateLateAnalogBurst()`, `sampleGateLateAnalog()`, `endGateLateAnalogBurst()`, `gateLateAnalogCompletedTimeUs()` (L31-44)

**文件**: `headers/addonmanager.h`
- 删除方法声明: `PreprocessGateEarlyAddons()`, `GetGateLateAnalogSource()`, `BeginGateLateAnalogBurst()`, `SampleGateLateAnalog()`, `EndGateLateAnalogBurst()`, `GetGateLateAnalogCompletedTimeUs()` (L28-33)
- 删除成员变量: `gateLateAnalogProvider` (L39)

**文件**: `src/addonmanager.cpp`
- 删除 `PreprocessGateEarlyAddons()` 实现 (L42-46)
- 删除 GateLateAnalog 方法实现 (L48-76)
- 删除 `LoadAddon()` 中 `isGateLateAnalogProvider()` 检查 (L10-12)

**文件**: `headers/addons/mcp3208_adc.h`
- 删除 GateLateAnalog 相关声明

### 步骤 8：清理 preprocessGateEarly 引用

**文件**: `src/addons/mcp3208_adc.cpp`
- `MCP3208ADCAddon::preprocess()` (L193-196): 当前为空，保持为空或删除注释

**文件**: `headers/gpaddon.h`
- 删除 `virtual void preprocessGateEarly() { preprocess(); }` (L33)

### 步骤 9：处理无线模式 USB 未挂载问题

无线模式下 `shouldUseMainLoopGate()` 返回 false → `main_loop_gate_enabled = false` → `getMainLoopGateAction()` 直接返回 `RunFrame` → 主循环自由跑动。USB 未挂载时 `tud_task()` 不阻塞，主循环照常执行。

无需额外代码修改，步骤 1 的启用条件变更自然解决此问题。

## 修改后门控逻辑与 be6ac11e 的区别

### 相同部分（保留不变）

| 方面 | 说明 |
|------|------|
| 核心状态机 | WAIT_MOUNT → BOOTSTRAP_BUILD → BOOTSTRAP_SUBMIT → WAIT_FIRST_IN → LEARNING → LOCKED + RECOVERY + SUSPENDED_ARMED/UNARMED |
| IN 令牌驱动 | 以 USB HID IN 完成事件（completeSeq）作为相位基准，SOF ISR 提供帧号/时间戳 |
| USB 快照 | `USBMainGamepadGateSnapshot` + `usb_get_main_gamepad_gate_snapshot()` + epoch 世代号 |
| 相位学习 | `recordMainLoopGateCompletion()` — phaseUs/sofFrameDelta/phaseSpread，128 次稳定 → LOCKED |
| 主循环动作 | getMainLoopGateAction → RunFrame/RetrySubmit/WaitUSB/ScanSuspended |
| 报告提交 | `mainLoopGateReportAttempted()` — armed 标记 + 状态转换 + 失败处理 |
| 挂起处理 | ScanSuspended — 低频 GPIO 扫描触发 remote wakeup |
| SOF ISR | 门控启用时 `tud_sof_isr_set(usb_notify_main_gamepad_sof)` |

### 关键区别

| 方面 | be6ac11e | 修改后 |
|------|----------|--------|
| **启用条件** | 仅 `reportRate==1000Hz` + 支持模式 | 额外要求三个无线模式均关闭（`!wirelessLinkEnabled && !bluetoothLinkEnabled && !nrf24LinkEnabled`） |
| **延迟采样** | 有 `sampleMainLoopGateLateAnalog()`：在 `gamepad->process()` 后、`ProcessAddons()` 前执行 MCP3208 burst+sample+end，LOCKED 时按 deadline 循环采样 | **删除**：MCP3208 采样在 `process()` 中由 `ProcessAddons()` 统一调用 |
| **sleep** | 无 `sleepUntilLateSampleDeadline()` | 同样无（一致） |
| **WCET 统计** | 有 `MainLoopGateRollingMax`（窗口=64），统计 ADC burst setup/sample、final process、endpoint arm；无 secondMaximum() | **全部删除** |
| **deadline 调度** | 有 `prepareMainLoopGateFrameSchedule()`：计算 nextTokenEarliestUs/armDeadlineUs/finalizeDeadlineUs，LOCKED 时启用 | **删除** |
| **插件前处理** | 门控模式 `PreprocessGateEarlyAddons()`（调用 `preprocessGateEarly()`），非门控 `PreprocessAddons()` | **统一** `PreprocessAddons()`，删除 `PreprocessGateEarlyAddons()` |
| **插件后处理** | `ProcessAddons()` 全量遍历，但 3 个摇杆插件 `process()` 空跑，逻辑由主循环显式调用 | **统一**：摇杆插件 `process()` 直接包含逻辑，`ProcessAddons()` 统一调用，删除显式调用 |
| **GateLateAnalog 接口** | 有：`BeginGateLateAnalogBurst`/`SampleGateLateAnalog`/`EndGateLateAnalogBurst`/`GetGateLateAnalogCompletedTimeUs` + `GateLateAnalogSource` 枚举 | **删除** |
| **插件注册顺序** | MCP3208(#4)、UnifiedAnalogProcessor(#5)、UnifiedJoystickTravelKey(#6) 在前；AxisTiltOverlay(#22)、InputMacro(#23) 在后 | MCP3208/UnifiedAnalogProcessor/UnifiedJoystickTravelKey/AxisTiltOverlay **移到最后**（#21-24），InputMacro(#25) 在其后 |
| **RunFrame 路径** | debounce→read→PreprocessGateEarly→gamepad.process→**sampleMainLoopGateLateAnalog**→ProcessAddons→hotkey→**processAnalog**→**processTravelKey**→双向交换→**applyFinalProcess**→inputDriver.process→**recordMainLoopGateFrameTiming**→**restoreGateSPIProfile**→tud_task→PostprocessAddons | debounce→read→PreprocessAddons→gamepad.process→ProcessAddons→hotkey→双向交换→inputDriver.process→mainLoopGateReportAttempted→tud_task→PostprocessAddons |
| **MainLoopGateStats** | 有 WCET 字段（ads8332/mcp3208 burst/sample、finalProcess、endpointArm、sampleAge、deadlineMiss 等） | 仅保留 `deadlineSchedulingActive`、`analogSource`、`stableCompletions`、`phaseMinUs`、`phaseMaxUs` |

### 本质差异总结

修改后的门控逻辑相当于 **be6ac11e 的门控核心（状态机+IN令牌+相位学习）** 去掉了：
1. 延迟采样机制（`sampleMainLoopGateLateAnalog` + GateLateAnalog 接口）
2. WCET 统计与 deadline 调度（`MainLoopGateRollingMax` + `prepareMainLoopGateFrameSchedule` + `recordMainLoopGateFrameTiming`）
3. 插件前后拆分（`PreprocessGateEarlyAddons` + 显式调用）

并增加了：
1. 无线模式排除条件
2. 摇杆插件注册顺序移到最后
3. 摇杆插件 `process()` 直接包含逻辑（改名，非层层调用）

这样修改后的 RunFrame 路径比 be6ac11e 更短：没有了采样/sleep/WCET 打点/显式调用的开销，IN 令牌触发后直接跑完整个主循环。

## 验证

1. HML2354 编译验证: `./gp2354.sh`（增量构建）
2. 检查门控启用条件: 有线模式 + PS4/PS4B/XInput/XInputB + 1000Hz 时门控启用；任一无线模式开启时门控不启用
3. 检查无线模式主循环: 无线模式下主循环自由跑动，USB 未挂载不阻塞
4. 检查插件执行顺序: ProcessAddons 中摇杆插件最后执行，InputMacro 在摇杆后处理之后
5. 检查无残留: grep 确认 GateLateAnalog、TIME_LOCKED、time_triggered、sampleMainLoopGateLateAnalog 等已全部删除
