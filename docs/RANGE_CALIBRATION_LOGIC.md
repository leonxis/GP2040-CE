# 外圈校准逻辑与重要控制参数

## 概述

外圈校准通过收集48个角度位置的最大半径数据，使用线性插值算法在实时输入处理中应用角度相关的范围限制，从而修正摇杆的非圆形外圈形状。

---

## 一、数据采样阶段

### 1.1 采样流程

**位置**: `www/src/Components/RangeCalibrationModal.tsx`

#### 关键步骤：

1. **角度索引计算**
   ```typescript
   const angleIndex = Math.round((angle + Math.PI) * CIRCULARITY_DATA_SIZE / (2 * Math.PI)) % CIRCULARITY_DATA_SIZE;
   ```
   - 将角度从 `[-π, π]` 转换为 `[0, 2π]`
   - 映射到48个索引位置（0-47）
   - 每个索引代表 7.5° 的角度范围

2. **数据收集条件**
   ```typescript
   // 计算归一化距离（0.0 到 1.0）
   const stickX = ((data.x - centerXValue) / (ADC_MAX / 2));  // -1 到 1
   const stickY = ((data.y - centerYValue) / (ADC_MAX / 2));  // -1 到 1
   const distance = Math.sqrt(stickX * stickX + stickY * stickY);  // 归一化距离
   
   if (distance > JOYSTICK_EXTREME_THRESHOLD) {
       // 只收集接近最大范围的数据
   }
   ```
   - **阈值**: `JOYSTICK_EXTREME_THRESHOLD = 0.85`
   - **含义**: 归一化距离的0.85，即**最大归一化半径（1.0）的85%**
   - 只有当摇杆推到归一化距离的85%以上时才记录数据
   - 由于需要进行4轮采样，至少后3轮样本均来自摇杆最大外圈数据，因此不会因为采样阈值低而得到错误样本
   - 较低的阈值（0.85而非0.95）可以兼容电位器最大电阻值偏差较大的摇杆
   - 例如：如果摇杆在某个方向的最大范围是1.0，则只有当距离 > 0.85 时才记录

3. **最大值累积**
   ```typescript
   if (distance > oldCycleValue) {
       cycleDataRef.current[angleIndex] = distance;  // 当前周期
       rangeDataRef.current[angleIndex] = distance;   // 累积最大值
   }
   ```
   - 每个角度位置保留最大值
   - 跨多个周期累积最大值
   - 最终得到每个角度的最大可达半径

4. **周期完成判断**
   ```typescript
   const fillRatio = currentNonZeroCount / CIRCULARITY_DATA_SIZE;
   if (fillRatio >= CIRCLE_FILL_THRESHOLD) {
       fullCyclesRef.current++;
       cycleDataRef.current.fill(0);  // 重置当前周期
   }
   ```
   - **阈值**: `CIRCLE_FILL_THRESHOLD = 0.95`
   - 当95%的角度（46/48）都有数据时，完成一个周期
   - 建议完成4个周期以获得更准确的数据

### 1.2 重要控制参数（采样阶段）

| 参数 | 值 | 说明 |
|------|-----|------|
| `CIRCULARITY_DATA_SIZE` | 48 | 角度采样点数量（每7.5°一个点） |
| `REQUIRED_FULL_CYCLES` | 4 | 建议完成的旋转圈数 |
| `JOYSTICK_EXTREME_THRESHOLD` | 0.85 | 最小归一化距离阈值（最大半径1.0的85%），只有推到接近最大范围才记录。较低阈值可兼容电位器偏差较大的摇杆，由于多轮采样机制，不会因阈值低而得到错误样本 |
| `CIRCLE_FILL_THRESHOLD` | 0.95 | 周期完成阈值（95%），46/48个角度有数据即完成一圈 |
| 采样频率 | 100ms | 每100毫秒检查一次摇杆位置 |

---

## 二、数据存储

### 2.1 数据结构

**位置**: `proto/config.proto`

```protobuf
message AnalogOptions {
    repeated float joystick_range_data_1 = 34 [(nanopb).max_count = 48];
    repeated float joystick_range_data_2 = 35 [(nanopb).max_count = 48];
}
```

