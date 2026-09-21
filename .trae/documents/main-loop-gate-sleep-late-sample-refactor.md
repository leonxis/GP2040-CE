# 主循环门控重构：移除早采样 + sleep-then-single-sample + 显式调用摇杆插件

## 摘要

将当前"早采样 + 多次晚采样循环"重构为"sleep 到 deadline 前一刻 + 单次晚采样"模式。摇杆插件（UnifiedAnalogProcessor、UnifiedJoystickTravelKey）采用 AxisTiltOverlay 已有模式：`process()` 改空，实际逻辑由主循环通过 `GetAddon` 显式调用。无需拆分 `ProcessAddons()` 或新增插件分类机制。PostprocessAddons 耗时纳入 deadline 预留。**采样统一由 `sampleMainLoopGateLateAnalog()` 无条件调用承担**（门控 `deadlineMode=true` 走 deadline 检查；非门控 `deadlineMode=false` 跳过检查但仍单次采样），`preprocess()` 和 `process()` 均改为空。**安全余量统一为 `MAIN_LOOP_GATE_SAFETY_MARGIN_US = 50µs`**（替代原 layered 112µs），各 Bound 使用原始 WCET `maximum`，50µs 在 armDeadline 层统一注入。**WCET 测量窗口修正**：postprocess WCET 前移覆盖 `restoreGateSPIProfile + processCompositeHID + tud_task + PostprocessAddons`；sample WCET 纳入 `EndGateLateAnalogBurst`。

**适用范围**：HML2354（RP2350）构建分支。记录本文档供 RP2040 分支后续同问题移植参考。

## 当前状态分析

### 主循环 RunFrame 流程（gp2040.cpp:1381-1535）

```
gamepad->read() → PreprocessGateEarlyAddons(含早采样) → gamepad->process()
→ sampleMainLoopGateLateAnalog(while循环多次采样直到deadline)
→ ProcessAddons(全部25个插件) → hotkey → 双向交换 → axisTiltOverlay.applyFinalProcess
→ processedGamepad->state = gamepad->state → inputDriver->process → tud_task
→ PostprocessAddons(uart/nrf发送)
```

### 问题

1. **早采样冗余**：MCP3208 `preprocess()`（mcp3208_adc.cpp:193-196）做一次 SPI 采样，晚采样会覆盖该快照，早采样仅作 fallback。
2. **多次采样循环**：`sampleMainLoopGateLateAnalog`（gp2040.cpp:694-801）在 deadline 模式下 `while(true)` 循环采样直到 deadline，占用 CPU 且产生多余 SPI 流量。
3. **摇杆插件在 ProcessAddons 中全量遍历**：无法在 sleep 前执行非摇杆插件、sleep 后执行摇杆插件。
4. **PostprocessAddons 未预留**：deadline 计算（gp2040.cpp:441-477）仅含 `finalProcessBoundUs` + `endpointArmBoundUs`，未含 uart/nrf 发送耗时。

### AxisTiltOverlay 先例

AxisTiltOverlayInput 已采用显式调用模式：
- `process()` 为空（axis_tilt_overlay.h:54），在 ProcessAddons 中空跑
- 实际工作在 `applyFinalProcess(Gamepad*)` 中，由主循环通过 `GetAddon` 显式调用（gp2040.cpp:1469-1471）

本方案对 UnifiedAnalogProcessor 和 UnifiedJoystickTravelKey 采用相同模式。

### 关键代码位置

| 文件 | 行号 | 内容 |
|------|------|------|
| headers/gpaddon.h | 20-58 | GPAddon 基类 |
| headers/addonmanager.h | 20-40 | AddonManager 类（无需修改）|
| src/addonmanager.cpp | 78-90 | ProcessAddons/PostprocessAddons（无需修改）|
| src/gp2040.cpp | 78-79 | `ENDPOINT_READY_GUARD_US` + `WCET_MARGIN_US` 常量（需替换为 `SAFETY_MARGIN_US=50`，见步骤 5d）|
| src/gp2040.cpp | 132-160 | MainLoopGateRollingMax 滚动 WCET 容器 |
| src/gp2040.cpp | 203-205 | final_process_wcet + endpoint_arm_wcet 静态实例 |
| src/gp2040.cpp | 256-285 | scheduling ready 检查 + reset measurements |
| src/gp2040.cpp | 431-478 | prepareMainLoopGateFrameSchedule（deadline 计算）|
| src/gp2040.cpp | 694-801 | sampleMainLoopGateLateAnalog（需简化）|
| src/gp2040.cpp | 803-855 | recordMainLoopGateFrameTiming（WCET 打点）|
| src/gp2040.cpp | 1066-1093 | 插件注册列表 |
| src/gp2040.cpp | 1402-1531 | RunFrame 主循环体 |
| src/addons/mcp3208_adc.cpp | 193-196 | preprocess() 早采样（需清空）|
| headers/addons/axis_tilt_overlay.h | 53-59 | process() 为空，applyFinalProcess 为实际工作 |
| headers/addons/unified_analog_processor.h | — | 需新增 processAnalog() 声明 |
| headers/addons/unified_joystick_travel_key.h | — | 需新增 processTravelKey() 声明 |

