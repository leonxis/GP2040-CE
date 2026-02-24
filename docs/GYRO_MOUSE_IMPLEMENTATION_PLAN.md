# GP2040 陀螺仪模拟鼠标 — 实现方案与 alpakka 对比

## 一、需求与参数对应关系

### 1.1 规划要点

| 规划项 | 实现要点 | 与 alpakka 对应 |
|--------|----------|-----------------|
| 鼠标操作方式 | 下拉：XY 轴模拟 / XZ 轴模拟 | 对应 alpakka 的「哪一轴→水平、哪一轴→垂直」 |
| 轴向反转 | 下拉：无 / 反转左右 / 反转上下 / 全部反转 | 对应鼠标轴符号（MOUSE_X_NEG / MOUSE_Y_NEG） |
| 灵敏度 | 拆为「左右灵敏度」「上下灵敏度」两个滑块 | alpakka 为单一倍率，此处拆成两路 |
| 死区 | 不做（与 alpakka 一致） | alpakka 陀螺仪路径无死区 |
| 亚像素累积 | 仅 XY 两路浮点余数累积，避免低速丢步 | 与 alpakka 算法一致，见 1.3 节说明 |
| 低区平滑 | 已实现：hssnf(t=1.0, k=0.5)，仅对 \|v\|<t 应用 | 对应 alpakka hssnf |

### 1.2 坐标与操作方式定义

- **calG[0]**：俯仰（Pitch，前后点头）→ 逻辑上适合「上下」
- **calG[1]**：偏航（Yaw，左右转头）→ 逻辑上适合「左右」
- **calG[2]**：横滚（Roll，左右倾）→ 逻辑上也可做「左右」

| 鼠标操作方式 | 水平（左右）来源 | 垂直（上下）来源 | 说明 |
|--------------|------------------|------------------|------|
| **XY 轴模拟** | calG[1] 偏航 | calG[0] 俯仰 | 俯仰上下、偏航左右（与 alpakka 默认一致） |
| **XZ 轴模拟** | calG[2] 横滚 | calG[0] 俯仰 | 俯仰上下、横滚左右 |

轴向反转：在算出 `mouse_dx` / `mouse_dy` 后，按选项对 `dx` 或 `dy` 取反（或两者都取反）。

### 1.3 亚像素累积：为何 alpakka 用 sub_x/sub_y/sub_z，GP2040 仅 sub_x/sub_y？

- **alpakka**：陀螺仪有三轴（X/Y/Z），每轴可**独立映射**到不同输出（例如 X→鼠标左右、Y→鼠标上下、Z→滚轮或摇杆轴）。因此需要对**三个输出维度**分别做亚像素累积，即 `sub_x`、`sub_y`、`sub_z`，每帧对三个浮点值分别 `modf` 取整并保留余数。
- **GP2040 本阶段**：只输出**二维鼠标位移**（水平 dx、垂直 dy），没有把 Z 轴接到鼠标滚轮或第三路输出。因此只有**两个输出维度**，只需两个余数变量 `sub_x`、`sub_y`（可理解为 sub_lr、sub_ud），用同样的「加余数→取整→存新余数」即可。

**结论**：算法相同（每个输出维度一路 modf/取整余数），区别仅在于**输出维度数量**——alpakka 三路输出故三路余数，GP2040 仅鼠标 XY 故两路余数。若后续增加「Z→滚轮」等，再为第三路增加 `sub_z` 即可。

---

## 二、配置与数据结构

### 2.1 Proto 新增字段（LSM6DSROptions）

```protobuf
// 陀螺仪模拟鼠标（outputMode=3 时生效）
optional int32  gyroMouseMapMode = 20 [default = 0];   // 0=XY轴模拟, 1=XZ轴模拟
optional int32  gyroMouseInvert  = 21 [default = 0];   // 0=无, 1=反转左右, 2=反转上下, 3=全部反转
optional float  gyroMouseSensLR  = 22 [default = 1.0]; // 左右灵敏度，参考 alpakka 1.0~2.0，范围 0.5~3.0
optional float  gyroMouseSensUD  = 23 [default = 1.0]; // 上下灵敏度
```

