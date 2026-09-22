# 无线模式门控启用方案

## 需求正确性分析

### 用户核心论点
> 主循环周期越接近轮询周期 1ms，NAK 可能性越低；无门控的无线主循环自由运行反而增加 NAK。

**正确。** 无线接收器（dongle）以 ~1ms 周期轮询控制器。自由运行的主循环周期不确定，可能恰好在轮询时刻正在构建报文，导致 NAK。将主循环周期锁定到 950µs（略快于 1ms），保证每次轮询时报文已就绪。

### 三个约束的正确性

| 约束 | 正确性 | 说明 |
|------|--------|------|
| 门控在三模式保持运行 | ✓ | BLE 当前被 `shouldUseMainLoopGate` 排除，需移除排除；nRF24/UART 当前在 USB 未挂载时降级为无门控，需改为时间触发 |
| USB 未挂载时主循环仍执行 | ✓ | 当前 L535-542 已处理（降级为 RunFrame），需改为时间触发而非自由运行 |
| 不使用 IN 令牌作为循环起点 | ✓ | 纯无线无 IN 令牌；有线+无线不同主机时 IN 令牌来自错误主机。时间触发（950µs）是正确替代 |
| 后续门控启动后的逻辑无需修改 | **部分正确** | 每帧工作结构（sleep/sample/WCET/postprocess）可复用，但 `mainLoopGateReportAttempted` 和 `recordMainLoopGateFrameTiming` 需适配：USB 未挂载时 `inputDriver->process()` 返回 false，当前会触发 RECOVERY 且不记录 endpoint arm WCET——需在时间触发模式下绕过 |

### 关键发现（探索阶段）