### 摇杆相关插件清单

仅 2 个插件 `process()` 非空且依赖采样数据：

| 插件 | 注册行号 | process() 作用 | 重构后 |
|------|---------|---------------|--------|
| UnifiedAnalogProcessorAddon | 1068 | 读 MCP3208 快照→曲线/死区/抖动量化→写 gamepad->state | process() 改空，逻辑移入 processAnalog() |
| UnifiedJoystickTravelKeyAddon | 1069 | 读 MCP3208 快照→行程阈值触发按键 | process() 改空，逻辑移入 processTravelKey() |

注：AxisTiltOverlayInput（1090）已是此模式，`process()` 为空，`applyFinalProcess()` 在 gp2040.cpp:1471 显式调用，无需改动。

## 修改计划

### 步骤 1：UnifiedAnalogProcessor 改为显式调用模式

**文件 1**：`headers/addons/unified_analog_processor.h`

新增公共方法声明：
```cpp
// 实际摇杆处理逻辑（曲线/死区/抖动量化→写 gamepad->state）
// 由主循环在晚采样后显式调用（与 AxisTiltOverlay.applyFinalProcess 同模式）
void processAnalog();
```

**文件 2**：`src/addons/unified_analog_processor.cpp`

将 `process()` 方法体（行 145 起）整体移入 `processAnalog()`：
```cpp
void UnifiedAnalogProcessorAddon::processAnalog() {
    // 原 process() 的全部逻辑移入此处，不变
    ...
}

void UnifiedAnalogProcessorAddon::process() {
    // 空跑：实际逻辑在 processAnalog() 中由主循环显式调用
    // 保留空方法体供 ProcessAddons 遍历调用（与 AxisTiltOverlay.process 一致）
}
```

### 步骤 2：UnifiedJoystickTravelKey 改为显式调用模式

**文件 1**：`headers/addons/unified_joystick_travel_key.h`

新增公共方法声明：
```cpp
// 实际行程按键触发逻辑
// 由主循环在晚采样后显式调用
void processTravelKey();
```

**文件 2**：`src/addons/unified_joystick_travel_key.cpp`

将 `process()` 方法体（行 106 起）整体移入 `processTravelKey()`：
```cpp
void UnifiedJoystickTravelKeyAddon::processTravelKey() {
    // 原 process() 的全部逻辑移入此处，不变
    ...
}

void UnifiedJoystickTravelKeyAddon::process() {
    // 空跑：实际逻辑在 processTravelKey() 中由主循环显式调用
}
```

### 步骤 3：移除 MCP3208 早采样

**文件**：[src/addons/mcp3208_adc.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/addons/mcp3208_adc.cpp)（行 193-196）

将 `preprocess()` 改为空函数：

```cpp
void MCP3208ADCAddon::preprocess() {
    // 早采样已移除：采样统一在 sampleMainLoopGateLateAnalog() 中进行，
    // 该函数在主循环中无条件调用（门控与非门控模式均执行）。
}
```

**注意 1**：`reinit()`（行 223-226）仍保留 `sampleStickSnapshot()` 调用，用于初始化时建立首个快照。

**注意 2**：`process()`（行 219-221）保持为空——无需任何采样补偿。非门控模式由 `sampleMainLoopGateLateAnalog()` 自身的 `deadlineMode=false` 分支完成单次采样（跳过 deadline 检查，直接 burst + sample + end）。

### 步骤 4：简化晚采样为单次采样

**文件**：[src/gp2040.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp)（行 694-801）

将 `sampleMainLoopGateLateAnalog()` 简化：

