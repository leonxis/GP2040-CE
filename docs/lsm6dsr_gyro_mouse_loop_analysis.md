# LSM6DSR 陀螺仪模拟鼠标：主循环与插件循环频率对精度的影响分析

本文针对 `src/addons/lsm6dsr_imu.cpp` 中**陀螺仪模拟鼠标**（`outputMode == LSM6DSR_OUTPUT_MOUSE`）的实现，分析主循环、插件 preprocess 调用频率与陀螺仪 ODR、USB 回报率之间的异步关系，以及对鼠标精度的影响。

---

## 一、相关代码与数据流

### 1.1 调用链

- **主循环**（`gp2040.cpp` 的 `GP2040::run()`）：  
  `gamepad->read()` → `USBHostManager::process()` → **`addons.PreprocessAddons()`** → `gamepad->process()` → `addons.ProcessAddons()` → `inputDriver->process(gamepad)`（在此处用 `auxState.sensors.mouse.x/y` 填 HID 报告并调用 `tud_hid_n_report`）→ `tud_task()` → 下一帧。

- **陀螺仪插件**：`LSM6DSRIMUAddon::preprocess()` 每帧被调用一次，内部会：
  1. 做**一次** SPI 读取（`spiReadRegs(..., LSM6DSR_OUTX_L_G, readBuf, 12)`），得到当前时刻的角速度 rawG、rawA；
  2. 校准得到 calG，再经一欧元滤波（若开启）；
  3. 若 `outputMode == LSM6DSR_OUTPUT_MOUSE`，调用 `outputGyroToMouse(gamepad, calG)`，根据 calG 计算本帧的 `mouse.x / mouse.y` 并写入 `gamepad->auxState.sensors.mouse`。

- **USB 报告**：PS4B 等驱动在 `inputDriver->process(gamepad)` 里从 `gamepad->auxState.sensors.mouse` 取 `x/y`，填入 HID 鼠标 report 并通过 `tud_hid_n_report` 发送。主机按 bInterval（如 1ms）轮询；设备侧主循环已实现**与回报率对齐**（见下），每帧末尾 sleep 到下一槽位，使“主循环更新一次 report”的节奏与主机轮询一致。

- **主循环与回报率对齐**（`gp2040.cpp`）：在 `run()` 的每帧末尾，使用绝对时间网格 `next_wake_us = main_loop_t0_us + (frame_count+1) * MAIN_LOOP_REPORT_INTERVAL_US` 做 `sleep_us()`，目标周期默认 **1000 µs**（1K Hz）。Config 模式不执行对齐。超时帧不 sleep，不累积漂移；正常时主循环周期 ≈ 1 ms，与 bInterval=1 一致。

### 1.2 关键参数（当前实现）

| 项目 | 值 | 说明 |
|------|-----|------|
| 陀螺仪 ODR | 1666 Hz | LSM6DSR CTRL2_G 配置，传感器内部采样周期 ≈ 0.6 ms |
| 一欧元滤波 Te | 1/1666 s | `LSM6DSR_ONE_EURO_TE_S = 1.0f/1666.0f`，按 1666 Hz 设计 |
| 鼠标换算 | 无 dt | `outputGyroToMouse` 中 `lr_float = lr_raw * GYRO_MOUSE_BASE_SENS * sensLR`，**未乘 delta time** |
| USB 描述符 | bInterval=1 | 全速 1ms 帧 → 主机理论轮询 1000 Hz |
| 主循环目标周期 | MAIN_LOOP_REPORT_INTERVAL_US=1000 | 每帧末尾 sleep 对齐到该周期，与回报率一致 |

---

## 二、主循环与插件循环的实际频率（当前关系）