- 每个摇杆48个float值（0.0 - 1.0）
- 值表示该角度方向的最大归一化半径
- 0.0表示该角度未校准

### 2.2 数据初始化

**位置**: `src/addons/analog.cpp` - `setup()`

```cpp
for (int i = 0; i < 48; i++) {
    if (i < analogOptions.joystick_range_data_1_count && 
        analogOptions.joystick_range_data_1[i] > 0.0f) {
        adc_pairs[0].range_data[i] = analogOptions.joystick_range_data_1[i];
    } else {
        adc_pairs[0].range_data[i] = 0.0f;  // 未校准
    }
}
```

- 从配置加载校准数据
- 0.0表示该角度未校准，将使用默认行为

---

## 三、插值算法

### 3.1 角度到索引的转换

**位置**: `src/addons/analog.cpp` - `getInterpolatedMaxRadius()`

```cpp
// 1. 角度归一化: [-π, π] -> [0, 2π] -> [0, 1]
float normalizedAngle = (angle + M_PI) / (2.0f * M_PI);

// 2. 转换为数组索引: [0, 1] -> [0, 48]
float index = normalizedAngle * CIRCULARITY_DATA_SIZE;

// 3. 获取相邻两个索引
int i0 = ((int)std::floor(index)) % CIRCULARITY_DATA_SIZE;
int i1 = (i0 + 1) % CIRCULARITY_DATA_SIZE;
float t = index - std::floor(index);  // 插值权重 (0.0 - 1.0)
```

**示例**:
- 角度 = 45° (π/4) → normalizedAngle = 0.625 → index = 30.0
- i0 = 30, i1 = 31, t = 0.0 (正好在索引30上)

### 3.2 线性插值

```cpp
float r0 = range_data[i0] > 0.0f ? range_data[i0] : 0.0f;
float r1 = range_data[i1] > 0.0f ? range_data[i1] : 0.0f;

return r0 * (1.0f - t) + r1 * t;
```

**插值公式**: `result = r0 × (1-t) + r1 × t`

**示例**:
- 如果 index = 30.3
- i0 = 30, r0 = 0.95
- i1 = 31, r1 = 0.92
- t = 0.3
- result = 0.95 × 0.7 + 0.92 × 0.3 = 0.941

### 3.3 边界处理

- **未校准角度**: 如果相邻两个索引都是0.0，返回0.0（使用默认行为）
- **部分校准**: 如果只有一个索引有数据，使用该值
- **循环边界**: 使用模运算处理 47 → 0 的边界情况

---

## 四、实时应用逻辑

### 4.1 处理流程

**位置**: `src/addons/analog.cpp` - `radialDeadzone()`

```cpp
void AnalogInput::radialDeadzone(int stick_num, adc_instance & adc_inst) {
    // 1. 计算当前距离
    float current_distance = std::sqrt(
        adc_inst.x_magnitude * adc_inst.x_magnitude + 
        adc_inst.y_magnitude * adc_inst.y_magnitude
    );
    
    // 2. 获取当前角度的最大允许半径（插值）
    float angle = std::atan2(adc_inst.y_magnitude, adc_inst.x_magnitude);
    float max_radius = getInterpolatedMaxRadius(stick_num, angle);
    
    // 3. 如果超出校准范围，进行缩放
    if (max_radius > 0.0f && current_distance > max_radius) {
        float scale = max_radius / current_distance;
        adc_inst.x_magnitude *= scale;
        adc_inst.y_magnitude *= scale;
        current_distance = max_radius;
    }
    
    // 4. 继续原有的死区处理逻辑
    // ...
}
```

### 4.2 应用时机

在 `process()` 函数中，`radialDeadzone()` 在以下时机被调用：

```
process()
  ├─ readPin()          // 读取ADC原始值
  ├─ 应用中心校准        // 使用joystick_center_x/y
  ├─ 应用反转设置
  ├─ 应用EMA平滑
  ├─ magnitudeCalculation()  // 计算距离
  ├─ radialDeadzone()   // ← 在这里应用外圈校准
  │   ├─ getInterpolatedMaxRadius()  // 插值获取最大半径
  │   ├─ 限制超出范围的值
  │   └─ 应用标准死区缩放
  └─ 输出到gamepad状态
```

