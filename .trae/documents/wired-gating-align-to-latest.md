# 有线门控逻辑对齐到 wireless-tx-2354 最新门控

## Context

`wireless-tx` 分支的主循环门控逻辑仍处于重构前状态（早采样 + while-loop 多次晚采样 + layered margins 112µs + 无 sleep + 无 postprocess WCET）。`wireless-tx-2354` 分支已落地两个关键改造：

1. **f67b6bae** — 主循环门控重构：移除早采样 → sleep-then-single-sample + 显式调用摇杆插件 + postprocess WCET + 统一 50µs 安全余量
2. **1165cdb6** — 门控 bound 降级（方案B）：RollingMax 新增 secondMaximum()，maximum 不可调度时降级为窗口次大值

本分支不含 3 个无线/蓝牙模式（nRF24/BLE/UART），因此**不需要时间触发门控路径**（TIME_LOCKED / main_loop_gate_time_triggered / prepareMainLoopGateTimeTriggeredFrameSchedule / MAIN_LOOP_GATE_WIRELESS_FRAME_INTERVAL_US），仅对齐**有线 USB IN 令牌门控路径**。

**wireless-tx 关键差异（vs wireless-tx-2354）需适配**：
- 有 ADS8332 + MCP3208 双 ADC（wcet 容器/switch 分发各有 ADS8332 case）
- 有 UnifiedVoltageSwitch 插件，依赖 MCP3208/ADS8332 的 divider 通道（CH2/CH5）采样
- MCP3208/ADS8332 的 `preprocess()` 不能完全清空——需保留 divider 通道采样（仅移除 stick snapshot 早采样）

**UnifiedVoltageSwitch 不需要改为显式调用**：
- `process()` 已为空（`virtual void process() {}`），所有逻辑在 `preprocess()` 中
- 消费 divider 通道数据（非 stick snapshot），divider 在 preprocess 阶段由 ADC 插件采样
- preprocess 在 `hotkey()` 之前执行，时序正确（注释说明 "Must run in preprocess so outputs are merged before hotkey()"）
- 重构不改变 preprocess 与 hotkey 的相对时序

**divider 与 stick 采样时序分离（有意为之）**：
- divider（CH2/CH5）：preprocess 阶段采样（sleep 前），早于 hotkey()，供 UnifiedVoltageSwitch 使用
- stick（CH0/CH1/CH6/CH7）：晚采样阶段（sleep 后），紧贴 USB 上报，保证新鲜度
- `sampleGateLateAnalog()` 只调 `sampleStickSnapshot()`，不含 divider 读取

**ADS8332 SPI 取舍（已确认接受）**：
- MCP3208：stick 和 divider 本就是独立 SPI 事务，重构不增加 SPI 次数
- ADS8332：原始 `preprocess()` 用一次 `readAllChannelsOptimizedUnique` 合并读取 stick+divider；重构后非门控模式每 4 帧多 1 次 SPI（divider 独立读 ~20µs）
- 这与门控模式一致（`preprocessGateEarly` + `sampleGateLateAnalog` 本就分离），重构统一了两种模式的采样路径

## 修改计划

### 1. headers/gp2040.h — MainLoopGateStats 新增字段

在 `finalizeDeadlineUs` 后新增：
```cpp
uint32_t postprocessWcetUs;   // f67b6bae
uint32_t boundDegradedCount;  // 1165cdb6
```

### 2. UnifiedAnalogProcessor — 显式调用模式

**headers/addons/unified_analog_processor.h**：`process()` 声明后加 `virtual void processAnalog();`

**src/addons/unified_analog_processor.cpp**：
- 行 160 `void UnifiedAnalogProcessorAddon::process()` → 改名为 `processAnalog()`
- 行 318（`getInterpolatedScale` 之前）新增空 `process()`：
```cpp
void UnifiedAnalogProcessorAddon::process() {
    // 空跑：实际逻辑在 processAnalog() 中由主循环在晚采样后显式调用
}
```

