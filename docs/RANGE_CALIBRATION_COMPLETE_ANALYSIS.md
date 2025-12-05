# 外圈采样校准完整逻辑与代码分析

## 概述

外圈校准通过收集48个角度位置的最大半径数据，使用线性插值算法在实时输入处理中应用角度相关的范围限制，从而修正摇杆的非圆形外圈形状。

---

## 一、数据采样阶段（前端）

### 1.1 文件位置
**文件**: `www/src/Components/RangeCalibrationModal.tsx`

### 1.2 关键常量定义

```typescript
const CIRCULARITY_DATA_SIZE = 48;              // 角度采样点数量（每7.5°一个点）
const REQUIRED_FULL_CYCLES = 4;                // 建议完成的旋转圈数
const JOYSTICK_EXTREME_THRESHOLD = 0.85;       // 最小归一化距离阈值（最大半径1.0的85%）
const CIRCLE_FILL_THRESHOLD = 0.95;            // 周期完成阈值（95%的角度必须有数据）
```

### 1.3 数据结构

```typescript
// 累积的最大值（跨所有周期）
const rangeDataRef = useRef<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0));

// 当前周期的数据
const cycleDataRef = useRef<number[]>(new Array(CIRCULARITY_DATA_SIZE).fill(0));

// 状态跟踪
const nonZeroCountRef = useRef(0);      // 当前周期中超过阈值的数据点数量
const fullCyclesRef = useRef(0);        // 已完成的完整周期数
```

### 1.4 采样流程详解

#### 步骤1: 数据获取与归一化

```typescript
// 1. 从API获取原始ADC值
const apiEndpoint = stickNumber === 1 ? '/api/getJoystickCenter' : '/api/getJoystickCenter2';
const res = await fetch(apiEndpoint);
const data = await res.json();

// 2. 获取中心值（已校准的中心点）
const ADC_MAX = 4095;
const centerXValue = centerX !== undefined ? centerX : ADC_MAX / 2;  // 默认2047.5
const centerYValue = centerY !== undefined ? centerY : ADC_MAX / 2;

// 3. 转换为归一化坐标 (-1 到 1)
const stickX = ((data.x - centerXValue) / (ADC_MAX / 2));
const stickY = ((data.y - centerYValue) / (ADC_MAX / 2));

// 4. 计算归一化距离和角度
const distance = Math.sqrt(stickX * stickX + stickY * stickY);  // 0.0 到 1.0
const angle = Math.atan2(stickY, stickX);                      // -π 到 π
```

**关键点**:
- 归一化距离 `distance` 的范围是 0.0 到 1.0
- 1.0 表示摇杆在该方向的理论最大范围
- 角度范围是 `[-π, π]`，需要转换为 `[0, 2π]` 用于索引计算

#### 步骤2: 阈值判断与数据收集

```typescript
// 只有当摇杆推到归一化距离的85%以上时才记录数据
if (distance > JOYSTICK_EXTREME_THRESHOLD) {  // distance > 0.85
    // 计算角度索引 (0 到 47)
    const angleIndex = Math.round((angle + Math.PI) * CIRCULARITY_DATA_SIZE / (2 * Math.PI)) % CIRCULARITY_DATA_SIZE;
    
    // 更新当前周期数据（保留最大值）
    const oldCycleValue = cycleDataRef.current[angleIndex] || 0;
    if (distance > oldCycleValue) {
        cycleDataRef.current[angleIndex] = distance;
        
        // 同时更新累积最大值（跨所有周期）
        const oldMaxValue = rangeDataRef.current[angleIndex] || 0;
        if (distance > oldMaxValue) {
            rangeDataRef.current[angleIndex] = distance;
        }
    }
}
```

**关键逻辑**:
- **角度索引计算**: `(angle + π) * 48 / (2π) % 48`
  - 将角度从 `[-π, π]` 转换为 `[0, 2π]`
  - 映射到48个索引位置（0-47）
  - 每个索引代表 7.5° 的角度范围
- **最大值累积**: 每个角度位置保留所有周期中的最大值
- **阈值选择**: 0.85 可以兼容电位器偏差较大的摇杆，同时通过多轮采样确保准确性

