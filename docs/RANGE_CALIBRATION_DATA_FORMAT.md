# 外圈校准数据格式说明

## 数据传递流程

### 1. 前端采样阶段

**位置**: `www/src/Components/RangeCalibrationModal.tsx`

```typescript
// 计算归一化距离 (0.0 到 1.0)
const stickX = ((data.x - centerXValue) / (ADC_MAX / 2));  // -1 到 1
const stickY = ((data.y - centerYValue) / (ADC_MAX / 2));  // -1 到 1
const distance = Math.sqrt(stickX * stickX + stickY * stickY);  // 0.0 到 1.0

// 保存归一化距离到数组
rangeDataRef.current[angleIndex] = distance;  // 归一化值，范围 0.0-1.0

// 完成时返回48个值的数组
const finalData = [...rangeDataRef.current];  // 48个归一化距离值
onComplete(finalData);
```

**数据内容**: 48个归一化距离值（float），范围 0.0 到 1.0

### 2. 前端保存阶段

**位置**: `www/src/Addons/JoystickCalibration.tsx`

```typescript
<RangeCalibrationModal
    onComplete={(rangeData) => {
        setFieldValue('joystickRangeData1', rangeData);  // 左摇杆
        // 或
        setFieldValue('joystickRangeData2', rangeData);  // 右摇杆
    }}
/>
```

**字段名**:
- 左摇杆: `joystickRangeData1`
- 右摇杆: `joystickRangeData2`

**数据格式**: JavaScript 数组，包含48个数字（0.0-1.0）

### 3. 后端接收阶段

**位置**: `src/webconfig.cpp` - `setAddonOptions()`

```cpp
// 接收 JSON 数组
if (doc.containsKey("joystickRangeData1") && doc["joystickRangeData1"].is<JsonArray>()) {
    JsonArray rangeData1 = doc["joystickRangeData1"];
    analogOptions.joystick_range_data_1_count = 0;
    for (size_t i = 0; i < rangeData1.size() && i < 48; i++) {
        if (rangeData1[i].is<float>()) {
            // 直接保存为 float 值（归一化值，0.0-1.0）
            analogOptions.joystick_range_data_1[i] = rangeData1[i].as<float>();
            analogOptions.joystick_range_data_1_count++;
        }
    }
}
```

**数据转换**: JSON 数组中的每个数字直接转换为 float，**不进行任何单位转换**

### 4. 数据存储

**位置**: `proto/config.proto`

```protobuf
message AnalogOptions {
    repeated float joystick_range_data_1 = 34 [(nanopb).max_count = 48];
    repeated float joystick_range_data_2 = 35 [(nanopb).max_count = 48];
}
```

**存储格式**: Protobuf `repeated float` 字段，每个值范围 0.0-1.0

### 5. 后端使用阶段

**位置**: `src/addons/analog.cpp` - `getInterpolatedMaxRadius()`

```cpp
float AnalogInput::getInterpolatedMaxRadius(int stick_num, float angle) {
    const float* range_data = adc_pairs[stick_num].range_data;
    // ...
    // 直接使用存储的归一化值进行插值
    float r0 = range_data[i0] > 0.0f ? range_data[i0] : 0.0f;
    float r1 = range_data[i1] > 0.0f ? range_data[i1] : 0.0f;
    return r0 * (1.0f - t) + r1 * t;  // 返回归一化值（0.0-1.0）
}
```

**返回值**: 归一化值（0.0-1.0），表示该角度方向的最大归一化半径

---

## 数据格式总结

### 数据类型
- **类型**: `float` 数组
- **长度**: 48个元素
- **范围**: 0.0 到 1.0（归一化值）
- **单位**: 无单位（归一化距离）

### 数据含义
- **0.0**: 该角度方向未校准或无法达到
- **0.0 < value < 1.0**: 该角度方向的最大归一化半径
- **1.0**: 该角度方向的理论最大范围（但实际可能小于1.0）

### 数据索引
- **索引 0**: 角度 -180°（或 180°）
- **索引 1**: 角度 -172.5°
- **索引 2**: 角度 -165°
- ...
- **索引 47**: 角度 172.5°
- 每个索引代表 7.5° 的角度范围

### JSON 传输格式示例

```json
{
  "joystickRangeData1": [
    0.95, 0.96, 0.94, 0.97, 0.95, 0.96, 0.93, 0.95,
    0.96, 0.94, 0.95, 0.97, 0.95, 0.96, 0.94, 0.95,
    0.96, 0.95, 0.94, 0.96, 0.95, 0.97, 0.94, 0.95,
    0.96, 0.95, 0.94, 0.96, 0.95, 0.96, 0.94, 0.95,
    0.96, 0.95, 0.94, 0.97, 0.95, 0.96, 0.94, 0.95,
    0.96, 0.95, 0.94, 0.96, 0.95, 0.97, 0.94, 0.95
  ],
  "joystickRangeData2": [
    // ... 48个值 ...
  ]
}
```

---

## 关键点

1. **数据是归一化的**: 所有值都在 0.0 到 1.0 范围内，表示归一化距离
2. **不是原始ADC值**: 不是0-4095的ADC原始值
3. **不是物理长度**: 不是以毫米、厘米等物理单位表示的长度
4. **直接使用**: 后端直接使用这些归一化值，不进行任何转换
5. **角度索引**: 48个值对应48个角度方向，每个方向间隔7.5°

---

## 数据流图

```
前端采样
  │
  ├─ 计算归一化距离: distance = sqrt(stickX² + stickY²)  (0.0-1.0)
  │
  ├─ 保存到数组: rangeData[angleIndex] = distance
  │
  └─ 返回48个归一化值: [0.95, 0.96, ...]  (48个float)
       │
       ▼
前端保存 (setFieldValue)
  │
  ├─ 字段名: joystickRangeData1 或 joystickRangeData2
  │
  └─ JSON格式: {"joystickRangeData1": [0.95, 0.96, ...]}
       │
       ▼
HTTP POST /api/setAddonsOptions
  │
  ├─ JSON数组 → C++ float数组
  │
  └─ 保存到: analogOptions.joystick_range_data_1[i]  (float, 0.0-1.0)
       │
       ▼
Protobuf序列化
  │
  ├─ repeated float joystick_range_data_1[48]
  │
  └─ 存储到Flash
       │
       ▼
运行时使用
  │
  ├─ 读取: adc_pairs[stick_num].range_data[48]
  │
  ├─ 插值: getInterpolatedMaxRadius() → 返回归一化值 (0.0-1.0)
  │
  └─ 应用: 限制摇杆输出范围
```

---

## 结论

**传递给手柄控制器的校准数据是：48个归一化距离值（float数组，范围0.0-1.0），表示每个角度方向的最大归一化半径。**

- ✅ 归一化值（0.0-1.0）
- ❌ 不是原始ADC值（0-4095）
- ❌ 不是物理长度（毫米、厘米等）
- ✅ 直接用于实时处理，无需转换