- **主循环**已实现**与回报率对齐**（类似 Alpakka）：每帧末尾按绝对时间网格 sleep 到 `main_loop_t0_us + (frame_count+1)*MAIN_LOOP_REPORT_INTERVAL_US`，目标周期 1000 µs。因此**正常负载下主循环周期 ≈ 1 ms**，与 USB 1K 回报率一致；单帧或连续超时时不 sleep，不累积漂移，但该段时间内有效更新率会暂时下降。
- **插件“循环”**：陀螺仪插件在 `PreprocessAddons()` 里每主循环一轮调用一次。因此**插件有效执行频率 = 主循环频率**；在未超时前提下，**主循环 ≈ 1K → 插件 ≈ 1K，与回报率一致**；与陀螺仪 ODR 1666 Hz 仍无硬同步。
- **陀螺仪数据**：每次 preprocess 仍只做**一次** SPI 读，得到**一个**角速度样本。因此：
  - 实际用于算鼠标的**陀螺仪采样率 = 主循环频率**（正常时 ≈ 1000 Hz），仍低于 1666 Hz；
  - 每帧只取一个“瞬时值”，对 1666 Hz ODR 仍为**欠采样**；若需更好利用 ODR，需在插件内做多采样或 FIFO（见第六节修复规划）。

结论：**主循环与回报率已对齐**；**插件频率 = 主循环频率**，正常时与 1K 一致。**陀螺仪采样仍为每帧一次**，与 ODR 不一致；**一欧元 Te、鼠标公式是否乘 dt** 等仍按“每帧”设计，需与固定周期 1 ms 一致化（见第六节）。

---

## 三、对模拟鼠标精度的影响

### 3.1 未乘 dt：灵敏度与帧间隔强相关（主循环对齐后部分缓解）

`outputGyroToMouse()` 中：

- `lr_float = (float)lr_raw * GYRO_MOUSE_BASE_SENS * sensLR`（左右类似），然后取整、亚像素累积。
- 角速度 (deg/s) 积分成位移应满足：**位移 ∝ 角速度 × Δt**。当前实现**没有乘以本帧真实 Δt**，等价于假设**每帧时间间隔为 1 个“单位时间”**。
- **主循环已对齐到 1 ms 后**：正常负载下每帧 Δt ≈ 1 ms，若基准灵敏度按 1 ms 标定，则手感大致正确；**超时帧**时本帧 Δt > 1 ms，代码仍按“1 单位”算，该帧位移会偏小，存在少量不一致。
- **修复方向**：要么保持“灵敏度常数按 1 ms 标定”（与 Alpakka 一致，依赖固定周期）；要么在公式中**显式乘 dt**（使用 `MAIN_LOOP_REPORT_INTERVAL_US` 或实际帧间时间），使超时帧也按真实时间积分（见第六节）。

### 3.2 一欧元滤波的 Te 与真实采样间隔不一致（待修复）

- 一欧元滤波里 `alpha = 1/(1 + tau/Te)`，代码中 `Te = LSM6DSR_ONE_EURO_TE_S = 1/1666`，即按** 1666 Hz 采样**设计。
- 实际 preprocess 调用频率 = 主循环频率，**主循环对齐后**正常为 1000 Hz，真实采样间隔 = **1 ms**，即 **Te_actual = 1/1000 s，约为当前 Te 的 1.66 倍**。
- 因此滤波器等效截止与平滑度仍与设计不符；**修复**：Te 应使用**实际采样间隔**（与主循环周期一致，如 `1e-3f` 或 `MAIN_LOOP_REPORT_INTERVAL_US * 1e-6f`），见第六节。

### 3.4 陀螺仪数据利用不足与混叠（可选改进）

- 陀螺仪 ODR 1666 Hz，每帧只读一次，**有效角速度采样率 = 主循环频率**（主循环对齐后正常 ≈ 1000 Hz），仍低于 1666 Hz：
  - 会丢弃部分中间样本，**时间分辨率不如 ODR**；
  - 若手部有较高频率分量，可能产生**欠采样/混叠**。可选在每帧内多采样平均（参考 Alpakka burst）以更好利用 ODR，见第六节。