#### 步骤3: 周期完成判断

```typescript
// 检查当前周期中超过阈值的数据点数量
const currentNonZeroCount = cycleDataRef.current.filter(v => v > JOYSTICK_EXTREME_THRESHOLD).length;
const fillRatio = currentNonZeroCount / CIRCULARITY_DATA_SIZE;

// 如果95%的角度（46/48）都有数据，完成一个周期
if (fillRatio >= CIRCLE_FILL_THRESHOLD) {  // fillRatio >= 0.95
    if (currentNonZeroCount > nonZeroCountRef.current || nonZeroCountRef.current === 0) {
        fullCyclesRef.current++;
        // 重置当前周期数据（但保留累积最大值）
        cycleDataRef.current.fill(0);
        nonZeroCountRef.current = 0;
    }
} else {
    nonZeroCountRef.current = currentNonZeroCount;
}
```

**关键逻辑**:
- **周期完成条件**: 95%的角度（46/48）都有超过阈值的数据
- **数据保留**: 累积最大值 `rangeDataRef` 不会被重置，跨周期保留
- **周期重置**: 只有当前周期数据 `cycleDataRef` 被重置

#### 步骤4: 进度计算

```typescript
// 基于已完成周期和当前周期进度计算总进度
const cycleProgress = (fullCyclesRef.current / REQUIRED_FULL_CYCLES) * 100;
const currentCycleProgress = (currentNonZeroCount / CIRCULARITY_DATA_SIZE) * (100 / REQUIRED_FULL_CYCLES);
setProgress(Math.min(100, cycleProgress + currentCycleProgress));
```

**进度计算**:
- 每个完整周期贡献 25% 的进度（100% / 4）
- 当前周期根据已填充的角度比例计算部分进度
- 总进度 = 已完成周期进度 + 当前周期进度

#### 步骤5: 采样控制流程

```typescript
// 开始采样
const startCalibration = () => {
    setIsCollecting(true);
    startCountdown();  // 15秒倒计时解锁"完成"按钮
    
    // 每100ms检查一次摇杆位置
    progressIntervalRef.current = setInterval(checkDataProgress, 100);
};

// 完成采样
const handleComplete = () => {
    // 检查是否有至少一个完整周期
    const currentNonZeroCount = cycleDataRef.current.filter(v => v > JOYSTICK_EXTREME_THRESHOLD).length;
    const hasMinimumData = fullCyclesRef.current >= 1 || 
        (currentNonZeroCount / CIRCULARITY_DATA_SIZE) >= CIRCLE_FILL_THRESHOLD;
    
    if (!hasMinimumData) {
        // 如果没有至少一个完整周期，视为取消
        onHide();
        return;
    }
    
    // 使用累积的最大值作为最终校准数据
    const finalData = [...rangeDataRef.current];
    onComplete(finalData);
};
```

**关键控制**:
- **采样频率**: 每100ms检查一次（约10Hz）
- **倒计时机制**: 15秒后解锁"完成"按钮，允许提前完成
- **最小数据要求**: 至少完成一个完整周期（95%的角度有数据）
- **最终数据**: 使用所有周期中累积的最大值

---

## 二、数据存储阶段

### 2.1 Protobuf定义

**文件**: `proto/config.proto`

```protobuf
message AnalogOptions {
    repeated float joystick_range_data_1 = 34 [(nanopb).max_count = 48];
    repeated float joystick_range_data_2 = 35 [(nanopb).max_count = 48];
}
```

**数据结构**:
- 每个摇杆48个float值（0.0 - 1.0）
- 值表示该角度方向的最大归一化半径
- 0.0表示该角度未校准

### 2.2 数据初始化

**文件**: `src/addons/analog.cpp` - `setup()`