### 3. UnifiedJoystickTravelKey — 显式调用模式

**headers/addons/unified_joystick_travel_key.h**：`process()` 声明后加 `virtual void processTravelKey();`

**src/addons/unified_joystick_travel_key.cpp**：
- 行 134 `void UnifiedJoystickTravelKeyAddon::process()` → 改名为 `processTravelKey()`
- 文件末尾新增空 `process()`：
```cpp
void UnifiedJoystickTravelKeyAddon::process() {
    // 空跑：实际逻辑在 processTravelKey() 中由主循环在晚采样后显式调用
}
```

### 4. MCP3208 + ADS8332 — 移除 stick 早采样，保留 divider 采样

**wireless-tx 适配**：与 wireless-tx-2354 不同，本分支 `preprocess()` 不能完全清空，因为 UnifiedVoltageSwitch 依赖 divider 通道采样。仅移除 stick snapshot 早采样。

**src/addons/mcp3208_adc.cpp** 行 244 `preprocess()`：
```cpp
void MCP3208ADCAddon::preprocess() {
    // stick 早采样已移除：采样统一在 sampleMainLoopGateLateAnalog() 中进行。
    // 仅保留 CH2/CH5 divider 通道采样（UnifiedVoltageSwitch 依赖），与 preprocessGateEarly() 一致。
    if (!spiOk_) return;
    if (++ch25_sample_counter_ >= CH25_SAMPLE_DIVIDER) {
        ch25_sample_counter_ = 0;
        (void)sampleSwitchChannels();
    }
}
```

**src/addons/ads8332_adc.cpp** 行 161 `preprocess()`：
```cpp
void ADS8332ADCAddon::preprocess() {
    // stick 早采样已移除：采样统一在 sampleMainLoopGateLateAnalog() 中进行。
    // 仅保留 divider 通道采样（UnifiedVoltageSwitch 依赖），与 preprocessGateEarly() 一致。
    if (!spiOk_) {
        return;
    }
    if ((dividerSampleFrameCounter_++ & 0x03u) == 0u) {
        (void)sampleDividerChannels();
    }
}
```

### 5. src/gp2040.cpp — 门控主逻辑重构

#### 5a. 常量替换（行 73-74）
```cpp
// 替换 MAIN_LOOP_GATE_ENDPOINT_READY_GUARD_US + MAIN_LOOP_GATE_WCET_MARGIN_US 为：
static const uint32_t MAIN_LOOP_GATE_SAFETY_MARGIN_US = 50;
```

#### 5b. RollingMax 新增 secondMaximum()（行 167 前 `};`）
```cpp
// 窗口次大值（重复最大值也算）：bound 最大值超限时降级调度
uint32_t secondMaximum() const {
    if (count < 2) return maximum;
    uint32_t best = 0;
    uint32_t second = 0;
    for (uint8_t i = 0; i < count; i++) {
        const uint32_t v = samples[i];
        if (v >= best) { second = best; best = v; }
        else if (v > second) { second = v; }
    }
    return second;
}
```

#### 5c. 新增静态实例 + 计数器（行 202 后）
```cpp
static MainLoopGateRollingMax main_loop_gate_postprocess_wcet;
// ... 在 main_loop_gate_max_sample_sets_per_frame 后：
static uint32_t main_loop_gate_bound_degraded_count = 0;
```

#### 5d. mainLoopGateSchedulingMeasurementsReady()（行 258）
新增 `main_loop_gate_postprocess_wcet.maximum == 0` 检查。

#### 5e. resetMainLoopGateMeasurements()（行 272）
新增 `main_loop_gate_postprocess_wcet.reset();` 和 `main_loop_gate_bound_degraded_count = 0;`

