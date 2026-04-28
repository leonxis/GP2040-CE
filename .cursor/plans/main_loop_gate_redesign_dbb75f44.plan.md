---
name: Main loop gate redesign
overview: 采用分阶段策略：第一阶段先落地“同一轮询完成计数器（IN成功或NAK近似）单触发门控”并验证；第二阶段再按结果决定是否引入双缓冲报文发布机制。
todos:
  - id: a-observe-gate
    content: 梳理并统一主循环门控依赖计数器，将现有IN完成计数升级为“轮询完成计数（IN完成或NAK近似）”
    status: completed
  - id: b-gate-machine
    content: 改造 gp2040 主循环门控：仅以轮询完成计数增长触发 + 门控总开关控制，移除SOF兜底分支
    status: completed
  - id: c-nak-accounting
    content: 通过 usbdriver 统一 notify 接口接入计数：仅覆盖“IN成功完成”与“未启动传输(not-ready/busy)前置判定”；已启动+失败回调路径完全排除
    status: completed
  - id: d-gating-switch
    content: 复用 main_loop_gate_enabled 作为唯一总开关，统一控制门控判断与计数器递增；在P5G/验证器模式下完全禁用
    status: completed
  - id: e-validate-tune
    content: 覆盖IN成功、NAK近似、非NAK失败、P5G禁用门控场景验证，确认主循环触发与负载目标
    status: completed
  - id: f-phase2-double-buffer
    content: 第二阶段候选：基于第一阶段测试结果决定是否落地双缓冲（latestReady/lastSent）与一致性发布机制
    status: completed
isProject: false
---

# 主循环门控与轮询计数合并计划

## 目标

- 使用同一个“轮询完成计数器”作为主循环触发依据：`IN传输完成` 与 `not-ready/busy 导致未启动传输` 都递增该计数器，语义统一为“本帧轮询已完成”。
- 移除 SOF 兜底/健康检查逻辑，仅保留 `IN+NAK` 单一触发源，保持严格触发语义并最大化降低门控负载。
- 引入统一门控总开关：在 `P5G` 以及后续验证器模式中彻底关闭门控路径与相关统计，降低负载。

## 改造范围

- 主循环门控状态机与计数触发逻辑：`[/home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp](/home/leonxis/GP2040/GP2040-CE/src/gp2040.cpp)`
- USB计数接口：`[/home/leonxis/GP2040/GP2040-CE/src/usbdriver.cpp](/home/leonxis/GP2040/GP2040-CE/src/usbdriver.cpp)`, `[/home/leonxis/GP2040/GP2040-CE/headers/usbdriver.h](/home/leonxis/GP2040/GP2040-CE/headers/usbdriver.h)`
- NAK近似与失败路径接入：`[/home/leonxis/GP2040/GP2040-CE/src/drivers/xinput/XInputDriver.cpp](/home/leonxis/GP2040/GP2040-CE/src/drivers/xinput/XInputDriver.cpp)` 及 HID上报路径（`usbdriver` 及对应驱动调用处）
- 第二阶段（可选）双缓冲范围：`[/home/leonxis/GP2040/GP2040-CE/headers/gpdriver.h](/home/leonxis/GP2040/GP2040-CE/headers/gpdriver.h)` 与目标输入驱动（`XInput/PS4/PS4B/SwitchPro`）的上报路径。

## 目标状态（架构）

```mermaid
flowchart TD
  inComplete[IN完成事件] --> pollDoneCounter[轮询完成计数器]
  notReady[端点busy或not-ready(未启动传输)] --> pollDoneCounter
  pollDoneCounter --> gateState
  gateState -->|runFrame| runMain[执行主循环一帧]
  gateState -->|gateDisabled| alwaysRun[门控关闭时按原路径运行]
```

## 关键设计

- **触发策略**
  - 主触发：比较“轮询完成计数器”是否增长，增长则启动主循环一帧。
  - 计数来源：`IN传输完成` 或 `not-ready/busy` 导致未启动传输的前置判定事件，均递增同一计数器。
  - 已启动+失败回调路径不计入触发计数器（无论 `xferred_len`），不新增失败统计要求。
  - 不引入 SOF 参与门控触发：主循环是否放行仅由统一计数器变化决定。
- **统一接口约束（必须落实）**
  - 所有主循环触发相关计数统一通过 `usbdriver` 提供的 notify/getter 接口更新与读取，避免驱动侧直接操作计数变量。
  - 成功路径通过统一 `poll_done_success` 类接口递增计数；未启动传输路径通过统一 `poll_done_not_ready` 类接口递增计数。
  - `not-ready/busy`（未启动传输）为唯一 NAK近似来源；已启动+失败回调路径直接排除，不触发计数递增。
  - 对 `not-ready/busy` 场景需加入“每帧最多记一次”的去重约束，避免主循环高频循环导致同帧重复递增。