```cpp
// 初始化左摇杆（stick 0）的校准数据
for (int i = 0; i < 48; i++) {
    if (i < analogOptions.joystick_range_data_1_count && 
        analogOptions.joystick_range_data_1[i] > 0.0f) {
        adc_pairs[0].range_data[i] = analogOptions.joystick_range_data_1[i];
    } else {
        adc_pairs[0].range_data[i] = 0.0f;  // 未校准
    }
}

// 初始化右摇杆（stick 1）的校准数据
for (int i = 0; i < 48; i++) {
    if (i < analogOptions.joystick_range_data_2_count && 
        analogOptions.joystick_range_data_2[i] > 0.0f) {
        adc_pairs[1].range_data[i] = analogOptions.joystick_range_data_2[i];
    } else {
        adc_pairs[1].range_data[i] = 0.0f;  // 未校准
    }
}
```

**数据结构**:
```cpp
// headers/addons/analog.h
typedef struct {
    // ... 其他字段 ...
    float range_data[48];  // 48个角度的最大半径数据
} adc_instance;
```

---

## 三、实时应用阶段（后端）

### 3.1 插值算法

**函数**: `AnalogInput::getInterpolatedMaxRadius(int stick_num, float angle)`
**位置**: `src/addons/analog.cpp`

```cpp
float AnalogInput::getInterpolatedMaxRadius(int stick_num, float angle) {
    const float* range_data = adc_pairs[stick_num].range_data;
    const int CIRCULARITY_DATA_SIZE = 48;
    
    // 1. 检查是否有校准数据
    bool hasData = false;
    for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
        if (range_data[i] > 0.0f) {
            hasData = true;
            break;
        }
    }
    if (!hasData) {
        return 0.0f;  // 无校准数据，使用默认行为
    }
    
    // 2. 将角度从 [-π, π] 转换为 [0, 2π] 然后映射到 [0, CIRCULARITY_DATA_SIZE]
    float normalizedAngle = (angle + M_PI) / (2.0f * M_PI);  // 0.0 to 1.0
    float index = normalizedAngle * CIRCULARITY_DATA_SIZE;
    
    // 3. 获取用于插值的两个相邻索引
    int i0 = ((int)std::floor(index)) % CIRCULARITY_DATA_SIZE;
    int i1 = (i0 + 1) % CIRCULARITY_DATA_SIZE;
    float t = index - std::floor(index);  // 小数部分 (0.0 to 1.0)
    
    // 4. 线性插值
    float r0 = range_data[i0] > 0.0f ? range_data[i0] : 0.0f;
    float r1 = range_data[i1] > 0.0f ? range_data[i1] : 0.0f;
    
    return r0 * (1.0f - t) + r1 * t;
}
```

**算法说明**:
1. **角度转换**: 将输入角度从 `[-π, π]` 转换为 `[0, 2π]`，然后映射到 `[0, 48]` 的浮点索引
2. **相邻点查找**: 找到该浮点索引对应的两个整数索引 `i0` 和 `i1`（相邻的两个数据点）
3. **线性插值**: 使用公式 `r = r0 * (1-t) + r1 * t` 计算精确的最大半径
   - `t` 是小数部分，表示在 `i0` 和 `i1` 之间的位置
   - `r0` 和 `r1` 是两个相邻数据点的值

**示例**:
- 如果角度对应索引 12.3，则使用 `range_data[12]` 和 `range_data[13]` 进行插值
- `t = 0.3`，所以 `r = range_data[12] * 0.7 + range_data[13] * 0.3`

### 3.2 径向死区处理

**函数**: `AnalogInput::radialDeadzone(int stick_num, adc_instance & adc_inst)`
**位置**: `src/addons/analog.cpp`