- 移除 `while(true)` 多次采样循环
- 保留 deadline 检查（不足则跳过采样，使用上一帧快照）
- 保留 burst setup/sample/end 三段式调用
- **`EndGateLateAnalogBurst()` 移入测量窗口**——原始代码在 `adcWcet->record()` 之后才调用 EndBurst，导致 sample WCET 漏计 EndBurst 耗时。修正为在 record 之前调用，使 sample WCET 覆盖完整 burst 生命周期（Begin + Sample + End）
- 单次采样后立即返回
- **移除 per-bound `MAIN_LOOP_GATE_WCET_MARGIN_US`**——安全余量统一为单一 `MAIN_LOOP_GATE_SAFETY_MARGIN_US = 50µs`，在 armDeadline 层注入（见步骤 6），各 Bound 使用原始 WCET 最大值

```cpp
static MainLoopGateLateSampleResult sampleMainLoopGateLateAnalog(
        AddonManager& addons) {
        MainLoopGateLateSampleResult result;
        MainLoopGateRollingMax* adcWcet =
                mainLoopGateADCWcet(main_loop_gate_analog_source);
        MainLoopGateRollingMax* burstSetupWcet =
                mainLoopGateADCBurstSetupWcet(main_loop_gate_analog_source);
        if (adcWcet == nullptr || burstSetupWcet == nullptr) {
                return result;
        }

        const bool deadlineMode =
                main_loop_gate_state == MainLoopGateState::LOCKED &&
                main_loop_gate_frame_schedule.valid;
        if (deadlineMode) {
                // 使用原始 WCET 最大值；安全余量统一在 armDeadline 层注入（见步骤 6）
                const uint32_t burstSetupBoundUs = burstSetupWcet->maximum;
                const uint32_t adcSampleBoundUs = adcWcet->maximum;
                if (mainLoopGateTimeRemaining(
                                time_us_32(),
                                main_loop_gate_frame_schedule.finalizeDeadlineUs) <
                        burstSetupBoundUs + adcSampleBoundUs) {
                        main_loop_gate_frame_without_fresh_sample_count++;
                        return result;
                }
        }

        const uint32_t burstSetupStartUs = time_us_32();
        const bool burstStarted = addons.BeginGateLateAnalogBurst();
        const uint32_t burstSetupEndUs = time_us_32();
        if (!burstStarted) {
                main_loop_gate_frame_without_fresh_sample_count++;
                return result;
        }
        uint32_t burstSetupDurationUs = burstSetupEndUs - burstSetupStartUs;
        if (burstSetupDurationUs == 0) burstSetupDurationUs = 1;
        burstSetupWcet->record(burstSetupDurationUs);
        result.busTouched = true;

        GateLateAnalogSampleRequest request;
        request.enforceDeadline = deadlineMode;
        request.deadlineUs = main_loop_gate_frame_schedule.finalizeDeadlineUs;

        // 采样 WCET 含 EndGateLateAnalogBurst：覆盖完整 burst 生命周期
        const uint32_t sampleStartUs = time_us_32();
        const bool sampled = addons.SampleGateLateAnalog(request);
        addons.EndGateLateAnalogBurst();
        const uint32_t sampleEndUs = time_us_32();
        uint32_t sampleDurationUs = sampleEndUs - sampleStartUs;
        if (sampleDurationUs == 0) sampleDurationUs = 1;
        adcWcet->record(sampleDurationUs);

        if (!sampled) {
                result.deadlineOverrun =
                        deadlineMode &&
                        mainLoopGateTimeReached(sampleEndUs, request.deadlineUs);
        } else {
                result.sampleSets = 1;
        }

        main_loop_gate_late_sample_set_count += result.sampleSets;
        if (result.sampleSets == 0) {
                main_loop_gate_frame_without_fresh_sample_count++;
        }
        return result;
}
```

移除不再需要的统计字段使用：`main_loop_gate_repeated_sample_frame_count`、`main_loop_gate_max_sample_sets_per_frame`（可保留为 0 兼容 stats 查询，或一并清理）。

### 步骤 5：新增 postprocess WCET 测量

**文件**：[src/gp2040.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp)

**5a. 新增静态实例**（行 203-205 附近）：

```cpp
static MainLoopGateRollingMax main_loop_gate_postprocess_wcet;
```

**5b. resetMainLoopGateMeasurements()**（行 271-285）新增：

```cpp
main_loop_gate_postprocess_wcet.reset();
```

**5c. mainLoopGateSchedulingMeasurementsReady()**（行 256-269）新增检查：

```cpp
if (main_loop_gate_postprocess_wcet.maximum == 0) return false;
```

**5d. 常量重构**（行 78-79 附近）：

将现有 `MAIN_LOOP_GATE_ENDPOINT_READY_GUARD_US = 16` 和 `MAIN_LOOP_GATE_WCET_MARGIN_US = 16` 替换为单一安全余量常量：