#### 5f. getMainLoopGateStats()（行 290）
- `endpointArmGuardUs` 改为 `main_loop_gate_endpoint_arm_wcet.maximum`（去掉 margin）
- 新增 `stats->postprocessWcetUs = main_loop_gate_postprocess_wcet.maximum;`
- 新增 `stats->boundDegradedCount = main_loop_gate_bound_degraded_count;`

#### 5g. 新增 resolveMainLoopGateBounds()（行 429 `prepareMainLoopGateFrameSchedule` 前）
```cpp
struct MainLoopGateResolvedBounds {
    uint32_t finalProcessUs;
    uint32_t endpointArmUs;
    uint32_t postprocessUs;
    bool degraded;
};

static bool resolveMainLoopGateBounds(
    uint32_t intervalUs, MainLoopGateResolvedBounds* out) {
    // 优先 maximum；不可调度时降级为次大值（方案B）
    const uint32_t fpMax = main_loop_gate_final_process_wcet.maximum;
    const uint32_t eaMax = main_loop_gate_endpoint_arm_wcet.maximum;
    const uint32_t ppMax = main_loop_gate_postprocess_wcet.maximum;
    if (fpMax + eaMax + ppMax + MAIN_LOOP_GATE_SAFETY_MARGIN_US < intervalUs) {
        out->finalProcessUs = fpMax; out->endpointArmUs = eaMax;
        out->postprocessUs = ppMax; out->degraded = false;
        return true;
    }
    const uint32_t fpTyp = main_loop_gate_final_process_wcet.secondMaximum();
    const uint32_t eaTyp = main_loop_gate_endpoint_arm_wcet.secondMaximum();
    const uint32_t ppTyp = main_loop_gate_postprocess_wcet.secondMaximum();
    if (fpTyp + eaTyp + ppTyp + MAIN_LOOP_GATE_SAFETY_MARGIN_US >= intervalUs) {
        return false;
    }
    out->finalProcessUs = fpTyp; out->endpointArmUs = eaTyp;
    out->postprocessUs = ppTyp; out->degraded = true;
    return true;
}
```

#### 5h. prepareMainLoopGateFrameSchedule()（行 431）
用 `resolveMainLoopGateBounds(MAIN_LOOP_GATE_USB_FRAME_US, &bounds)` 替换原直接 maximum 计算；degraded 时 `main_loop_gate_bound_degraded_count++`；armDeadline = nextTokenEarliest - postprocessBound - SAFETY_MARGIN；finalizeDeadline = armDeadline - finalProcessBound - endpointArmBound。

#### 5i. 新增 sleepUntilLateSampleDeadline()（行 674 `sampleMainLoopGateLateAnalog` 前）

forward declare `mainLoopGateEventPending()`，然后：
```cpp
static void sleepUntilLateSampleDeadline() {
    if (main_loop_gate_state != MainLoopGateState::LOCKED ||
        !main_loop_gate_frame_schedule.valid) {
        return;
    }
    // ... 获取 adcWcet/burstSetupWcet
    const uint32_t sampleBoundUs = burstSetupWcet->maximum + adcWcet->maximum;
    const uint32_t sleepTargetUs =
        main_loop_gate_frame_schedule.finalizeDeadlineUs - sampleBoundUs;
    if (static_cast<int32_t>(sleepTargetUs - time_us_32()) <= 0) return;
    if (!mainLoopGateEventPending()) {
        best_effort_wfe_or_timeout(make_timeout_time_us(sleepTargetUs));
    }
}
```
**注意**：只检查 `LOCKED`，不检查 `TIME_LOCKED`（无无线模式）。

#### 5j. sampleMainLoopGateLateAnalog()（行 674）
- `deadlineMode` 只检查 `LOCKED`（不检查 TIME_LOCKED）
- 移除 `while(true)` 循环，改为单次采样
- `EndGateLateAnalogBurst()` 移至 `adcWcet->record()` 之前
- deadlineMode 检查中 bound 用原始 `maximum`（不加 WCET_MARGIN_US）
- 移除 `main_loop_gate_repeated_sample_frame_count` 累加