```cpp
void AnalogInput::radialDeadzone(int stick_num, adc_instance & adc_inst) {
    // 1. 计算当前摇杆到中心的距离
    float current_distance = std::sqrt((adc_inst.x_magnitude * adc_inst.x_magnitude) + 
                                       (adc_inst.y_magnitude * adc_inst.y_magnitude));
    
    // 2. 检查是否有外圈校准数据，并获取角度相关的最大半径
    float max_radius = getInterpolatedMaxRadius(stick_num, 
                                                 std::atan2(adc_inst.y_magnitude, 
                                                            adc_inst.x_magnitude));
    bool has_range_calibration = (max_radius > 0.0f);
    
    if (has_range_calibration) {
        // 3. 如果有外圈校准：应用角度相关的限制
        if (current_distance > max_radius) {
            // 按比例缩小以适应校准范围
            float scale = max_radius / current_distance;
            adc_inst.x_magnitude *= scale;
            adc_inst.y_magnitude *= scale;
            current_distance = max_radius;
        }
        // 4. 使用校准后的距离直接转换（不再使用error_rate或forced_circularity）
        adc_inst.x_value = adc_inst.x_magnitude + ANALOG_CENTER;
        adc_inst.y_value = adc_inst.y_magnitude + ANALOG_CENTER;
    } else {
        // 5. 如果没有外圈校准：直接使用原始数据，不进行缩放
        adc_inst.x_value = adc_inst.x_magnitude + ANALOG_CENTER;
        adc_inst.y_value = adc_inst.y_magnitude + ANALOG_CENTER;
    }
    
    // 6. 边界限制
    adc_inst.x_value = std::clamp(adc_inst.x_value, ANALOG_MINIMUM, ANALOG_MAX);
    adc_inst.y_value = std::clamp(adc_inst.y_value, ANALOG_MINIMUM, ANALOG_MAX);
    
    // 7. 应用反死区（anti_deadzone）逻辑（如果启用）
    if (adc_pairs[stick_num].anti_deadzone > 0.0f) {
        float x_component = adc_inst.x_value - ANALOG_CENTER;
        float y_component = adc_inst.y_value - ANALOG_CENTER;
        float length = std::sqrt((x_component * x_component) + (y_component * y_component));
        if (length > 0.0f) {
            float normalized = std::min(length / ANALOG_CENTER, 1.0f);
            float baseline = std::clamp(adc_pairs[stick_num].anti_deadzone, 0.0f, 1.0f);
            if (normalized < baseline) {
                float targetLength = baseline * ANALOG_CENTER;
                float scale = targetLength / length;
                x_component *= scale;
                y_component *= scale;
                adc_inst.x_value = std::clamp(x_component + ANALOG_CENTER, ANALOG_MINIMUM, ANALOG_MAX);
                adc_inst.y_value = std::clamp(y_component + ANALOG_CENTER, ANALOG_MINIMUM, ANALOG_MAX);
            }
        }
    }
}
```

**关键逻辑**:
1. **距离计算**: 计算当前摇杆位置到中心的归一化距离
2. **角度相关限制**: 调用 `getInterpolatedMaxRadius` 获取当前角度方向的最大允许半径
3. **范围限制**: 如果当前距离超过最大半径，按比例缩小 `x_magnitude` 和 `y_magnitude`
4. **直接转换**: 将校准后的幅度直接转换为输出值（不再使用 `error_rate` 或 `forced_circularity`）
5. **无校准情况**: 如果没有校准数据，直接使用原始数据，不进行任何缩放

---

## 四、数据流图