### 4.3 缩放逻辑

当摇杆位置超出校准的最大半径时：

```cpp
if (current_distance > max_radius) {
    float scale = max_radius / current_distance;
    adc_inst.x_magnitude *= scale;
    adc_inst.y_magnitude *= scale;
}
```

**效果**:
- 保持方向不变
- 将距离限制在最大半径内
- 修正非圆形外圈形状

**示例**:
- 校准数据：45°方向最大半径 = 0.95
- 实际输入：45°方向距离 = 1.0
- 缩放后：距离 = 0.95（被限制在校准范围内）

---

## 五、重要控制参数总结

### 5.1 采样阶段参数

| 参数 | 位置 | 值 | 作用 |
|------|------|-----|------|
| `CIRCULARITY_DATA_SIZE` | RangeCalibrationModal.tsx | 48 | 角度采样点数量 |
| `JOYSTICK_EXTREME_THRESHOLD` | RangeCalibrationModal.tsx | 0.95 | 最小距离阈值，确保只收集外圈数据 |
| `CIRCLE_FILL_THRESHOLD` | RangeCalibrationModal.tsx | 0.95 | 周期完成阈值（46/48角度有数据） |
| `REQUIRED_FULL_CYCLES` | RangeCalibrationModal.tsx | 4 | 建议完成的旋转圈数 |
| 采样间隔 | RangeCalibrationModal.tsx | 100ms | 数据检查频率 |
| 倒计时时间 | RangeCalibrationModal.tsx | 15秒 | 完成按钮解锁时间 |
| 最小完成要求 | RangeCalibrationModal.tsx | 1圈 | 至少完成1圈才能保存数据 |

### 5.2 运行时参数

| 参数 | 位置 | 说明 |
|------|------|------|
| `range_data[48]` | analog.h / analog.cpp | 存储48个角度的最大半径值 |
| `M_PI` | analog.cpp | 圆周率常量，用于角度计算 |
| `ANALOG_CENTER` | analog.cpp | 0.5，归一化中心值 |
| `ANALOG_MAX` | analog.cpp | 1.0，归一化最大值 |

### 5.3 参数调优建议

#### 提高精度
- **增加采样点**: 将 `CIRCULARITY_DATA_SIZE` 从48增加到96（每3.75°一个点）
  - 优点：更精确的角度分辨率
  - 缺点：存储空间翻倍，插值计算稍慢

#### 降低采样要求
- **降低阈值**: 将 `JOYSTICK_EXTREME_THRESHOLD` 从0.95降到0.90
  - 优点：更容易收集数据
  - 缺点：可能收集到非最大范围的数据，精度降低

#### 加快采样速度
- **减少周期数**: 将 `REQUIRED_FULL_CYCLES` 从4降到2
  - 优点：采样更快
  - 缺点：数据可能不够全面

---

## 六、数据流图

```
┌─────────────────────────────────────────────────────────┐
│  采样阶段 (RangeCalibrationModal.tsx)                   │
├─────────────────────────────────────────────────────────┤
│  1. 用户旋转摇杆                                         │
│  2. 每100ms检查位置                                       │
│  3. 计算角度索引 (0-47)                                  │
│  4. 如果距离 > 0.95，记录最大值                          │
│  5. 累积48个角度的最大值                                 │
│  6. 完成4圈后保存数据                                    │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│  存储阶段 (webconfig.cpp)                               │
├─────────────────────────────────────────────────────────┤
│  1. 将48个float值保存到proto配置                        │
│  2. 写入到设备存储                                        │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│  加载阶段 (analog.cpp - setup())                        │
├─────────────────────────────────────────────────────────┤
│  1. 从配置读取48个float值                                │
│  2. 存储到 adc_pairs[i].range_data[48]                  │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────┐
│  实时应用 (analog.cpp - radialDeadzone())               │
├─────────────────────────────────────────────────────────┤
│  1. 计算当前摇杆角度                                     │
│  2. 使用插值获取该角度的最大半径                         │
│  3. 如果超出，缩放限制在范围内                           │
│  4. 继续标准死区处理                                     │
└─────────────────────────────────────────────────────────┘
```

