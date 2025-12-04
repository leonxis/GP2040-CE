# 外圈校准阈值问题分析

## 问题描述

如果摇杆的电位器最大电阻值偏差较多，导致摇杆最大输出电压低于预期，那么使用固定的0.95阈值可能会导致无法采集到样本数据。

## 当前实现分析

### 归一化计算

在 `RangeCalibrationModal.tsx` 中，归一化计算如下：

```typescript
const ADC_MAX = 4095;
const centerXValue = centerX !== undefined ? centerX : ADC_MAX / 2;  // 默认 2047.5
const centerYValue = centerY !== undefined ? centerY : ADC_MAX / 2;  // 默认 2047.5

// 转换为归一化坐标 (-1 到 1)
const stickX = ((data.x - centerXValue) / (ADC_MAX / 2));
const stickY = ((data.y - centerYValue) / (ADC_MAX / 2));

// 计算归一化距离
const distance = Math.sqrt(stickX * stickX + stickY * stickY);

// 阈值判断
if (distance > JOYSTICK_EXTREME_THRESHOLD) {  // 0.95
    // 记录数据
}
```

### 问题场景

假设摇杆的物理最大范围受限：

**场景1：单方向最大范围受限**
- 原始ADC值最大只能到 3500（而不是4095）
- 归一化后：
  - `stickX_max = (3500 - 2047.5) / 2047.5 = 1452.5 / 2047.5 ≈ 0.71`
  - 如果只推X方向，`distance = 0.71 < 0.95` ❌ **无法采集**
  - 如果推到对角线方向，`distance = sqrt(0.71² + 0.71²) ≈ 1.0` ✅ **可以采集**

**场景2：所有方向都受限**
- 原始ADC值最大只能到 3500（而不是4095）
- 归一化后：
  - `stickX_max = stickY_max ≈ 0.71`
  - 对角线方向：`distance ≈ 1.0` ✅ **可以采集**
  - 但单方向：`distance = 0.71 < 0.95` ❌ **无法采集**

**场景3：严重受限**
- 原始ADC值最大只能到 3000（而不是4095）
- 归一化后：
  - `stickX_max = stickY_max = (3000 - 2047.5) / 2047.5 ≈ 0.465`
  - 对角线方向：`distance = sqrt(0.465² + 0.465²) ≈ 0.66 < 0.95` ❌ **无法采集**

## 结论

**用户的理解是正确的！**

如果摇杆的电位器最大电阻值偏差较多，导致：
- 摇杆最大输出电压低于 `0.95 * ADC_MAX`（即 `0.95 * 3.3V = 3.135V`）
- 或者更准确地说，如果归一化后的最大距离小于 0.95

那么确实会出现样本采集不到的问题。

## 解决方案建议

### 方案1：动态阈值（推荐）

在开始采样前，先检测摇杆的实际最大范围，然后根据实际范围设置阈值：

```typescript
// 在开始采样前，先快速检测最大距离
let maxDetectedDistance = 0;
for (let i = 0; i < 100; i++) {
    const data = await fetch(apiEndpoint);
    const distance = calculateDistance(data);
    maxDetectedDistance = Math.max(maxDetectedDistance, distance);
}

// 使用实际最大距离的某个百分比作为阈值（例如85%）
const dynamicThreshold = maxDetectedDistance * 0.85;
```

### 方案2：降低固定阈值

将 `JOYSTICK_EXTREME_THRESHOLD` 从 0.95 降低到 0.85 或 0.80，以兼容更多摇杆。

**缺点**：可能会采集到一些非极值位置的数据。

### 方案3：自适应阈值

在采样过程中动态调整阈值：
- 初始阈值设为较低值（如0.80）
- 随着采样进行，逐步提高阈值
- 最终只保留接近最大值的样本

### 方案4：使用相对阈值

不基于归一化距离的绝对值，而是基于当前采样周期内检测到的最大距离：

```typescript
// 在每个周期内，使用该周期最大距离的百分比
const cycleMaxDistance = Math.max(...cycleDataRef.current);
const relativeThreshold = cycleMaxDistance * 0.90;  // 使用周期最大值的90%
```

## 推荐实现

建议采用**方案1（动态阈值）**，在采样开始前先检测摇杆的实际最大范围，然后使用该范围的85-90%作为阈值。这样既能确保采集到极值数据，又能兼容不同规格的摇杆。