### 3.5 USB 报告率与“实际更新率”不一致（已缓解）

- 主循环已与回报率对齐，**正常时主循环 ≈ 1K，实际鼠标更新率 = 主循环 = 1K**，与 bInterval 一致。仅在连续超时期间有效更新率会暂时下降，超时结束后重新对齐。

---

## 四、总结与待修复项（主循环对齐后的状态）

| 问题 | 当前状态 | 对精度/手感的影响 |
|------|----------|---------------------|
| 主循环/回报率不一致 | **已解决**：主循环已按绝对时间对齐到 MAIN_LOOP_REPORT_INTERVAL_US | 正常时更新率=1K，与 bInterval 一致 |
| 灵敏度与帧间隔 | **部分缓解**：固定 1 ms 下大致正确；超时帧仍按“1 单位”算，可选项显式 dt | 超时帧位移略偏小，可选修复 |
| 一欧元滤波 Te | **待修复**：Te=1/1666，实际调用间隔=1 ms | 滤波特性与设计不符，需改为实际采样间隔 |
| 陀螺仪欠采样 | **待改进（可选）**：每帧 1 次 SPI，有效 1K < ODR 1666 Hz | 可做每帧多采样或 FIFO 以更好利用 ODR |

**后续修复规划**见**第六节**，仅在分析文档中做修改规划，实现时依该节在 `lsm6dsr_imu.cpp` 中修改。

---

## 五、对比：Alpakka 项目的处理方式

以下基于 `/home/leonxis/alpakka` 中 Alpakka 固件（Input Labs Oy）的实现，分析其如何应对**主循环、回报率与陀螺仪 ODR 不一致**的问题。

### 5.1 主循环：固定节拍（tick） + sleep 补偿

- **位置**：`alpakka_firmware/src/loop.c` 的 `loop_run()`。
- **逻辑**：每轮循环先记录 `start = time_us_32()`，执行 `loop_controller_task()`（其中包含 `profile_report_active()` 和 `hid_report_wired()`），再计算 `used = time_us_32() - start`，得到 `unused = CFG_TICK_INTERVAL_IN_US - used`；若 `unused > 0` 则 `sleep_us(unused)`，否则 `sleep_us(0)`。
- **效果**：主循环周期被**强制对齐到 `CFG_TICK_INTERVAL_IN_US`**（即 `1000000 / CFG_TICK_FREQUENCY`）。控制器为 **250 Hz**（4 ms/帧），Dongle 为 **1000 Hz**（1 ms/帧）。因此**主循环频率、陀螺仪报告调用频率、HID 报告更新频率三者一致且为固定值**，与 GP2040-CE 的“无固定节拍、随负载变化”形成对比。

### 5.2 陀螺仪→鼠标：无显式 dt，依赖固定周期

- **位置**：`alpakka_firmware/src/gyro.c` 的 `Gyro__report_incremental()`。
- **公式**：`x = imu_gyro.x * CFG_GYRO_SENSITIVITY_X * sensitivity_multiplier`（y/z 同理），再经 hssnf 非线性、亚像素余数（sub_x/sub_y/sub_z）取整后送 `hid_mouse_move()`。
- **与 GP2040-CE 的异同**：同样**没有在公式中乘 delta time**。但 Alpakka 通过**固定 tick 间隔**，使“每帧”对应的真实时间恒定（1/250 s 或 1/1000 s），因此**灵敏度常数是按该固定周期标定的**，等价于把 dt 吸收进常数里；主循环一旦稳定在设定频率，灵敏度与帧率解耦。若某帧超时（`unused < 0`），该帧会略长，存在少量帧间抖动，但整体仍以固定周期为主。

### 5.3 陀螺仪数据：每 tick 内多采样平均（burst）