- `XInputDriver::process()` ([XInputDriver.cpp:362-415](file:///home/leonxis/GP2040/GP2040-CE/src/drivers/xinput/XInputDriver.cpp#L362-L415))：`tud_ready()` 为 false 时返回 false
- `PS4Driver::process()` ([PS4Driver.cpp:710-784](file:///home/leonxis/GP2040/GP2040-CE/src/drivers/ps4/PS4Driver.cpp#L710-L784))：`tud_hid_ready()` 为 false 时返回 false
- 因此 USB 未挂载时 `submitted=false` → `mainLoopGateReportAttempted(false)` → 当前触发 RECOVERY，需修改
- `recordMainLoopGateFrameTiming` ([gp2040.cpp:843](file:///home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp#L843))：`if (!reportArmed) return;` 跳过 endpoint arm WCET 记录，需在时间触发模式下绕过

## 修改方案

所有修改集中在 [src/gp2040.cpp](file:///home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp) 单文件。

### 1. 新增常量与状态（L70-82 区域）

```cpp
static const uint32_t MAIN_LOOP_GATE_WIRELESS_FRAME_INTERVAL_US = 950;
```

在 `MainLoopGateState` 枚举（L107）新增：
```cpp
TIME_LOCKED,  // 无线模式：时间触发，不依赖 IN 令牌
```

### 2. 新增静态变量（L63-68 区域）

```cpp
// 无线模式（nRF24/BLE/UART）启用时间触发门控：不依赖 IN 令牌，
// 以 950µs 固定间隔触发帧，使主循环周期接近 1ms 轮询周期，降低 NAK。
static bool main_loop_gate_time_triggered = false;
static uint32_t main_loop_gate_last_frame_start_us = 0;
```

### 3. `shouldUseMainLoopGate()`（L333-351）— 移除 BLE 排除

```cpp
// 修改前
return (...) && supportedMode && !gamepadOptions.bluetoothLinkEnabled;
// 修改后
return (...) && supportedMode;
```

更新注释：BLE 不再排除，改为时间触发模式运行。

### 4. `setup()`（L1200-1201）— 设置时间触发标志

```cpp
main_loop_gate_enabled = shouldUseMainLoopGate();
main_loop_wireless_link_active = wirelessLinkActive();
main_loop_gate_time_triggered = main_loop_wireless_link_active;
```

### 5. `resetMainLoopGateForSnapshot()`（L493-507）— 时间触发模式初始化

在函数末尾修改状态赋值：
```cpp
if (main_loop_gate_time_triggered) {
    main_loop_gate_state = MainLoopGateState::BOOTSTRAP_BUILD;
} else {
    main_loop_gate_state = (snapshot.mounted && !snapshot.suspended)
        ? MainLoopGateState::BOOTSTRAP_BUILD
        : MainLoopGateState::WAIT_MOUNT;
}
main_loop_gate_last_frame_start_us = 0;
```

### 6. `getMainLoopGateAction()`（L520-672）— 时间触发路径

#### 6a. USB 未挂载处理（L535-542）

时间触发模式跳过降级，让状态机处理：
```cpp
if (!snapshot.mounted && !main_loop_gate_time_triggered) {
    // 原有逻辑：非时间触发模式，USB 未挂载时等待或降级
    main_loop_gate_state = MainLoopGateState::WAIT_MOUNT;
    main_loop_suspend_scan_initialized = false;
    if (main_loop_wireless_link_active) {
        main_loop_gate_runtime_enabled = false;
        return MainLoopGateAction::RunFrame;
    }
}
```

#### 6b. USB 挂起处理（L557-567）

时间触发模式跳过挂起态，继续运行无线输出：
```cpp
if (snapshot.suspended && !main_loop_gate_time_triggered) {
    // 原有挂起扫描逻辑
    ...
    return MainLoopGateAction::ScanSuspended;
}
```

#### 6c. failedSeq 处理（L582-587）

时间触发模式不因 USB 提交失败进入 RECOVERY：
```cpp
if (snapshot.failedSeq != main_loop_gate_failed_seq) {
    main_loop_gate_failed_seq = snapshot.failedSeq;
    if (!main_loop_gate_time_triggered) {
        main_loop_gate_first_in_seen = false;
        resetMainLoopGateTiming();
        main_loop_gate_state = MainLoopGateState::RECOVERY;
    }
}
```

#### 6d. 新增 TIME_LOCKED case

在 switch 语句中新增（BOOTSTRAP_BUILD case 也需修改）：
```cpp
case MainLoopGateState::BOOTSTRAP_BUILD:
    main_loop_gate_frame_schedule = {};
    if (main_loop_gate_time_triggered) {
        if (main_loop_gate_last_frame_start_us == 0) {
            main_loop_gate_last_frame_start_us = time_us_32();
        }
    }
    return MainLoopGateAction::RunFrame;

// ... 其他 case 不变 ...

case MainLoopGateState::TIME_LOCKED: {
    const uint32_t now = time_us_32();
    const uint32_t deadline =
        main_loop_gate_last_frame_start_us +
        MAIN_LOOP_GATE_WIRELESS_FRAME_INTERVAL_US;
    if (mainLoopGateTimeReached(now, deadline)) {
        main_loop_gate_last_frame_start_us = now;
        prepareMainLoopGateTimeTriggeredFrameSchedule();
        return MainLoopGateAction::RunFrame;
    }
    return MainLoopGateAction::WaitUSB;
}
```

### 7. 新增 `prepareMainLoopGateTimeTriggeredFrameSchedule()`

在 `prepareMainLoopGateFrameSchedule()` 之后新增，复用相同 WCET 字段和安全余量逻辑，但以 `lastFrameStart + 950` 作为下一帧起点：
```cpp
static void prepareMainLoopGateTimeTriggeredFrameSchedule() {
    main_loop_gate_frame_schedule = {};
    if (main_loop_gate_state != MainLoopGateState::TIME_LOCKED ||
        !mainLoopGateSchedulingMeasurementsReady() ||
        main_loop_gate_last_frame_start_us == 0) {
        return;
    }
    const uint32_t finalProcessBoundUs = main_loop_gate_final_process_wcet.maximum;
    const uint32_t endpointArmBoundUs = main_loop_gate_endpoint_arm_wcet.maximum;
    const uint32_t postprocessBoundUs = main_loop_gate_postprocess_wcet.maximum;
    const uint32_t reservedUs = finalProcessBoundUs + endpointArmBoundUs +
        postprocessBoundUs + MAIN_LOOP_GATE_SAFETY_MARGIN_US;
    if (reservedUs >= MAIN_LOOP_GATE_WIRELESS_FRAME_INTERVAL_US) {
        return;
    }
    // 下一帧起点 = 当前帧启动 + 950µs
    const uint32_t nextFrameStartUs =
        main_loop_gate_last_frame_start_us +
        MAIN_LOOP_GATE_WIRELESS_FRAME_INTERVAL_US;
    const uint32_t armDeadlineUs =
        nextFrameStartUs - postprocessBoundUs - MAIN_LOOP_GATE_SAFETY_MARGIN_US;
    main_loop_gate_frame_schedule.valid = true;
    main_loop_gate_frame_schedule.nextTokenEarliestUs = nextFrameStartUs;
    main_loop_gate_frame_schedule.armDeadlineUs = armDeadlineUs;
    main_loop_gate_frame_schedule.finalizeDeadlineUs =
        armDeadlineUs - finalProcessBoundUs - endpointArmBoundUs;
}
```

### 8. `mainLoopGateReportAttempted()`（L674-707）— 时间触发模式

在函数开头（`runtime_enabled` 检查之后）插入时间触发分支：
```cpp
if (main_loop_gate_time_triggered) {
    if (submitted) {
        (void)usb_mark_main_gamepad_report_submitted(main_loop_gate_action_epoch);
    }
    // BOOTSTRAP_BUILD 首帧后进入 TIME_LOCKED；RECOVERY 恢复到 TIME_LOCKED
    if (main_loop_gate_state == MainLoopGateState::BOOTSTRAP_BUILD ||
        main_loop_gate_state == MainLoopGateState::RECOVERY) {
        main_loop_gate_state = MainLoopGateState::TIME_LOCKED;
        main_loop_gate_frame_schedule = {};
    }
    // 始终返回 true：记录 endpoint arm WCET（inputDriver->process 仍构建报文）
    return true;
}
```

### 9. `recordMainLoopGateFrameTiming()`（L828-881）— 时间触发模式

#### 9a. endpoint arm WCET 记录（L843-845）

```cpp
// 修改前
if (!reportArmed) {
    return;
}
// 修改后
if (!reportArmed && !main_loop_gate_time_triggered) {
    return;
}
```

#### 9b. missedDeadline 处理（L876-880）

时间触发模式不进入 RECOVERY，仅计数：
```cpp
if (missedDeadline) {
    main_loop_gate_deadline_miss_count++;
    if (!main_loop_gate_time_triggered) {
        resetMainLoopGateTiming();
        main_loop_gate_state = MainLoopGateState::RECOVERY;
    }
}
```

### 10. `mainLoopGateEventPending()`（L883-897）— 时间触发模式

```cpp
static bool mainLoopGateEventPending() {
    USBMainGamepadGateSnapshot snapshot = {};
    usb_get_main_gamepad_gate_snapshot(&snapshot);
    if (snapshot.epoch != main_loop_gate_epoch) {
        return true;
    }
    if (main_loop_gate_time_triggered) {
        if (main_loop_gate_state == MainLoopGateState::TIME_LOCKED) {
            return mainLoopGateTimeReached(
                time_us_32(),
                main_loop_gate_last_frame_start_us +
                    MAIN_LOOP_GATE_WIRELESS_FRAME_INTERVAL_US);
        }
        return true;  // 非 TIME_LOCKED 态总有事件待处理
    }
    // 原有 IN 令牌逻辑
    if (main_loop_gate_state == MainLoopGateState::WAIT_MOUNT) {
        return snapshot.mounted;
    }
    return !snapshot.mounted || snapshot.suspended ||
        snapshot.completeSeq != main_loop_gate_complete_seq ||
        snapshot.failedSeq != main_loop_gate_failed_seq ||
        !snapshot.reportArmed;
}
```

### 11. 主循环 WaitUSB 超时（L1338-1346）— 时间触发模式

```cpp
if (gateAction == MainLoopGateAction::WaitUSB) {
    USBHostManager::getInstance().process();
    tud_task();
    if (!mainLoopGateEventPending()) {
        if (main_loop_gate_time_triggered) {
            // 等待到下一帧触发时刻
            const uint32_t remaining = mainLoopGateTimeRemaining(
                time_us_32(),
                main_loop_gate_last_frame_start_us +
                    MAIN_LOOP_GATE_WIRELESS_FRAME_INTERVAL_US);
            best_effort_wfe_or_timeout(make_timeout_time_us(remaining));
        } else {
            best_effort_wfe_or_timeout(
                make_timeout_time_us(MAIN_LOOP_GATE_WAIT_TIMEOUT_US));
        }
    }
    continue;
}
```

### 12. `sleepUntilLateSampleDeadline()`（L715-741）— 支持 TIME_LOCKED

```cpp
// 修改前
if (main_loop_gate_state != MainLoopGateState::LOCKED ||
    !main_loop_gate_frame_schedule.valid) {
    return;
}
// 修改后
if ((main_loop_gate_state != MainLoopGateState::LOCKED &&
     main_loop_gate_state != MainLoopGateState::TIME_LOCKED) ||
    !main_loop_gate_frame_schedule.valid) {
    return;
}
```

### 13. `sampleMainLoopGateLateAnalog()` deadlineMode（L754-756）

```cpp
// 修改前
const bool deadlineMode =
    main_loop_gate_state == MainLoopGateState::LOCKED &&
    main_loop_gate_frame_schedule.valid;
// 修改后
const bool deadlineMode =
    (main_loop_gate_state == MainLoopGateState::LOCKED ||
     main_loop_gate_state == MainLoopGateState::TIME_LOCKED) &&
    main_loop_gate_frame_schedule.valid;
```

### 14. `getMainLoopGateStats()`（L295-297）— 支持 TIME_LOCKED

```cpp
// 修改前
stats->deadlineSchedulingActive =
    main_loop_gate_state == MainLoopGateState::LOCKED &&
    main_loop_gate_frame_schedule.valid;
// 修改后
stats->deadlineSchedulingActive =
    (main_loop_gate_state == MainLoopGateState::LOCKED ||
     main_loop_gate_state == MainLoopGateState::TIME_LOCKED) &&
    main_loop_gate_frame_schedule.valid;
```

## 状态机流程图

### IN 令牌模式（原有，有线专用）
```
WAIT_MOUNT → BOOTSTRAP_BUILD → BOOTSTRAP_SUBMIT → WAIT_FIRST_IN
  → LEARNING → LOCKED
           ↘ RECOVERY ↗
```

### 时间触发模式（新增，无线专用）
```
WAIT_MOUNT → BOOTSTRAP_BUILD → TIME_LOCKED
               (首帧)          (950µs 时间触发循环)
```

- USB 未挂载：不降级，直接进入 BOOTSTRAP_BUILD
- USB 挂起：不进入挂起态，继续 TIME_LOCKED
- IN 令牌：忽略（不作为触发源，不驱动状态转换）
- failedSeq：忽略（不进入 RECOVERY）

## 验证步骤

1. **编译验证**：`cd build-hml2354 && cmake --build .` 确认无编译错误
2. **逻辑验证**（代码审查）：
   - `shouldUseMainLoopGate` 不再排除 BLE
   - 时间触发模式下 USB 未挂载时不降级为无门控
   - TIME_LOCKED 以 950µs 间隔触发帧
   - `mainLoopGateReportAttempted` 时间触发模式不进入 RECOVERY
   - `recordMainLoopGateFrameTiming` 时间触发模式记录 endpoint arm WCET
   - `mainLoopGateEventPending` 时间触发模式检查 950µs 到期
   - WaitUSB 超时为 `lastFrameStart + 950`
3. **WCET 就绪验证**：前几帧 `mainLoopGateSchedulingMeasurementsReady()` 返回 false（WCET 未填充），frame schedule 无效，不 sleep、不 deadline 检查；数帧后 WCET 填充，schedule 生效

## 假设与决策

- **BLE 连接间隔**：用户分析认为 950µs 对 BLE 也适用。若 BLE 实际有缓冲问题，后续可单独调整 `MAIN_LOOP_GATE_WIRELESS_FRAME_INTERVAL_US` 或为 BLE 使用不同间隔
- **USB 挂起 + 无线**：时间触发模式跳过挂起态以保证无线输出连续。USB remote wakeup 在无线模式下不是优先项（无线链路是主要输出）。若有需要可后续补充
- **`inputDriver->process()` 返回 false**：USB 未挂载时驱动返回 false，但仍构建报文。时间触发模式下 `mainLoopGateReportAttempted` 始终返回 true 以记录 WCET
- **SOF ISR**：`tud_sof_isr_set` 仍注册（`main_loop_gate_enabled` 为 true），SOF 回调更新 sof 序列但时间触发模式不使用。无害，保留