- **竞争与边界处理（必须落实）**
  - 事件竞争处理：在同一轮门控决策中，先处理计数快照更新再决定放行，避免并发回调导致重复放行或漏放行。
  - 采样窗口一致性：计数更新与主循环读取采用快照比较（上一快照 vs 当前快照）完成最终判定，避免回调与主循环并发读写导致误判。
  - 模式切换复位：`main_loop_gate_enabled` 开关变化、USB重枚举、驱动切换、configMode/P5G/验证器切换时，必须统一复位 last_counter 快照与去重状态，防止切换瞬间假阳性触发。
- **门控总开关**
  - 复用现有 `main_loop_gate_enabled` 作为唯一总开关，覆盖门控判定与计数器递增路径控制。
  - `P5G` 与后续验证器模式中 `main_loop_gate_enabled=false`：不执行门控相关判断与计数，降低CPU负载。
- **兼容与语义**
  - 取消独立“IN成功计数”保留策略，仅维护统一轮询完成计数器，主循环触发只依赖该单一计数源，减少计数开销与状态维护复杂度。
  - 不依赖 TinyUSB 提供显式 NAK 回调，触发计数仅基于成功完成与未启动传输前置判定实现。

## 分阶段实施

1. **阶段A（第一阶段）：统一计数器改造**
  - 将主循环读取计数统一到一个“轮询完成计数器”接口，移除并替换旧的独立 IN 成功触发计数路径。
  - 在现有 IN 完成回调处继续递增统一计数器，保证成功路径行为不退化。
2. **阶段B（第一阶段）：NAK近似接入（HID + XInput）**
  - 在 HID 与 XInput 发送前 `not-ready/busy` 判定路径补充递增，确保“未启动传输”场景不漏记。
  - 已启动+失败回调路径全部排除出触发计数器，不新增失败统计逻辑，避免污染主触发语义并降低开销。
  - 增加“每帧最多一次”去重机制，避免高频循环单帧重复递增。
3. **阶段C（第一阶段）：主循环触发切换**
  - `gp2040.cpp` 使用统一计数器增量作为 `runFrame` 首要触发条件。
  - 移除 SOF 相关门控分支，仅保留统一计数器变化触发，简化主循环判定与执行路径。
4. **阶段D（第一阶段）：门控总开关落地**
  - 统一复用 `main_loop_gate_enabled` 判定，并在P5G/验证器模式确保门控与统计全关闭。
  - 门控关闭时不执行任何门控相关计算，减少无效分支与开销。
5. **阶段E（第一阶段）：竞争与复位边界加固**
  - 落实“先更新计数快照再判定放行”的顺序与快照机制，验证无误判。
  - 完成模式切换/重枚举复位逻辑，确保切换瞬间不会误触发。
6. **阶段F（第一阶段）：联调与回归**
  - 覆盖成功、not-ready/busy未启动、已启动失败不计数、门控关闭四类核心场景。
  - 对比改造前后主循环稳定性与CPU占用，确认简化后负载收益。
7. **阶段G（第二阶段，按结果决策）：双缓冲改造**
  - 触发条件：仅在第一阶段完成并经测试确认仍存在可观测问题（如报文撕裂风险、相位错位导致的可见抖动、回包稳定性不足）时启动。
  - 方案内容：引入 `latestReady/lastSent` 双缓冲与发布一致性机制，保证发送侧读取稳定快照，必要时在跳帧/未更新场景重发 `lastSent`。
  - 范围控制：仅改目标驱动上报主路径，不扩散到全部输入模式。

## 验证与验收标准

- 在 1kHz 目标下，主循环可由“IN成功或not-ready/busy未启动”任一事件触发，帧推进无明显空洞。
- 当端点 `not-ready/busy` 导致未启动传输时，仍能通过前置判定路径递增统一计数器，且同一帧内不会重复递增。
- 已启动+失败回调（任意 `xferred_len`）完全排除，不计入统一触发计数器且不新增统计逻辑。
- `P5G` 与验证器模式下门控逻辑完全不生效：计数不更新、门控判定不执行。
- 不引入 `PS4/PS4B/SwitchPro/XInput/XInputB` 输入模式回归，保持现有功能行为。
- 模式切换与重枚举边界下无误触发：切换后第一帧不出现因脏快照导致的虚假触发。
- 第二阶段决策门槛明确：只有第一阶段验收后仍存在已定义问题，才进入双缓冲实现；否则不增加双缓冲复杂度与维护成本。

## 风险与回退

- **语义风险**：`not-ready/busy` 作为 NAK近似属于工程启发式，不是 USB 硬件精确NAK事件。
- **控制**：不引入基于SOF的“长期不增长告警”判定逻辑；已启动+失败回调路径按非触发事件处理。无SOF后的故障恢复由外部机制处置，本项目代码不新增故障自恢复逻辑。
- **实现策略**：直接替换主门控触发计数来源，不保留并行旧门控路径，降低复杂度。
- **回退**：依赖 Git 提交回退（revert 到改造前提交）作为唯一回退手段，不在运行时代码中保留旧逻辑。