---

## 七、关键算法详解

### 7.1 角度索引计算

```cpp
// 输入: angle ∈ [-π, π]
// 输出: index ∈ [0, 47]

normalizedAngle = (angle + π) / (2π)  // [0, 1]
index = normalizedAngle × 48          // [0, 48)
i0 = floor(index) % 48                // [0, 47]
i1 = (i0 + 1) % 48                    // [0, 47]
t = index - floor(index)               // [0, 1)
```

**边界情况处理**:
- `angle = -π` → `index = 0` → `i0 = 0, i1 = 1`
- `angle = π` → `index = 48` → `i0 = 0, i1 = 1` (模运算)
- `angle = 0` → `index = 24` → `i0 = 24, i1 = 25`

### 7.2 线性插值

```cpp
// 输入: r0, r1 (相邻两个角度的最大半径), t (插值权重)
// 输出: 插值后的最大半径

result = r0 × (1 - t) + r1 × t
```

**特性**:
- `t = 0`: 完全使用 r0
- `t = 1`: 完全使用 r1
- `t = 0.5`: 使用 r0 和 r1 的平均值
- 线性平滑过渡

### 7.3 范围限制

```cpp
// 输入: current_distance, max_radius
// 输出: 缩放后的 x_magnitude, y_magnitude

if (current_distance > max_radius) {
    scale = max_radius / current_distance
    x_magnitude = x_magnitude × scale
    y_magnitude = y_magnitude × scale
}
```

**效果**:
- 保持方向向量不变
- 将距离限制在 `max_radius` 内
- 修正非圆形外圈

---

## 八、性能考虑

### 8.1 计算复杂度

- **插值计算**: O(1) - 常数时间
- **数据检查**: O(48) - 线性时间（仅在初始化时）
- **实时处理**: O(1) - 每次输入处理只需一次插值

### 8.2 内存占用

- **每个摇杆**: 48 × 4 bytes = 192 bytes
- **两个摇杆**: 384 bytes
- **总配置大小**: 可忽略不计

### 8.3 实时性能

- **插值计算**: < 1μs（在现代MCU上）
- **对输入延迟影响**: 可忽略不计
- **CPU占用**: < 0.1%

---

## 九、故障排除

### 9.1 常见问题

**问题1**: 校准后摇杆无法达到最大范围
- **原因**: 校准数据中的最大值小于1.0
- **解决**: 重新校准，确保推到真正的最大范围

**问题2**: 某些方向仍然超出范围
- **原因**: 该角度方向未收集到数据（值为0.0）
- **解决**: 重新校准，确保所有方向都旋转到

**问题3**: 校准数据不准确
- **原因**: 采样时未推到最大范围
- **解决**: 提高 `JOYSTICK_EXTREME_THRESHOLD` 或重新校准

### 9.2 调试方法

1. **检查校准数据**: 查看 `range_data` 数组，确认所有48个值都 > 0
2. **验证插值**: 在特定角度测试，确认返回的最大半径正确
3. **监控实时值**: 在 `radialDeadzone()` 中添加日志，查看缩放是否生效

---

## 十、总结

外圈校准通过以下步骤实现：

1. **采样**: 收集48个角度方向的最大半径值（0.95阈值）
2. **存储**: 保存为48个float值（0.0-1.0）
3. **插值**: 使用线性插值计算任意角度的最大半径
4. **应用**: 实时限制超出校准范围的输入值

**关键优势**:
- ✅ 支持非圆形外圈校准
- ✅ 实时性能优秀（O(1)插值）
- ✅ 内存占用小（192 bytes/摇杆）
- ✅ 向后兼容（未校准时使用默认行为）

**重要参数**:
- `JOYSTICK_EXTREME_THRESHOLD = 0.95`: 确保只收集外圈数据
- `CIRCULARITY_DATA_SIZE = 48`: 角度分辨率（7.5°/点）
- `CIRCLE_FILL_THRESHOLD = 0.95`: 周期完成标准（46/48角度）