- **gyroMouseMapMode**：0 = XY（偏航左右、俯仰上下），1 = XZ（横滚左右、俯仰上下）。
- **gyroMouseInvert**：0=无，1=仅反转 dx，2=仅反转 dy，3=dx、dy 都反转。
- **gyroMouseSensLR / gyroMouseSensUD**：与 alpakka 的 `sens_mouse_values` 同语义，alpakka 预设约 1.0/1.5/2.0，协议按 ×10 传整数，步进 0.1；建议范围 0.5~3.0，步进 0.1。
- **死区**：不做（与 alpakka 一致），不新增 deadzone 字段。

### 2.2 前端（HML 体感 / 陀螺仪模拟设置）

- **鼠标操作方式**：下拉，选项「XY 轴模拟（俯仰上下，偏航左右）」「XZ 轴模拟（俯仰上下，横滚左右）」→ 对应 `gyroMouseMapMode` 0/1。
- **轴向反转**：下拉，选项「无反转」「反转左右」「反转上下」「全部反转」→ 对应 `gyroMouseInvert` 0/1/2/3。
- **左右灵敏度**：滑块，绑定 `gyroMouseSensLR`，范围 0.5~3.0，步进 0.1（或前端 5~30 整型 /10 显示）。
- **上下灵敏度**：滑块，绑定 `gyroMouseSensUD`，同上。

原「鼠标灵敏度」「鼠标阈值」两个未持久化的滑块替换为以上四项（两下拉 + 两灵敏度）。**不做死区**（与 alpakka 一致）。

### 2.3 后端 LSM6DSR addon 读取与默认值

- 在 `setup()` / `reinit()` 中从 `LSM6DSROptions` 读取上述 4 个字段，无则用默认（mapMode=0, invert=0, sensLR=1.0f, sensUD=1.0f）。
- **低区平滑**：已实现，与 alpakka `Gyro__report_incremental` 一致，对缩放后的水平/垂直浮点值在 \|v\|<1.0 时应用 `hssnf(t=1.0, k=0.5)`，再进入亚像素累积。

---

## 三、固件实现要点

### 3.1 基础灵敏度与单位（对齐 alpakka 思路）

- alpakka：`value = imu_gyro.x * CFG_GYRO_SENSITIVITY_X * sensitivity_multiplier`，`CFG_GYRO_SENSITIVITY = pow(2,-9)*1.45`，角速度单位为 LSB。
- GP2040：LSM6DSR @ 500dps → 17.5 mdps/LSB。可定义基础系数 `BASE_SENS`（与 alpakka 量级接近），每帧：
  - 水平方向：`raw_lr = (mapMode==0 ? calG[1] : calG[2])`，`float_lr = (float)raw_lr * BASE_SENS * gyroMouseSensLR`。
  - 垂直方向：`raw_ud = calG[0]`，`float_ud = (float)raw_ud * BASE_SENS * gyroMouseSensUD`。
- **死区**：不做（与 alpakka 一致），不读取、不应用任何 deadzone。

### 3.2 亚像素累积（与 alpakka 一致）

- 在 addon 内维护静态 `float sub_x, sub_y`（仅鼠标 XY 两轴）。
- 每帧：
  - `float_lr += sub_x`；`float_ud += sub_y`。
  - 对 `float_lr` 用 `modf`（或手动取整+余数）得到整数位移 `dx` 与新的 `sub_x`；`float_ud` 同理得到 `dy`、`sub_y`。
- 再根据 **轴向反转** 对 `dx`/`dy` 取反，最后写入 `gamepad->auxState.sensors.mouse.x = (int16_t)dx`，`mouse.y = (int16_t)dy`，并设 `mouse.enabled = true`，`mouse.active = true`。

### 3.3 outputGyroToMouse 流程（伪代码）

```
1) 根据 gyroMouseMapMode 取水平/垂直原始值：
   lr_raw = (mapMode==0) ? calG[1] : calG[2]
   ud_raw = calG[0]
2) 灵敏度与基础系数（浮点），无死区：
   lr_float = (float)lr_raw * BASE_SENS * gyroMouseSensLR
   ud_float = (float)ud_raw * BASE_SENS * gyroMouseSensUD
3) 低区平滑（hssnf）：对 |lr_float|<1.0、|ud_float|<1.0 应用 hssnf(t=1.0, k=0.5)，公式 x(1-k)/(1 - x*k/t)
4) 亚像素累积（仅 XY 两路，见 1.3 节）：
   lr_float += sub_x;  ud_float += sub_y
   dx = (int)trunc(lr_float);  dy = (int)trunc(ud_float)
   sub_x = lr_float - dx;  sub_y = ud_float - dy
5) 轴向反转：按 gyroMouseInvert 对 dx/dy 取反
6) 饱和到 int16 范围，写入 auxState.sensors.mouse.x / .y，enabled=true, active=true
```