```cpp
// 总安全余量：覆盖 WCET 测量不确定性 + WFE 唤醒延迟 + USB 端点就绪抖动 + 未测量开销
// 在 armDeadline 层统一注入（见步骤 6），各 Bound 使用原始 WCET 最大值
static const uint32_t MAIN_LOOP_GATE_SAFETY_MARGIN_US = 50;
```

**注意**：原 `MAIN_LOOP_GATE_WCET_MARGIN_US` 在步骤 4 的 deadline 检查、步骤 6 的 Bound 计算、步骤 7 的 sampleBoundUs 中均有引用，需全部移除（各 Bound 用原始 WCET `maximum`），由 `MAIN_LOOP_GATE_SAFETY_MARGIN_US` 统一承担安全余量角色。

### 步骤 6：更新 deadline 计算纳入 postprocess

**文件**：[src/gp2040.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp)（行 431-478 `prepareMainLoopGateFrameSchedule`）

修改 `armDeadlineUs` 和 `finalizeDeadlineUs` 计算。各 Bound 使用原始 WCET 最大值（无 per-bound margin），安全余量 `MAIN_LOOP_GATE_SAFETY_MARGIN_US = 50µs` 在 armDeadline 层统一注入：

```cpp
// 各 Bound 使用原始 WCET 最大值，安全余量统一在 armDeadline 层注入
const uint32_t finalProcessBoundUs =
        main_loop_gate_final_process_wcet.maximum;
const uint32_t endpointArmBoundUs =
        main_loop_gate_endpoint_arm_wcet.maximum;
const uint32_t postprocessBoundUs =
        main_loop_gate_postprocess_wcet.maximum;
const uint32_t reservedUs =
        finalProcessBoundUs +
        endpointArmBoundUs +
        postprocessBoundUs +
        MAIN_LOOP_GATE_SAFETY_MARGIN_US;
if (reservedUs >= MAIN_LOOP_GATE_USB_FRAME_US) {
        return;
}

// ... tokenPhaseUs / nextTokenEarliestUs 计算不变 ...

// armDeadline = 下一 IN 令牌 - postprocess 耗时 - 50µs 安全余量
// 50µs 覆盖：WCET 测量不确定性 + WFE 唤醒延迟 + 端点就绪抖动 + 未测量开销
// （tud_task/restoreGateSPIProfile/processCompositeHID 已纳入 postprocess WCET 窗口，见步骤 8）
const uint32_t armDeadlineUs =
        nextTokenEarliestUs -
        postprocessBoundUs -
        MAIN_LOOP_GATE_SAFETY_MARGIN_US;

main_loop_gate_frame_schedule.valid = true;
main_loop_gate_frame_schedule.nextTokenEarliestUs = nextTokenEarliestUs;
main_loop_gate_frame_schedule.armDeadlineUs = armDeadlineUs;
main_loop_gate_frame_schedule.finalizeDeadlineUs =
        armDeadlineUs -
        finalProcessBoundUs -
        endpointArmBoundUs;
```

**deadline 公式推导**（与用户分析模型一致）：

```
sleepTargetUs = finalizeDeadlineUs - sampleBoundUs
             = nextTokenEarliestUs - postprocessBound - SAFETY(50µs)
               - finalProcessBound - endpointArmBound - sampleBound
sleep 时长 = sleepTargetUs - now(T2)
           = 1000 - (T2-T1) - (T4-T3 各段 WCET 之和) - 50µs
```

其中 `(T2-T1)` 由绝对时间定位隐式吸收（T2 = `time_us_32()` 实测），`(T4-T3)` 由四段 WCET 滚动最大值估算（sample + finalProcess + endpointArm + postprocess），安全余量 = 50µs 统一注入。

**注意**：`finalProcessBoundUs` 测量窗口在新流程中覆盖摇杆插件显式调用（processAnalog + processTravelKey）+ 双向交换 + axisTilt + memcpy（hotkey 已移到 sleep 前，非摇杆插件已在 ProcessAddons 中提前执行完毕），测量自动正确。`postprocessBoundUs` 测量窗口覆盖 `restoreGateSPIProfile + processCompositeHID + tud_task + PostprocessAddons`（见步骤 8 修正）。

### 步骤 7：新增 sleep 逻辑

**文件**：[src/gp2040.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp)

**7a. sleep 辅助函数**（在 `sampleMainLoopGateLateAnalog` 之前新增）：

