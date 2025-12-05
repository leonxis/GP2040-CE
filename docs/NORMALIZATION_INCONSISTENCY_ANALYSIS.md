# 归一化不一致问题分析

## 问题描述

用户发现了一个重要的归一化不一致问题：

1. **采样阶段**：使用校准中心点进行归一化，可能导致 `stickX`、`stickY` 的值大于 1
2. **后端应用阶段**：使用 `readPin()` 进行中心校准映射，`current_distance` 最大只能达到约 0.707
3. **结果**：采样时收集的 `distance` 可能大于后端应用时的 `current_distance` 最大值

---

## 一、采样阶段的归一化

### 代码位置
`www/src/Components/RangeCalibrationModal.tsx`

```typescript
// 获取中心值（已校准的中心点或默认理论中心）
const ADC_MAX = 4095;
const centerXValue = centerX !== undefined ? centerX : ADC_MAX / 2;  // 默认2047.5
const centerYValue = centerY !== undefined ? centerY : ADC_MAX / 2;  // 默认2047.5

// 转换为归一化坐标 (-1 到 1)
const stickX = ((data.x - centerXValue) / (ADC_MAX / 2));
const stickY = ((data.y - centerYValue) / (ADC_MAX / 2));

// 计算归一化距离
const distance = Math.sqrt(stickX * stickX + stickY * stickY);
```

### 问题分析

**当校准中心点不在ADC中点（2047.5）时**：

#### 场景1: 中心点偏小（例如 centerX = 1900）

假设摇杆推到最大，`data.x = 4095`：
```typescript
stickX = (4095 - 1900) / (4095 / 2) = 2195 / 2047.5 ≈ 1.072 > 1
```

**结果**: `stickX` 可能大于 1，导致 `distance` 可能大于 1

#### 场景2: 中心点偏大（例如 centerX = 2200）

假设摇杆推到最小，`data.x = 0`：
```typescript
stickX = (0 - 2200) / (4095 / 2) = -2200 / 2047.5 ≈ -1.074 < -1
```

**结果**: `stickX` 可能小于 -1（绝对值大于1），导致 `distance` 可能大于 1

### 结论

**采样阶段的归一化距离 `distance` 可能大于 1.0**，因为：
- 归一化公式假设中心点在ADC中点（2047.5）
- 如果实际中心点偏离中点，归一化后的值会超出 [-1, 1] 范围
- 距离 `distance = sqrt(stickX² + stickY²)` 可能大于 1.0

---

## 二、后端应用阶段的归一化

### 代码位置
`src/addons/analog.cpp`

#### 步骤1: readPin() - 中心校准映射

```cpp
float AnalogInput::readPin(int stick_num, Pin_t pin_adc, uint16_t center) {
    adc_select_input(pin_adc);
    uint16_t adc_value = adc_read();  // 原始ADC值 0-4095
    
    // 应用中心校准映射
    if (center != 0) {
        if (adc_value > center) {
            // 映射到 [center, 4095] → [2047.5, 4095]
            adc_value = map(adc_value, center, ADC_MAX, ADC_MAX / 2, ADC_MAX);
        } else if (adc_value == center) {
            adc_value = ADC_MAX / 2;  // 中心点映射到中点
        } else {
            // 映射到 [0, center] → [0, 2047.5]
            adc_value = map(adc_value, 0, center, 0, ADC_MAX / 2);
        }
    }
    // 归一化到 0.0 到 1.0
    return ((float)adc_value) / ADC_MAX;
}
```

**关键点**:
- `readPin()` 会将原始ADC值映射到以 `ADC_MAX/2`（2047.5）为中心的对称范围
- 无论实际中心点在哪里，映射后的值都在 `[0, 4095]` 范围内
- 最终归一化值 `x_value` 在 `[0.0, 1.0]` 范围内

#### 步骤2: magnitudeCalculation() - 计算幅度

```cpp
float AnalogInput::magnitudeCalculation(int stick_num, adc_instance & adc_inst) {
    adc_inst.x_magnitude = adc_inst.x_value - ANALOG_CENTER;  // ANALOG_CENTER = 0.5
    adc_inst.y_magnitude = adc_inst.y_value - ANALOG_CENTER;
    // ...
}
```

**关键点**:
- `x_value` 范围: `[0.0, 1.0]`
- `x_magnitude = x_value - 0.5`，范围: `[-0.5, 0.5]`
- `y_magnitude` 范围: `[-0.5, 0.5]`

#### 步骤3: radialDeadzone() - 计算距离

```cpp
void AnalogInput::radialDeadzone(int stick_num, adc_instance & adc_inst) {
    // 计算当前距离
    float current_distance = std::sqrt((adc_inst.x_magnitude * adc_inst.x_magnitude) + 
                                       (adc_inst.y_magnitude * adc_inst.y_magnitude));
    // ...
}
```

**关键点**:
- `x_magnitude` 范围: `[-0.5, 0.5]`
- `y_magnitude` 范围: `[-0.5, 0.5]`
- `current_distance` 最大值: `sqrt(0.5² + 0.5²) = sqrt(0.5) ≈ 0.707`