### 3.4 PS4BDriver 与 HID 鼠标报告

- 在 `process()` 中，在写 `mouse_report[0]`（按键）之后：
  - 若 `gamepad->auxState.sensors.mouse.enabled` 且 `outputMode==鼠标` 的语义由 addon 保证，则：
    - `mouse_report[1] = (int8_t)clamp(mouse.x, -127, 127)`
    - `mouse_report[2] = (int8_t)clamp(mouse.y, -127, 127)`
    - `mouse_report[3] = 0`（滚轮暂不接陀螺仪）
- 相对鼠标报告每帧发送本帧的 dx/dy，不跨帧累加（addon 已做亚像素累积，driver 只负责把本帧位移送出去）。

### 3.5 webconfig 与 schema

- `config.proto` 增加上述 4 个字段后，在 `webconfig.cpp` 中对 `LSM6DSROptions` 的读写增加对应项。
- 前端 schema（如 `www/src/Addons/LSM6DSR.tsx` 或 HML 所用表单）增加 `gyroMouseMapMode`、`gyroMouseInvert`、`gyroMouseSensLR`、`gyroMouseSensUD` 的默认值与校验；app.js 默认状态同步更新。

---

## 四、与 alpakka 的差异与空缺

### 4.1 已覆盖的对应关系

| 功能 | alpakka | GP2040 本方案 |
|------|---------|----------------|
| 轴映射 | 每轴可配 actions（MOUSE_X/Y 等） | 用「操作方式」预设：XY 或 XZ，等价固定两轴映射 |
| 轴反转 | 正/负可映射到不同 action（如 MOUSE_X_NEG） | 用「轴向反转」4 档统一处理 |
| 灵敏度 | 单一 sensitivity_multiplier | 拆成左右/上下两个灵敏度 |
| 死区 | 陀螺仪路径无 | 不做（与 alpakka 一致） |
| 亚像素 | sub_x/sub_y/sub_z + modf（三路输出故三路余数） | sub_x/sub_y + 取整余数（仅鼠标 XY 两路输出，见 1.3 节） |
| 输出 | hid_mouse_move(dx,dy) 累加后每周期发送并清零 | auxState.sensors.mouse.x/y 本帧位移，PS4B 每帧发送 |

### 4.2 差异与取舍

- **轴映射**：alpakka 为每轴独立配置 actions（更灵活），GP2040 用两种「操作方式」预设，实现简单、满足常见握持，若以后需要「Z 轴→滚轮」等再扩展。
- **灵敏度**：alpakka 一个倍率，GP2040 左右/上下分离，更利于竖屏或非对称手感。
- **死区**：双方陀螺仪→鼠标路径均不做死区。
- **低区平滑**：alpakka 与 GP2040 均采用 hssnf(t=1.0, k=0.5)，对 |v|<1.0 的缩放后值应用，再进入亚像素。

### 4.3 仍存在的空缺（相对 alpakka）

| 项目 | 说明 |
|------|------|
| **陀螺仪用户偏移** | alpakka 有 offset_gyro_user_x/y/z 微调零位，GP2040 当前无；若后续需要「不重新校准即可微调零位」可再加。 |
| **低区平滑（hssnf）** | 已实现，与 alpakka 一致。 |
| **Z 轴映射** | alpakka 可把 Z 映射到滚轮等；GP2040 当前仅 XY→鼠标移动，Z 未接；若需「横滚→滚轮」可后续扩展 mouse_report[3]。 |
| **每轴独立 action 配置** | alpakka 每轴 4 个 action 槽；GP2040 用「操作方式+反转」简化，不做全量 action 表。 |

---

## 五、实现顺序建议