```cpp
// 在 LOCKED + valid schedule 时，sleep 到晚采样的最晚安全启动时刻。
// sleep 目标 = finalizeDeadlineUs - sampleBoundUs（原始 WCET，安全余量已在 armDeadline 层注入）
// 若剩余时间不足（非摇杆工作已超时），跳过 sleep 立即采样。
static void sleepUntilLateSampleDeadline() {
        if (main_loop_gate_state != MainLoopGateState::LOCKED ||
                !main_loop_gate_frame_schedule.valid) {
                return;
        }
        MainLoopGateRollingMax* adcWcet =
                mainLoopGateADCWcet(main_loop_gate_analog_source);
        MainLoopGateRollingMax* burstSetupWcet =
                mainLoopGateADCBurstSetupWcet(main_loop_gate_analog_source);
        if (adcWcet == nullptr || burstSetupWcet == nullptr) {
                return;
        }
        // 使用原始 WCET 最大值；50µs 安全余量已在 armDeadline 层注入（步骤 6），
        // 经 finalizeDeadlineUs 级联至 sleepTargetUs，无需在此额外扣减
        const uint32_t sampleBoundUs =
                burstSetupWcet->maximum + adcWcet->maximum;
        const uint32_t sleepTargetUs =
                main_loop_gate_frame_schedule.finalizeDeadlineUs - sampleBoundUs;
        const uint32_t nowUs = time_us_32();
        if (static_cast<int32_t>(sleepTargetUs - nowUs) <= 0) {
                return;  // 非摇杆工作已超时，跳过 sleep
        }
        if (!mainLoopGateEventPending()) {
                best_effort_wfe_or_timeout(
                        make_timeout_time_us(sleepTargetUs));
        }
}
```

**注意**：原步骤 7a 的 `MAIN_LOOP_GATE_SLEEP_SAFETY_MARGIN_US = 16` 已移除——安全余量统一为 `MAIN_LOOP_GATE_SAFETY_MARGIN_US = 50µs`（步骤 5d），在 armDeadline 层注入后级联至 finalizeDeadlineUs 和 sleepTargetUs，无需在 sleep 函数内额外扣减。

### 步骤 8：主循环体重构

**文件**：[src/gp2040.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp)（行 1402-1531）

将当前流程：
```
PreprocessGateEarlyAddons → gamepad->process() → sampleMainLoopGateLateAnalog
→ ProcessAddons → hotkey → 交换 → axisTilt → memcpy → inputDriver → tud_task → PostprocessAddons
```