- **位置**：`alpakka_firmware/src/imu.c` 的 `imu_read_gyro()`。
- **实现**：调用 `imu_read_gyro_burst(cs, n)` 对两个 IMU 分别做多次 SPI 读取并取平均：IMU0 读 `CFG_IMU_TICK_SAMPLES/8*1` 次，IMU1 读 `CFG_IMU_TICK_SAMPLES/8*7` 次（`CFG_IMU_TICK_SAMPLES = 128`，即每 tick 共 128 次陀螺仪读取），再按权重混合两个 IMU 的结果。
- **效果**：每个 tick 内用**多采样平均**得到该段时间内的角速度估计，相当于在固定 tick 周期上做了“窗口内平均”，既降噪，又在时间上与 tick 对齐；与 GP2040-CE 的“每帧单次 SPI 读”相比，对 ODR 的利用更充分（在同一 tick 内多次采样），且不依赖 FIFO。

### 5.4 回报率与 tick 一致

- **调用链**：`loop_run()` → `loop_controller_task()` → `profile_report_active()` → `profile->report()`（内部调用 gyro 的 report）→ 更新 HID 状态；随后同一 task 内 `hid_report_wired()` 调用 `tud_hid_report()` 等发送报告。
- **结论**：**每 tick 一次 profile report、一次陀螺仪计算、一次 HID 上报**，因此**实际回报率 = 主循环 tick 频率**（250 Hz 或 1000 Hz），与设计一致，不存在“主机 1K 轮询但设备 800 次/秒更新”的错位。

### 5.5 小结：Alpakka 的策略与 GP2040-CE 现状

| 维度 | Alpakka 做法 | GP2040-CE 当前（主循环对齐后） |
|------|----------------|----------------------------------|
| 主循环周期 | 固定 tick（sleep 补足到 CFG_TICK_INTERVAL_IN_US） | **已对齐**：sleep 到 MAIN_LOOP_REPORT_INTERVAL_US，正常 ≈ 1 ms |
| 灵敏度 / dt | 不乘 dt，灵敏度常数按固定 tick 周期标定 | 同样不乘 dt；周期固定后大致正确，超时帧可选显式 dt |
| 一欧元 / 滤波 | 未使用一欧元；仅有 hssnf、亚像素余数 | 使用 One Euro，**Te 仍为 1/1666，需改为实际间隔 1 ms**（见第六节） |
| 陀螺仪采样 | 每 tick 128 次 SPI 读取平均（burst） | 每帧 1 次 SPI；可选增加每帧多采样以更好利用 ODR |
| 回报率 | 回报率 = tick 频率 | 回报率 = 主循环频率，正常时与 1K 一致 |

**核心思路**：Alpakka 通过固定主循环周期 + 灵敏度按周期标定 + 每 tick 多采样，实现一致性与较好利用 ODR。GP2040-CE 已实现**主循环与回报率对齐**；剩余精度修复集中在 **lsm6dsr_imu.cpp**：一欧元 Te、可选 dt、可选多采样（见第六节）。

---

## 六、陀螺仪模拟鼠标修复规划（仅规划，后续依此实现）

本节基于前述分析与 Alpakka 方式，给出在 `src/addons/lsm6dsr_imu.cpp` 中的**修改规划**，不在此文档中改代码；实现时按本节约束进行。

### 6.1 一欧元滤波 Te 与主循环周期一致（必做）

- **问题**：`LSM6DSR_ONE_EURO_TE_S = 1.0f/1666.0f` 按 1666 Hz 设计，实际 preprocess 调用间隔 = 主循环周期 = 1 ms（1000 Hz）。
- **修复**：Te 改为**实际采样间隔**。建议在 lsm6dsr_imu 中定义或引用“主循环报告间隔（秒）”，例如：
  - 若能与 `gp2040.cpp` 共享常量：使用 `MAIN_LOOP_REPORT_INTERVAL_US * 1e-6f` 作为 Te（单位：秒）；
  - 否则在 addon 内定义等效常量（如 `LSM6DSR_ONE_EURO_TE_S = 1e-3f`，对应 1K），并在注释中说明与主循环 1 ms 对齐。