```
┌─────────────────────────────────────────────────────────────────┐
│                    数据采样阶段（前端）                          │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
        ┌─────────────────────────────────────┐
        │  1. 获取原始ADC值 (0-4095)          │
        │     /api/getJoystickCenter           │
        └─────────────────────────────────────┘
                          │
                          ▼
        ┌─────────────────────────────────────┐
        │  2. 归一化到 (-1, 1) 坐标           │
        │     stickX = (x - centerX) / 2047.5 │
        │     stickY = (y - centerY) / 2047.5 │
        └─────────────────────────────────────┘
                          │
                          ▼
        ┌─────────────────────────────────────┐
        │  3. 计算距离和角度                   │
        │     distance = sqrt(stickX² + stickY²)│
        │     angle = atan2(stickY, stickX)    │
        └─────────────────────────────────────┘
                          │
                          ▼
        ┌─────────────────────────────────────┐
        │  4. 阈值判断 (distance > 0.85)      │
        └─────────────────────────────────────┘
                          │
                    ┌─────┴─────┐
                    │           │
               Yes  │           │  No
                    │           │
                    ▼           ▼
        ┌──────────────────┐   ┌──────────────┐
        │  5. 计算角度索引  │   │  跳过此样本  │
        │  index = ...     │   └──────────────┘
        └──────────────────┘
                    │
                    ▼
        ┌─────────────────────────────────────┐
        │  6. 更新数据（保留最大值）          │
        │     cycleData[index] = max(...)     │
        │     rangeData[index] = max(...)     │
        └─────────────────────────────────────┘
                    │
                    ▼
        ┌─────────────────────────────────────┐
        │  7. 检查周期完成 (95%角度有数据)    │
        │     如果完成：fullCycles++           │
        │     重置 cycleData（保留rangeData） │
        └─────────────────────────────────────┘
                    │
                    ▼
        ┌─────────────────────────────────────┐
        │  8. 完成采样（至少1个完整周期）      │
        │     返回 rangeData（48个值）        │
        └─────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────────────────────────┐
│                    数据存储阶段                                 │
└─────────────────────────────────────────────────────────────────┘
                    │
                    ▼
        ┌─────────────────────────────────────┐
        │  保存到 Protobuf                     │
        │  joystick_range_data_1[48]          │
        │  joystick_range_data_2[48]          │
        └─────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────────────────────────┐
│                    实时应用阶段（后端）                          │
└─────────────────────────────────────────────────────────────────┘
                    │
                    ▼
        ┌─────────────────────────────────────┐
        │  1. 读取ADC值并归一化               │
        │     readPin() → x_value, y_value    │
        └─────────────────────────────────────┘
                    │
                    ▼
        ┌─────────────────────────────────────┐
        │  2. 计算幅度                        │
        │     x_magnitude = x_value - 0.5     │
        │     y_magnitude = y_value - 0.5     │
        └─────────────────────────────────────┘
                    │
                    ▼
        ┌─────────────────────────────────────┐
        │  3. 计算当前距离和角度               │
        │     distance = sqrt(x² + y²)        │
        │     angle = atan2(y, x)             │
        └─────────────────────────────────────┘
                    │
                    ▼
        ┌─────────────────────────────────────┐
        │  4. 插值获取最大半径                │
        │     max_radius = getInterpolated... │
        └─────────────────────────────────────┘
                    │
                    ▼
        ┌─────────────────────────────────────┐
        │  5. 应用范围限制                     │
        │     if (distance > max_radius)      │
        │         scale = max_radius / distance│
        │         x_magnitude *= scale         │
        │         y_magnitude *= scale         │
        └─────────────────────────────────────┘
                    │
                    ▼
        ┌─────────────────────────────────────┐
        │  6. 转换为最终输出值                │
        │     x_value = x_magnitude + 0.5     │
        │     y_value = y_magnitude + 0.5     │
        └─────────────────────────────────────┘
                    │
                    ▼
        ┌─────────────────────────────────────┐
        │  7. 输出到游戏手柄状态               │
        │     gamepad->state.lx/ly/rx/ry      │
        └─────────────────────────────────────┘
```

---

## 五、关键参数总结

### 5.1 采样阶段参数

| 参数 | 值 | 说明 |
|------|-----|------|
| `CIRCULARITY_DATA_SIZE` | 48 | 角度采样点数量（每7.5°一个点） |
| `REQUIRED_FULL_CYCLES` | 4 | 建议完成的旋转圈数 |
| `JOYSTICK_EXTREME_THRESHOLD` | 0.85 | 最小归一化距离阈值（最大半径1.0的85%），兼容电位器偏差较大的摇杆 |
| `CIRCLE_FILL_THRESHOLD` | 0.95 | 周期完成阈值（95%），46/48个角度有数据即完成一圈 |
| 采样频率 | 100ms | 每100毫秒检查一次摇杆位置（约10Hz） |
| 倒计时时间 | 15秒 | 解锁"完成"按钮的等待时间 |

### 5.2 实时应用阶段参数

| 参数 | 值 | 说明 |
|------|-----|------|
| `range_data[48]` | float数组 | 存储48个角度的最大半径，用于插值 |
| `in_deadzone` | 0-100% | 摇杆内死区，小于此值视为中心 |
| `anti_deadzone` | 0-100% | 反死区，用于在死区外加速响应 |
| 插值方法 | 线性插值 | 使用相邻两个数据点进行线性插值 |

---

## 六、设计要点

### 6.1 为什么使用48个数据点？

- **精度**: 48个点意味着每7.5°一个采样点，足以描述大多数摇杆的非圆形特性
- **存储**: 48个float值（192字节）的存储开销可接受
- **计算**: 线性插值计算量小，适合实时处理