改为：
```cpp
// ===== 非摇杆工作阶段（sleep 前）=====

// Preprocess：MCP3208 preprocess 已清空（早采样移除）
if (splitGateFrame) {
    addons.PreprocessGateEarlyAddons();
} else {
    addons.PreprocessAddons();
}

gamepad->process(); // MPGS

// ProcessAddons 全量遍历：25 个插件中
//   - UnifiedAnalogProcessor.process() → 空跑（逻辑在 processAnalog）
//   - UnifiedJoystickTravelKey.process() → 空跑（逻辑在 processTravelKey）
//   - AxisTiltOverlay.process() → 空跑（逻辑在 applyFinalProcess）
//   - 其余 22 个插件正常执行
addons.ProcessAddons();

// 热键处理移到 sleep 前：
// - hotkey() 读 buttons/dpad（数字量），不依赖摇杆采样
// - HOTKEY_APPLY_CURVE_PRESET_* 修改 AnalogOptions，移到 processAnalog() 前
//   可使曲线预设变更当帧生效（原流程下一帧才生效）
// - HOTKEY_SAVE_CONFIG 等阻塞边缘情况（Flash 写）会吃掉 sleep 时间，
//   但 sleepUntilLateSampleDeadline 会检测剩余时间不足并跳过 sleep
gamepad->hotkey();
rebootHotkeys.process(configMode);

// ===== sleep 阶段（仅 LOCKED 态）=====

if (splitGateFrame) {
    sleepUntilLateSampleDeadline();
}

// ===== 采样阶段 =====

// 无条件调用：门控模式下 deadlineMode=true（走 deadline 检查分支，单次采样）；
// 非门控模式下 deadlineMode=false（跳过 deadline 检查，仍单次采样）。
// 二者均完成一次 burst + sample + end，publishStickSnapshot 更新原子快照。
MainLoopGateLateSampleResult lateSample;
lateSample = sampleMainLoopGateLateAnalog(addons);

const uint32_t finalProcessStartUs =
        splitGateFrame ? time_us_32() : 0;

// ===== 摇杆后处理阶段（采样后显式调用）=====

// 与 AxisTiltOverlay.applyFinalProcess 同模式：GetAddon + 显式调用
UnifiedAnalogProcessorAddon* analogProc =
        (UnifiedAnalogProcessorAddon*)addons.GetAddon(UnifiedAnalogProcessorName);
if (analogProc != nullptr) {
    analogProc->processAnalog();
}

UnifiedJoystickTravelKeyAddon* travelKey =
        (UnifiedJoystickTravelKeyAddon*)addons.GetAddon(UnifiedJoystickTravelKeyName);
if (travelKey != nullptr) {
    travelKey->processTravelKey();
}

// ===== 后续不变 =====

// 双向交换（行 1436-1466 不变）

// AxisTiltOverlay 显式调用（行 1469-1471，已有，不变）
AxisTiltOverlayInput* axisTiltOverlay =
        (AxisTiltOverlayInput*)addons.GetAddon(AxisTiltOverlayName);
if (axisTiltOverlay != nullptr) {
    axisTiltOverlay->applyFinalProcess(gamepad);
}

checkProcessedState(processedGamepad->state, gamepad->state);
memcpy(&processedGamepad->state, &gamepad->state, sizeof(GamepadState));

// USB suppress（行 1482-1495 不变）

// Process Input Driver
const uint32_t endpointArmStartUs =
        splitGateFrame ? time_us_32() : 0;
bool processed = inputDriver->process(gamepad);
const bool reportArmed = mainLoopGateReportAttempted(processed);
const uint32_t endpointArmEndUs =
        splitGateFrame ? time_us_32() : 0;

if (splitGateFrame) {
    recordMainLoopGateFrameTiming(
            addons, lateSample, finalProcessStartUs,
            endpointArmStartUs, endpointArmEndUs, reportArmed);
}

// ===== Postprocess 窗口（WCET 覆盖完整）=====
// postprocessStartUs 前移至 restoreGateSPIProfile 之前：
// 原 code 中 postprocessStartUs 在 tud_task 之后、PostprocessAddons 之前打点，
// 漏计了 restoreGateSPIProfile + processCompositeHID + suppress 恢复 + tud_task。
// 修正后 postprocess WCET 覆盖：restoreGateSPIProfile + processCompositeHID
// + suppress 恢复 + tud_task + PostprocessAddons 全部耗时
const uint32_t postprocessStartUs =
        splitGateFrame ? time_us_32() : 0;

if (lateSample.busTouched) {
    LSM6DSRIMUAddon::restoreGateSPIProfile();
}
if (composite_hid_enabled) {
    processCompositeHID(gamepad);
}

if (suppressUsb) {
    memcpy(&gamepad->state, &savedUsbState, sizeof(GamepadState));
}

tud_task();

addons.PostprocessAddons(processed);

if (splitGateFrame) {
    const uint32_t postprocessEndUs = time_us_32();
    uint32_t postprocessDurationUs = postprocessEndUs - postprocessStartUs;
    if (postprocessDurationUs == 0) postprocessDurationUs = 1;
    main_loop_gate_postprocess_wcet.record(postprocessDurationUs);
}

checkSaveRebootState();
```

### 步骤 9：更新 stats 查询

**文件**：[src/gp2040.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp)（行 287-327 `getMainLoopGateStats`）

新增 postprocess WCET 输出：

```cpp
stats->postprocessWcetUs = main_loop_gate_postprocess_wcet.maximum;
```

需在 `MainLoopGateStats` 结构体（若有定义在 header 中）新增对应字段。

## 时间模型验证

### LOCKED 态帧时间线

```
T_in: USB IN 完成（帧起点 = T1）
│
├─ GPIO 去抖 + gamepad->read()
├─ PreprocessAddons（MCP3208 preprocess 已空）
├─ gamepad->process()
├─ ProcessAddons（全量，3个摇杆插件 process 空跑，22个正常）
│    └─ MCP3208.process() → 空跑（采样已统一至晚采样）
│    └─ 其余 22 个非摇杆插件正常执行              ← 非摇杆工作
├─ gamepad->hotkey() + rebootHotkeys.process()       ← 热键（sleep 前）= T2
├─ sleep(sleepTargetUs - now)                         ← WFE 低功耗
├─ sampleMainLoopGateLateAnalog()（单次，无条件调用）  ← 晚采样 = T3
├─ processAnalog() + processTravelKey()               ← 摇杆后处理
├─ 双向交换 + axisTilt.applyFinalProcess
├─ processedGamepad->state = gamepad->state           ← 构建 state
├─ inputDriver->process()                              ← USB 上报
├─ recordMainLoopGateFrameTiming()
├─ restoreGateSPIProfile + processCompositeHID + suppress 恢复 + tud_task + PostprocessAddons ← T4
│
T_next_in: 下一 IN 完成
```

