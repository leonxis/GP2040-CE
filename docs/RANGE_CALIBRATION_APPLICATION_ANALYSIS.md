# 外圈校准数据应用方式分析

## ds4项目中的circularity数据用途

### 1. 主要用途

#### A. 可视化显示
- 在canvas上绘制circularity覆盖范围（扇形图）
- 显示误差率百分比
- 用于诊断和调试

#### B. 微调功能（Finetune Mode）
- 在circularity模式下，用于调整摇杆的8个方向边界值
- 8个方向：LL (Left-Left), LT (Left-Top), LR (Left-Right), LB (Left-Bottom)
- 以及：RL (Right-Left), RT (Right-Top), RR (Right-Right), RB (Right-Bottom)
- circularity数据通过滑块调整，影响这些边界值

#### C. 控制器内部校准
- `calibrateRangeEnd()` 发送命令给控制器（DS4/DS5）
- **控制器自己处理并存储校准数据**
- 网页项目不直接应用这些数据到输入处理

### 2. 关键发现

**ds4项目中的circularity数据并不直接用于校准摇杆输入处理！**

- circularity数据主要用于**可视化和辅助微调**
- 实际的校准是通过**控制器的内部机制**完成的
- DS4/DS5控制器有自己的校准存储和处理逻辑

---

## GP2040-CE中的外圈处理机制

### 当前实现

```cpp
// src/addons/analog.cpp
void AnalogInput::radialDeadzone(int stick_num, adc_instance & adc_inst) {
    // 计算缩放因子
    float scaling_factor = (adc_inst.xy_magnitude - in_deadzone) / (out_deadzone - in_deadzone);
    
    // 如果启用强制圆形，限制缩放因子
    if (forced_circularity == true) {
        scaling_factor = std::fmin(scaling_factor, ANALOG_CENTER);
    }
    
    // 应用缩放
    adc_inst.x_value = ((adc_inst.x_magnitude / adc_inst.xy_magnitude) * scaling_factor) + ANALOG_CENTER;
    adc_inst.y_value = ((adc_inst.y_magnitude / adc_inst.xy_magnitude) * scaling_factor) + ANALOG_CENTER;
}
```

### 关键参数
- `outer_deadzone`: 外圈死区百分比（0-100）
- `inner_deadzone`: 内圈死区百分比（0-100）
- `forced_circularity`: 是否强制圆形

---

## 外圈校准数据应用方案对比

### 方案1：转换为outer_deadzone参数（最简单）

#### 原理
将48个circularity数据转换为单一的`outer_deadzone`百分比值。

#### 转换逻辑
```cpp
// 计算平均半径
float avgRadius = 0;
for (int i = 0; i < 48; i++) {
    avgRadius += circularityData[i];
}
avgRadius /= 48;

// 转换为outer_deadzone百分比
// 如果平均半径是0.95，则outer_deadzone = (1.0 - 0.95) * 100 = 5%
uint32_t outerDeadzone = (uint32_t)((1.0f - avgRadius) * 100.0f);
```

#### 优点
- ✅ 实现最简单
- ✅ 不需要修改proto定义
- ✅ 使用现有配置字段
- ✅ 完全兼容现有系统

#### 缺点
- ❌ 丢失了角度方向的详细信息
- ❌ 只能处理圆形外圈
- ❌ 无法精确还原非圆形外圈

#### 应用方式
```cpp
// 在radialDeadzone()中直接使用
adc_pairs[i].out_deadzone = analogOptions.outer_deadzone / 100.0f;
```

---

### 方案2：保存48个数据点并插值计算（最精确）

#### 原理
保存完整的48个角度位置的距离值，在输入处理时根据当前角度插值计算外圈限制。

#### 数据结构
```protobuf
message AnalogOptions {
    repeated float joystick_range_data_1 = 34;  // 48个float值
    repeated float joystick_range_data_2 = 35;  // 48个float值
}
```