### 6.2 为什么使用0.85阈值？

- **兼容性**: 可以兼容电位器最大电阻值偏差较大的摇杆
- **准确性**: 通过多轮采样（至少4轮），后3轮样本均来自摇杆最大外圈数据，不会因阈值低而得到错误样本
- **平衡**: 在兼容性和数据质量之间取得平衡

### 6.3 为什么使用最大值累积？

- **稳定性**: 跨多个周期保留最大值，确保捕获摇杆的真实最大范围
- **容错性**: 即使某次采样不准确，也不会影响最终结果
- **渐进性**: 随着采样进行，数据逐渐接近真实值

### 6.4 为什么使用线性插值？

- **简单性**: 线性插值计算简单，适合实时处理
- **连续性**: 保证输出值的连续性，避免突变
- **精度**: 对于48个数据点，线性插值精度足够

---

## 七、代码位置总结

| 功能 | 文件 | 函数/组件 |
|------|------|-----------|
| 前端采样UI | `www/src/Components/RangeCalibrationModal.tsx` | `RangeCalibrationModal` |
| 数据初始化 | `src/addons/analog.cpp` | `setup()` |
| 插值算法 | `src/addons/analog.cpp` | `getInterpolatedMaxRadius()` |
| 实时应用 | `src/addons/analog.cpp` | `radialDeadzone()` |
| 数据结构 | `headers/addons/analog.h` | `adc_instance` |
| 配置定义 | `proto/config.proto` | `AnalogOptions` |

---

## 八、使用示例

### 8.1 采样流程

1. 用户点击"校准摇杆外圈"按钮
2. 显示欢迎对话框，提示用户旋转摇杆
3. 用户点击"开始"按钮
4. 系统开始每100ms采样一次
5. 用户将摇杆推到所有方向的最大范围，旋转多圈
6. 15秒后"完成"按钮解锁
7. 用户点击"完成"按钮（至少完成1个完整周期）
8. 系统保存48个角度的最大半径数据

### 8.2 实时应用

1. 系统读取ADC值并归一化
2. 计算当前摇杆位置的距离和角度
3. 使用插值算法获取该角度方向的最大允许半径
4. 如果当前距离超过最大半径，按比例缩小
5. 输出校准后的值到游戏手柄状态

---

## 九、故障排除

### 9.1 采样无法完成

**问题**: 进度条不动或无法完成周期

**可能原因**:
- 摇杆无法推到足够远的距离（低于0.85阈值）
- 摇杆有死角，某些角度无法达到阈值

**解决方案**:
- 检查摇杆硬件是否正常
- 尝试降低阈值（但需要修改代码）
- 确保在校准过程中将摇杆推到所有方向的极限

### 9.2 校准结果不准确

**问题**: 校准后摇杆行为异常

**可能原因**:
- 采样数据不足（未完成至少1个完整周期）
- 摇杆在校准过程中未推到最大范围

**解决方案**:
- 重新校准，确保完成至少4个完整周期
- 在校准过程中将摇杆推到所有方向的极限
- 检查校准数据是否已正确保存

### 9.3 插值结果异常

**问题**: 某些角度方向的行为不正确

**可能原因**:
- 该角度方向没有采样数据（值为0.0）
- 相邻数据点差异过大

**解决方案**:
- 重新校准，确保所有角度都有数据
- 检查采样数据是否完整（48个值都应该 > 0）

---

## 十、性能分析

### 10.1 采样阶段

- **采样频率**: 10Hz（每100ms一次）
- **计算量**: 每次采样约10次浮点运算
- **内存**: 48个float值（192字节）

### 10.2 实时应用阶段

- **插值计算**: 每次调用约20次浮点运算
- **范围限制**: 每次调用约5次浮点运算
- **总计算量**: 对实时性能影响可忽略（< 1% CPU）

---

## 总结

外圈校准系统通过48点采样、线性插值和角度相关范围限制，实现了对摇杆非圆形外圈的精确修正。系统设计兼顾了兼容性、准确性和实时性能，能够有效改善摇杆的使用体验。