**时间标记**：T1=帧起点（IN 完成），T2=sleep 前（hotkey 后），T3=采样前（sleep 后），T4=postprocess 结束。

sleep 时长 = `sleepTargetUs - T2` = `1000 - (T2-T1) - (T4-T3 各段 WCET) - 50µs`

### 非门控模式帧时间线

```
帧起点（自由运行，无 IN 令牌同步）
│
├─ GPIO 去抖 + gamepad->read()
├─ PreprocessAddons（MCP3208 preprocess 已空）
├─ gamepad->process()
├─ ProcessAddons（全量，3个摇杆插件 process 空跑，22个正常）
├─ gamepad->hotkey() + rebootHotkeys.process()
├─ sleepUntilLateSampleDeadline() → 直接返回（非 LOCKED 态，跳过 sleep）
├─ sampleMainLoopGateLateAnalog()（单次，deadlineMode=false
│    跳过 deadline 检查，直接 burst + sample + end）
├─ processAnalog() + processTravelKey()
├─ 双向交换 + axisTilt.applyFinalProcess
├─ processedGamepad->state = gamepad->state
├─ inputDriver->process()（USB 未挂载时无实质上报）
├─ restoreGateSPIProfile（若 busTouched）
├─ processCompositeHID（若 enabled）
├─ suppress 恢复（若 suppressUsb）
├─ tud_task()
├─ PostprocessAddons()（uart/nrf 发包）
│
下一帧（无固定间隔，由 CPU 速度决定）
```

### deadline 约束链

```
nextTokenEarliestUs = T_in + 1000 + phase
armDeadlineUs       = nextTokenEarliestUs - postprocessBoundUs - SAFETY_MARGIN_US(50µs)
finalizeDeadlineUs  = armDeadlineUs - finalProcessBoundUs - endpointArmBoundUs
sleepTargetUs       = finalizeDeadlineUs - sampleBoundUs
```

各 Bound 含义（均使用原始 WCET `maximum`，无 per-bound margin）：

| Bound | 测量窗口 | 覆盖操作 |
|-------|---------|---------|
| `finalProcessBoundUs` | finalProcessStartUs → endpointArmStartUs | processAnalog + processTravelKey + 交换 + axisTilt + memcpy + USB suppress（hotkey 已移到 sleep 前，不含于此窗口）|
| `endpointArmBoundUs` | endpointArmStartUs → endpointArmEndUs | inputDriver->process() |
| `postprocessBoundUs` | postprocessStartUs → postprocessEndUs | restoreGateSPIProfile + processCompositeHID + suppress 恢复 + tud_task + PostprocessAddons (uart/nrf) |
| `sampleBoundUs` | burstSetup WCET + sample WCET（含 EndBurst） | BeginBurst + Sample + EndBurst |

### 安全余量

- `MAIN_LOOP_GATE_SAFETY_MARGIN_US = 50µs`（单一常量，在 armDeadline 层统一注入）
- 覆盖：WCET 测量不确定性（滚动最大值 vs 真实最坏情况）+ WFE 唤醒延迟（IRQ latency）+ USB 端点就绪抖动 + 未测量开销
- 各 Bound 使用原始 WCET `maximum`，无额外 per-bound margin

在 1000µs 帧中，预留 50µs 安全余量后仍有 950µs 可用于实际工作（含 sleep）。

### 执行顺序保证

三个摇杆相关插件的显式调用顺序：

```
processAnalog()          ← 曲线/死区/抖动量化，写 gamepad->state.lx/ly/rx/ry
processTravelKey()       ← 读 gamepad->state 触发行程按键
applyFinalProcess()     ← RC 抖动叠加到 gamepad->state（已有，行 1471）
```

与当前 ProcessAddons 遍历顺序一致（UnifiedAnalogProcessor #3 → UnifiedJoystickTravelKey #4 → AxisTiltOverlay applyFinalProcess 行 1471）。

## 验证步骤

### 编译验证

```bash
cd /home/leonxis/GP2040/GP2040-CE
cmake --build build-hml2354 --target GP2040-CE 2>&1 | tail -20
```

### 功能验证（需硬件）

1. **门控模式（USB 连接，1000Hz）**：
   - 确认 LOCKED 状态稳定进入（`stableCompletions` 达到 128）
   - 确认 deadline miss 计数不持续增长
   - 确认 USB 上报间隔稳定 1ms
   - 确认摇杆数据新鲜（快速移动摇杆，host 端无延迟感）