#### 应用逻辑
```cpp
// 在radialDeadzone()中
void AnalogInput::radialDeadzone(int stick_num, adc_instance & adc_inst) {
    // 计算当前角度
    float angle = atan2(adc_inst.y_magnitude, adc_inst.x_magnitude);
    int angleIndex = (int)((angle + M_PI) * 48 / (2 * M_PI)) % 48;
    
    // 获取该角度的最大半径
    float maxRadius = adc_pairs[stick_num].range_data[angleIndex];
    
    // 计算当前距离
    float currentDistance = sqrt(adc_inst.x_magnitude * adc_inst.x_magnitude + 
                                  adc_inst.y_magnitude * adc_inst.y_magnitude);
    
    // 如果超过最大半径，进行缩放
    if (currentDistance > maxRadius) {
        float scale = maxRadius / currentDistance;
        adc_inst.x_magnitude *= scale;
        adc_inst.y_magnitude *= scale;
    }
    
    // 继续原有的缩放逻辑...
}
```

#### 优点
- ✅ 保存完整的circularity数据
- ✅ 可以精确还原摇杆的外圈形状
- ✅ 支持非圆形外圈
- ✅ 精度最高

#### 缺点
- ❌ 需要修改proto定义（384字节）
- ❌ 需要实现插值逻辑
- ❌ 代码复杂度较高

---

### 方案3：保存8个关键方向的距离值（折中方案）

#### 原理
保存8个主要方向（上、右上、右、右下、下、左下、左、左上）的距离值，其他角度通过插值计算。

#### 数据结构
```protobuf
message AnalogOptions {
    // 8个方向：0=上, 1=右上, 2=右, 3=右下, 4=下, 5=左下, 6=左, 7=左上
    optional uint32 joystick_range_up_1 = 34;
    optional uint32 joystick_range_right_up_1 = 35;
    optional uint32 joystick_range_right_1 = 36;
    optional uint32 joystick_range_right_down_1 = 37;
    optional uint32 joystick_range_down_1 = 38;
    optional uint32 joystick_range_left_down_1 = 39;
    optional uint32 joystick_range_left_1 = 40;
    optional uint32 joystick_range_left_up_1 = 41;
    // 摇杆2的8个方向...
}
```

#### 应用逻辑
```cpp
// 在radialDeadzone()中
void AnalogInput::radialDeadzone(int stick_num, adc_instance & adc_inst) {
    // 计算当前角度
    float angle = atan2(adc_inst.y_magnitude, adc_inst.x_magnitude);
    
    // 转换为8个方向的索引（0-7）
    int directionIndex = (int)((angle + M_PI) * 8 / (2 * M_PI)) % 8;
    
    // 获取该方向的最大半径（需要从48个数据中提取对应方向的值）
    float maxRadius = getMaxRadiusForDirection(stick_num, directionIndex);
    
    // 应用限制...
}
```

#### 优点
- ✅ 存储空间适中（64字节）
- ✅ 保留了主要方向的信息
- ✅ 可以处理大部分非圆形情况

#### 缺点
- ❌ 仍然丢失了部分角度信息
- ❌ 需要插值计算中间角度
- ❌ 需要修改proto定义

---

### 方案4：外圈缩放曲线函数（高级方案）

#### 原理
将48个数据点转换为缩放曲线函数，根据当前距离和角度应用不同的缩放因子。

#### 数据结构
```protobuf
message AnalogOptions {
    // 使用多项式系数或其他函数参数
    optional float joystick_range_scale_factor_1 = 34;
    optional float joystick_range_offset_1 = 35;
    // 或者使用分段线性函数的关键点
}
```

#### 应用逻辑
```cpp
// 在radialDeadzone()中
void AnalogInput::radialDeadzone(int stick_num, adc_instance & adc_inst) {
    // 计算当前角度和距离
    float angle = atan2(adc_inst.y_magnitude, adc_inst.x_magnitude);
    float distance = sqrt(adc_inst.x_magnitude * adc_inst.x_magnitude + 
                          adc_inst.y_magnitude * adc_inst.y_magnitude);
    
    // 根据角度和距离计算缩放因子
    float scaleFactor = calculateScaleFactor(stick_num, angle, distance);
    
    // 应用缩放
    adc_inst.x_magnitude *= scaleFactor;
    adc_inst.y_magnitude *= scaleFactor;
}
```