#### 5k. recordMainLoopGateFrameTiming()（行 782）
不加 `main_loop_gate_time_triggered` 分支（无无线模式）。保持原样。

#### 5l. 主循环体 RunFrame（行 1389-1505）

原流程：
```
sampleMainLoopGateLateAnalog(仅splitGateFrame) → ProcessAddons → hotkey → 交换 → axisTilt → memcpy → inputDriver → recordTiming + restoreGateSPIProfile → compositeHID → suppress恢复 → tud_task → PostprocessAddons
```

新流程：
```
ProcessAddons → hotkey → sleepUntilLateSampleDeadline(仅splitGateFrame) → sampleMainLoopGateLateAnalog(无条件) → processAnalog/processTravelKey(显式调用) → 交换 → axisTilt → memcpy → inputDriver → recordTiming → [postprocess窗口: restoreGateSPIProfile + compositeHID + suppress恢复 + tud_task + PostprocessAddons + WCET打点]
```

具体改动：
- 移除原 `lateSample = sampleMainLoopGateLateAnalog(addons)`（仅 splitGateFrame 调用）和 `finalProcessStartUs` 提前打点
- `ProcessAddons()` 调用保持在 `gamepad->process()` 之后
- `hotkey()` + `rebootHotkeys.process()` 移到 sleep 前（原已在 ProcessAddons 后，位置不变）
- 新增 `if (splitGateFrame) { sleepUntilLateSampleDeadline(); }`
- 新增无条件 `lateSample = sampleMainLoopGateLateAnalog(addons);`
- 新增 `finalProcessStartUs = splitGateFrame ? time_us_32() : 0;`
- 新增显式调用 `analogProc->processAnalog()` 和 `travelKey->processTravelKey()`
- 双向交换 + axisTilt + memcpy + suppress 保存 不变
- `inputDriver->process()` + `recordMainLoopGateFrameTiming()` 不变（但 restoreGateSPIProfile 从 recordTiming 块移出）
- 新增 `postprocessStartUs = splitGateFrame ? time_us_32() : 0;`（在 restoreGateSPIProfile 前）
- `restoreGateSPIProfile` + `processCompositeHID` + suppress 恢复 + `tud_task` + `PostprocessAddons` 移入 postprocess 窗口
- 新增 postprocess WCET 打点：`main_loop_gate_postprocess_wcet.record(postprocessDurationUs);`

## 不引入的改动（无线模式专用，跳过）

- `MainLoopGateState::TIME_LOCKED` 状态
- `main_loop_gate_time_triggered` 标志
- `main_loop_gate_last_frame_start_us`
- `prepareMainLoopGateTimeTriggeredFrameSchedule()`
- `MAIN_LOOP_GATE_WIRELESS_FRAME_INTERVAL_US`
- `isMainLoopGateTimeTriggered()`
- `mainLoopGateReportAttempted()` 的 time_triggered 分支
- `recordMainLoopGateFrameTiming()` 的 `!main_loop_gate_time_triggered` 分支
- `mainLoopGateEventPending()` 的 time_triggered 分支
- `getMainLoopGateAction()` 的 TIME_LOCKED case
- nrf24_link 异步 TX 改造（无此插件）

## 验证

```bash
cd /home/leonxis/GP2040/GP2040-CE
cmake --build build --target GP2040-CE 2>&1 | tail -20
```

构建通过后确认：
- `MainLoopGateStats` 含 `postprocessWcetUs` + `boundDegradedCount`
- `sampleMainLoopGateLateAnalog` 无 while-loop
- `sleepUntilLateSampleDeadline` 存在
- `resolveMainLoopGateBounds` + `secondMaximum` 存在
- MCP3208/ADS8332 `preprocess()` 不含 stick snapshot 采样
- `UnifiedAnalogProcessorAddon::processAnalog()` + 空 `process()`
- `UnifiedJoystickTravelKeyAddon::processTravelKey()` + 空 `process()`
```