### 结论

**后端应用阶段的 `current_distance` 最大只能达到约 0.707**，因为：
- `readPin()` 的映射确保了 `x_value` 和 `y_value` 都在 `[0.0, 1.0]` 范围内
- `x_magnitude` 和 `y_magnitude` 都在 `[-0.5, 0.5]` 范围内
- 最大距离受限于 `sqrt(0.5² + 0.5²) ≈ 0.707`

---

## 三、问题验证

### 用户描述的正确性

✅ **描述1**: "在采样中得到的归一化距离distance是摇杆所能达到的最远点到校准中心的距离"
- **正确**：采样时收集的是摇杆推到最大范围时的归一化距离

✅ **描述2**: "由于校准中心点不位于ADC_MAX的中点，因此实际上在采样中读取到的data.x,data.y到校准中心点的距离会大于ADC_MAX中点到摇杆理论边界的距离"
- **正确**：如果中心点不在2047.5，归一化后的值可能超出 [-1, 1] 范围

✅ **描述3**: "也就会导致stickX,stickY的值大于1"
- **正确**：当中心点偏离中点时，`stickX` 或 `stickY` 的绝对值可能大于 1

❓ **描述4**: "因此在后端应用中读取得到的current_distance是不可能大于distance的"
- **需要修正**：实际上，由于归一化方式不同：
  - 采样时的 `distance` 可能大于 1.0
  - 后端应用时的 `current_distance` 最大只能达到约 0.707
  - **所以 `current_distance` 确实不可能大于采样时的 `distance`（如果 `distance > 0.707`）**

---

## 四、问题影响

### 1. 数据不一致

- **采样阶段**: 收集的 `distance` 可能大于 1.0（例如 1.072）
- **后端应用**: `current_distance` 最大只能达到约 0.707
- **结果**: 校准数据中的某些值（> 0.707）在后端应用中永远不会被触发

### 2. 校准数据浪费

- 如果采样时收集到 `distance = 1.072`，这个值会被保存
- 但后端应用时，`current_distance` 永远不可能达到 1.072
- 这部分校准数据实际上是无用的

### 3. 校准精度问题

- 如果采样时 `distance` 可能大于 1.0，但后端应用时最大只能达到 0.707
- 那么校准数据的有效范围是 `[0.0, 0.707]`，而不是 `[0.0, 1.0]`
- 这可能导致校准精度下降

---

## 五、解决方案建议

### 方案1: 统一归一化方式（推荐）

**修改采样阶段的归一化方式，使其与后端应用一致**：

```typescript
// 方案1a: 使用与后端相同的映射方式
// 需要在前端模拟 readPin() 的映射逻辑
function normalizeADC(adcValue: number, center: number): number {
    if (center === 0) {
        return adcValue / 4095;
    }
    
    let mappedValue: number;
    if (adcValue > center) {
        mappedValue = map(adcValue, center, 4095, 2047.5, 4095);
    } else if (adcValue === center) {
        mappedValue = 2047.5;
    } else {
        mappedValue = map(adcValue, 0, center, 0, 2047.5);
    }
    return mappedValue / 4095;  // 归一化到 0.0-1.0
}

// 然后计算 magnitude
const x_value = normalizeADC(data.x, centerXValue);
const y_value = normalizeADC(data.y, centerYValue);
const x_magnitude = x_value - 0.5;  // -0.5 到 0.5
const y_magnitude = y_value - 0.5;  // -0.5 到 0.5
const distance = Math.sqrt(x_magnitude * x_magnitude + y_magnitude * y_magnitude);  // 最大约 0.707
```

### 方案2: 限制采样距离

**在采样阶段限制 `distance` 的最大值**：

```typescript
const distance = Math.sqrt(stickX * stickX + stickY * stickY);
const maxDistance = Math.sqrt(0.5 * 0.5 + 0.5 * 0.5);  // ≈ 0.707
const clampedDistance = Math.min(distance, maxDistance);
```

### 方案3: 后端适配

**在后端应用时，将校准数据按比例缩放**：

```cpp
// 如果 max_radius > 0.707，按比例缩放
if (max_radius > 0.707f) {
    max_radius = 0.707f;  // 限制到实际最大值
}
```

---

## 六、推荐方案

**推荐使用方案1（统一归一化方式）**，因为：
1. ✅ 确保采样和应用使用相同的归一化逻辑
2. ✅ 校准数据的范围与实际使用范围一致
3. ✅ 避免数据浪费和精度损失
4. ✅ 逻辑清晰，易于维护

---

## 七、结论

**用户的分析是正确的！**

1. ✅ 采样阶段的归一化距离 `distance` 可能大于 1.0（当中心点不在ADC中点时）
2. ✅ 后端应用阶段的 `current_distance` 最大只能达到约 0.707
3. ✅ 这导致了归一化不一致问题
4. ✅ 需要统一归一化方式以确保校准数据的有效性