#### 优点
- ✅ 可以精确控制外圈形状
- ✅ 存储空间小（如果使用函数参数）

#### 缺点
- ❌ 实现复杂度最高
- ❌ 需要设计合适的函数模型
- ❌ 可能无法完全还原所有外圈形状

---

## 方案对比表

| 方案 | 存储空间 | 实现复杂度 | 精度 | 兼容性 | 推荐度 |
|------|---------|-----------|------|--------|--------|
| **1: 转换为outer_deadzone** | **0字节** | **低** | **⭐⭐⭐** | **完全兼容** | **⭐⭐⭐⭐⭐** |
| 2: 48个完整数据点+插值 | 384字节 | 高 | ⭐⭐⭐⭐⭐ | 需要修改proto | ⭐⭐⭐ |
| 3: 8个关键方向+插值 | 64字节 | 中 | ⭐⭐⭐⭐ | 需要修改proto | ⭐⭐⭐⭐ |
| 4: 缩放曲线函数 | 可变 | 很高 | ⭐⭐⭐⭐ | 需要修改proto | ⭐⭐ |

---

## ds4项目的实际应用方式

### 关键发现

1. **circularity数据不直接用于输入处理**
   - ds4项目中的circularity数据主要用于可视化和微调
   - 实际的校准是通过DS4/DS5控制器的内部机制完成的

2. **控制器内部处理**
   - `calibrateRangeEnd()` 只是发送命令给控制器
   - 控制器自己处理并存储校准数据
   - 控制器在硬件层面应用校准

3. **微调功能**
   - 在finetune模式中，circularity数据用于调整8个方向的边界值
   - 这些边界值（LL, LT, LR, LB等）用于控制器的内部校准参数

---

## GP2040-CE的适配建议

### 推荐方案：方案1（转换为outer_deadzone）

#### 理由
1. **GP2040-CE没有控制器内部校准机制**
   - 需要在软件层面实现校准
   - 使用现有的`outer_deadzone`参数是最简单的方案

2. **实现简单**
   - 不需要修改proto定义
   - 不需要复杂的插值逻辑
   - 可以快速实现

3. **足够实用**
   - 对于大多数摇杆，圆形外圈已经足够
   - 如果需要更精确的校准，可以考虑后续升级到方案2或3

#### 实现步骤
1. 收集48个circularity数据点
2. 计算平均半径
3. 转换为outer_deadzone百分比
4. 保存到配置中
5. 在`radialDeadzone()`中应用（已有实现）

---

## 如果需要更高精度（未来升级）

### 方案2或3的实现要点

1. **数据收集**：收集48个角度位置的最大距离值
2. **数据保存**：保存到proto配置中
3. **插值计算**：在输入处理时根据当前角度插值
4. **应用限制**：在`radialDeadzone()`中应用角度相关的限制

### 插值算法示例
```cpp
// 线性插值
float interpolateRadius(float angle, float* rangeData) {
    float normalizedAngle = (angle + M_PI) / (2 * M_PI);  // 0-1
    float index = normalizedAngle * 48;
    int i0 = (int)index % 48;
    int i1 = (i0 + 1) % 48;
    float t = index - i0;
    return rangeData[i0] * (1 - t) + rangeData[i1] * t;
}
```

---

## 总结

### ds4项目的实际应用
- circularity数据**不直接用于输入处理**
- 主要用于**可视化和微调**
- 实际校准由**控制器内部机制**完成

### GP2040-CE的适配
- **推荐方案1**：转换为outer_deadzone参数
- 实现简单，兼容性好
- 对于大多数场景已经足够

### 未来升级
- 如果需要更高精度，可以考虑方案2或3
- 需要修改proto定义和实现插值逻辑