- **效果**：一欧元滤波的 alpha 与真实 1000 Hz 调用一致，滤波截止与延迟符合设计。

### 6.2 鼠标位移公式：固定周期标定 vs 显式 dt（二选一或兼容）

- **现状**：`lr_float = lr_raw * GYRO_MOUSE_BASE_SENS * sensLR`，未乘 dt；主循环已固定 1 ms，等价于“按 1 ms 标定”。
- **方案 A（与 Alpakka 一致）**：保持不乘 dt，明确文档/注释：`GYRO_MOUSE_BASE_SENS` 按“每帧 1 ms”标定，依赖主循环周期不变。超时帧接受该帧位移略偏小。
- **方案 B（更稳健）**：在 `outputGyroToMouse` 中引入**本帧有效 dt**：使用上一帧与本帧时间戳差（或默认 `MAIN_LOOP_REPORT_INTERVAL_US * 1e-6f`），计算 `mouse_delta ∝ gyro_raw * dt * base_sens * sens`。这样超时帧按真实时间积分，灵敏度与帧长解耦；需在 addon 内维护上一帧时间戳并在每帧更新。
- **建议**：优先实现 **6.1** 与 **方案 A**（最小改动）；若需超时帧也一致，再实现方案 B。

### 6.4 每帧多采样（可选，参考 Alpakka burst）与 ODR 约束

- **现状**：每帧 1 次 SPI 读，有效采样率 = 主循环 ≈ 1000 Hz。
- **ODR 约束**：当前 LSM6DSR ODR = 1666 Hz（寄存器约每 0.6 ms 更新一次）。在一帧（1 ms）内连续做多次 SPI 读，**得到的仍是同一寄存器值**，多采样不会得到新数据；只有把 ODR 提高到例如 **6.66 kHz**（约 0.15 ms 更新一次），同帧内才能读到多组不同值，此时做 4～8 次读取取平均才有 Alpakka burst 的降噪/利用 ODR 效果。
- **当前实现**：`LSM6DSR_GYRO_MOUSE_BURST_SAMPLES = 1`（仅单次读取），避免无意义的多次 SPI 耗时。若日后支持并将 ODR 设为 6.66 kHz 或更高，可将该常量改为 4～8 并保留/启用 burst 分支。

### 6.5 实现顺序建议

1. **必做**：6.1 一欧元 Te 改为实际采样间隔（1 ms 或与 MAIN_LOOP_REPORT_INTERVAL_US 一致）。
2. **建议**：6.2 方案 A，明确 BASE_SENS 按 1 ms 标定，不改公式。
3. **可选**：6.2 方案 B（显式 dt）、6.4 每帧多采样，按需求与工时择一或分步实现。

### 6.6 已实现（按 Alpakka 方式）

- **6.1**：`LSM6DSR_ONE_EURO_TE_S` 已改为 `1e-3f`（1 ms），与主循环回报周期一致。
- **6.2 方案 A**：`GYRO_MOUSE_BASE_SENS` 与 `outputGyroToMouse` 已加注释，明确按「每帧 1 ms」标定、不乘 dt。
- **6.4**：在 ODR=1666 Hz 下同帧多读只得同一值，故当前 `LSM6DSR_GYRO_MOUSE_BURST_SAMPLES = 1`（单次读取）；代码中保留 burst 分支，当 ODR 提升至 6.66 kHz 等时可改为 4～8 以启用 Alpakka 式多采样平均。

---

*文档基于 `src/addons/lsm6dsr_imu.cpp`、`src/gp2040.cpp` 主循环对齐实现，以及 `/home/leonxis/alpakka` 固件分析；6.1/6.2 方案 A/6.4 已实现。*