2. **非门控模式（无线/蓝牙）**：
   - 确认主循环自由运行（无 WaitUSB 卡死）
   - 确认 uart/nrf 发包频率正常（50ms 心跳 + 模拟量变化触发）
   - 确认摇杆数据有更新（`sampleMainLoopGateLateAnalog()` 无条件调用，`deadlineMode=false` 时单次采样生效）

3. **nRF24 直连模式（USB 未挂载）**：
   - 确认 `wirelessLinkActive()` 返回 true（已修复）
   - 确认门控降级为 RunFrame，PostprocessAddons 正常执行

4. **postprocess WCET 测量**：
   - `getMainLoopGateStats` → `postprocessWcetUs` 应有非零值
   - 确认 postprocessWcetUs 覆盖 `restoreGateSPIProfile + processCompositeHID + tud_task + PostprocessAddons` 全部耗时（postprocessStartUs 前移至 tud_task 之前）
   - uart/nrf 启用时 postprocessWcetUs 应明显增大（含发送耗时）
   - `MAIN_LOOP_GATE_SAFETY_MARGIN_US = 50µs` 应足够覆盖 WCET 抖动——若 deadline miss 计数持续增长，需调查 postprocessWcetUs 是否异常偏大

## RP2040 分支移植注意

本文档记录的修改逻辑同样适用于 RP2040 分支，移植时需注意：

1. RP2040 的 `build/` 目录当前为陈旧环境（缺 BoardConfig），需使用正确的构建目录
2. `MainLoopGateRollingMax` 容器、`best_effort_wfe_or_timeout` 等基础设施在 RP2040 上行为一致
3. 若 RP2040 分支不含 MCP3208 插件（仅板载 ADC），`sampleMainLoopGateLateAnalog` 的 gateLateAnalogProvider 为 null，sleep 逻辑需退化为无采样等待（或基于 AnalogInput 的 process 耗时计算）
4. 非门控模式采样统一通过 `sampleMainLoopGateLateAnalog()` 无条件调用完成（`deadlineMode=false` 分支跳过 deadline 检查，直接单次采样）。RP2040 板载 ADC 若不实现 `GateLateAnalogProvider` 接口，需在主循环中为非门控模式提供等效的单次采样路径（AnalogInput::process() 本身即采样，保持其 `process()` 非空即可），或让 `sampleMainLoopGateLateAnalog()` 在 provider=null 时优雅降级为 no-op 并由 AnalogInput 自行采样
5. 显式调用模式（processAnalog/processTravelKey/applyFinalProcess）不依赖具体 ADC 源，RP2040 分支可同样适用

## 假设与决策

1. **不拆分 ProcessAddons()**：采用 AxisTiltOverlay 显式调用模式，ProcessAddons 全量遍历不变，3 个摇杆插件的 process() 空跑
2. **AxisTiltOverlay 无需改动**：已是显式调用模式，applyFinalProcess 在行 1471 显式调用
3. **非门控模式采样由 `sampleMainLoopGateLateAnalog()` 无条件调用承担**：`deadlineMode=false` 时跳过 deadline 检查但仍执行一次 burst + sample + end，`publishedStickSnapshot_` 每帧更新。非门控帧率 >1kHz，数据足够新鲜
4. **sleep 使用 WFE + 50µs 安全余量**：WFE 可被 USB IRQ 早唤醒或晚唤醒，50µs 统一安全余量覆盖唤醒抖动 + WCET 不确定性 + 端点就绪抖动 + 未测量开销。原 layered margins（5×16 + 16 + 16 = 112µs）过保守，统一为 50µs 释放更多 sleep 预算
5. **postprocessBoundUs 纳入 armDeadline**：postprocess WCET 窗口覆盖 `restoreGateSPIProfile + processCompositeHID + tud_task + PostprocessAddons`（postprocessStartUs 前移修正），endpoint arm 完成后全部后续工作必须在下一 IN 令牌前完成，否则帧率退化
6. **MCP3208.process() 保持为空**：无冗余采样。采样统一在 `sampleMainLoopGateLateAnalog()`（门控/非门控共用同一入口），`preprocess()` 和 `process()` 均为空，避免重复 SPI 突发
7. **EndGateLateAnalogBurst 纳入 sample WCET**：`EndGateLateAnalogBurst()` 移至 `adcWcet->record()` 之前，sample WCET 覆盖完整 burst 生命周期（Begin + Sample + End），消除原测量漏项