1. **Proto + webconfig**：增加 4 个字段（mapMode, invert, sensLR, sensUD）并读写。
2. **前端**：陀螺仪模拟设置改为两下拉（操作方式、轴向反转）+ 左右/上下灵敏度滑块，并绑定上述字段与保存（不做死区）。
3. **Addon**：在 `outputGyroToMouse` 中实现：取轴→灵敏度→低区平滑(hssnf)→亚像素累积→反转→写 `auxState.sensors.mouse`。
4. **PS4BDriver**：从 `auxState.sensors.mouse` 读 x/y，写入 `mouse_report[1..2]` 并做 int8 饱和。
5. **联调与范围**：确认灵敏度 0.5~3.0 在实机上的手感，必要时微调 BASE_SENS 或范围步进。

按此方案可实现与 alpakka 陀螺仪模拟鼠标在「轴映射、反转、灵敏度、低区平滑(hssnf)、亚像素」上的对齐；死区与 alpakka 一致不做。

---

## 六、alpakka 陀螺仪与鼠标量程 / 分辨率（无 DPI 设置）

- **鼠标 DPI**：alpakka 固件中**无 DPI 或鼠标分辨率**配置，仅通过灵敏度倍率（sensitivity_multiplier）调节。主机端由操作系统/驱动将 HID 相对位移解释为屏幕像素。
- **陀螺仪→鼠标换算**（alpakka `gyro.c`）：
  - 基础系数：`CFG_GYRO_SENSITIVITY = 2^-9 × 1.45 ≈ 0.00283`（与 LSM6DSR 17.5 mdps/LSB 量级兼容）。
  - 每轴每帧：`value = LSB × CFG_GYRO_SENSITIVITY × sensitivity_multiplier`（sensitivity_multiplier 预设 1.0 / 1.5 / 2.0）。
  - 对 |value| < 1.0 应用 hssnf(1.0, 0.5, value)，再亚像素 modf，整数部分作为本帧 HID 相对位移（counts），无「像素/LSB」或 DPI 常数。
- **结论**：量程与分辨率对应关系为「角速度 LSB × 固定系数 × 用户灵敏度 → 每帧相对位移 counts」；最终光标移动由主机按系统灵敏度/DPI 解释，固件不涉及 DPI。

### 6.1 当前实现下的「等效 DPI」（500 dps 量程）

固件不输出 DPI，但可算出「角速度 → HID counts/s」的等效关系，再与「物理鼠标 DPI = 1 inch/s 移动产生的 counts/s」类比，得到**等效 DPI**。

- **量程**：LSM6DSR @ 500 dps → **17.5 mdps/LSB** → 1 LSB = 0.0175 deg/s。
- **当前公式**（每帧）：`counts = LSB × (1.45/512) × sens` → 即 **1 LSB → 0.00283×sens counts/帧**（hssnf 仅压缩低区，不改变量纲）。
- **主循环**：假设 addon 每帧调用一次，若主循环为 **F Hz**（常见 1000 Hz），则每秒输出 `0.00283 × sens × F` counts  per LSB。
- **1 deg/s 角速度**：1 deg/s = 1/0.0175 ≈ **57.14 LSB** → 每秒 counts = 57.14 × 0.00283 × sens × F ≈ **0.162 × sens × F** counts/s。
- **等效 DPI 定义**：物理鼠标以 1 inch/s 移动时，D DPI 对应 D counts/s。将「1 deg/s 旋转产生的 counts/s」与之等价，得：
  - **等效 DPI ≈ 0.162 × sens × F**（sens 为左右或上下灵敏度 0.5~3.0，F 为主循环 Hz）。
  - 主循环 **1000 Hz**、灵敏度 **1.0** 时：**等效 DPI ≈ 162**。
  - 同条件下 sens=2.0 → ≈324，sens=0.5 → ≈81。

| 灵敏度 sens | 主循环 1000 Hz 时等效 DPI |
|-------------|---------------------------|
| 0.5         | ≈ 81                      |
| 1.0         | ≈ 162                     |
| 1.5         | ≈ 243                     |
| 2.0         | ≈ 324                     |
| 3.0         | ≈ 486                     |

即：**在 500 dps 量程与当前 BASE_SENS 下，灵敏度 1.0、1000 Hz 时，陀螺仪模拟鼠标约等价于 162 DPI 的物理鼠标（以「1 deg/s → counts/s」与「1 inch/s → counts/s」类比）。**
