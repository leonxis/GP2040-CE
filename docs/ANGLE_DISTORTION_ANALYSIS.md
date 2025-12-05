# readPin() 映射导致的角度偏转问题分析

## 问题描述

如果中心点不在45度线上（即 `centerX != centerY`），`readPin()` 函数对X轴和Y轴的缩放比例会不同，这可能导致摇杆点的角度发生偏转。

---

## 一、数学分析

### 场景设置

假设：
- **中心点**: `centerX = 1900`, `centerY = 2100`（不在45度线上）
- **摇杆位置**: 原始ADC值 `x = 3000`, `y = 3000`（理论上应该是45度方向）

### X轴映射

**原始范围**: `[1900, 4095]` = 2195 个ADC单位
**映射后范围**: `[2047.5, 4095]` = 2047.5 个ADC单位
**缩放比例**: `2047.5 / 2195 ≈ 0.933`（**压缩了约6.7%**）

**映射计算**:
```cpp
x_mapped = map(3000, 1900, 4095, 2047.5, 4095)
         = (3000 - 1900) * (4095 - 2047.5) / (4095 - 1900) + 2047.5
         = 1100 * 2047.5 / 2195 + 2047.5
         ≈ 1026 + 2047.5
         ≈ 3073.5
```

**归一化**:
```
x_value = 3073.5 / 4095 ≈ 0.751
x_magnitude = 0.751 - 0.5 = 0.251
```

### Y轴映射

**原始范围**: `[2100, 4095]` = 1995 个ADC单位
**映射后范围**: `[2047.5, 4095]` = 2047.5 个ADC单位
**缩放比例**: `2047.5 / 1995 ≈ 1.026`（**扩展了约2.6%**）

**映射计算**:
```cpp
y_mapped = map(3000, 2100, 4095, 2047.5, 4095)
         = (3000 - 2100) * (4095 - 2047.5) / (4095 - 2100) + 2047.5
         = 900 * 2047.5 / 1995 + 2047.5
         ≈ 924 + 2047.5
         ≈ 2971.5
```

**归一化**:
```
y_value = 2971.5 / 4095 ≈ 0.726
y_magnitude = 0.726 - 0.5 = 0.226
```

### 角度计算

**原始角度**（如果使用简单归一化）:
```
angle_original = atan2(3000 - 2100, 3000 - 1900)
               = atan2(900, 1100)
               ≈ 39.3度
```

**映射后角度**:
```
angle_mapped = atan2(y_magnitude, x_magnitude)
             = atan2(0.226, 0.251)
             ≈ 42.0度
```

**理论45度方向的角度**:
```
angle_theoretical = atan2(0.251, 0.251) = 45度
```

**角度偏差**: `42.0度 - 45度 = -3度`（**角度发生了偏转！**）

---

## 二、详细示例

### 示例1: 中心点偏小且不对称

**设置**:
- `centerX = 1900`（偏小）
- `centerY = 2100`（接近中点）
- 摇杆推到45度方向：`x = 3000`, `y = 3000`

**X轴处理**:
```
原始距离: 3000 - 1900 = 1100
映射后: map(3000, 1900, 4095, 2047.5, 4095) ≈ 3073.5
映射后距离: 3073.5 - 2047.5 = 1026
缩放比例: 1026 / 1100 ≈ 0.933
```

**Y轴处理**:
```
原始距离: 3000 - 2100 = 900
映射后: map(3000, 2100, 4095, 2047.5, 4095) ≈ 2971.5
映射后距离: 2971.5 - 2047.5 = 924
缩放比例: 924 / 900 ≈ 1.027
```

**结果**:
- X轴被压缩了约6.7%
- Y轴被扩展了约2.7%
- **缩放比例不同**：0.933 vs 1.027
- **角度发生偏转**：从理论45度变为约42度

### 示例2: 中心点偏大且不对称

**设置**:
- `centerX = 2200`（偏大）
- `centerY = 2000`（偏小）
- 摇杆推到45度方向：`x = 3000`, `y = 3000`

**X轴处理**:
```
原始范围: [2200, 4095] = 1895单位
映射后范围: [2047.5, 4095] = 2047.5单位
缩放比例: 2047.5 / 1895 ≈ 1.080（扩展）
```

**Y轴处理**:
```
原始范围: [2000, 4095] = 2095单位
映射后范围: [2047.5, 4095] = 2047.5单位
缩放比例: 2047.5 / 2095 ≈ 0.977（压缩）
```

**结果**:
- X轴被扩展了约8.0%
- Y轴被压缩了约2.3%
- **缩放比例不同**：1.080 vs 0.977
- **角度发生偏转**

---

## 三、角度偏转的影响

### 1. 方向偏差

如果摇杆推到物理上的45度方向，经过 `readPin()` 映射后，输出的角度可能不是45度，而是42度或48度等。

### 2. 非圆形变形

即使摇杆本身是圆形的，经过不同比例的缩放后，输出的形状会变成椭圆形。

### 3. 与外圈校准的冲突

- **外圈校准的目的**：修正摇杆的非圆形形状
- **readPin() 映射的效果**：可能引入新的角度偏转
- **冲突**：外圈校准试图修正的形状，可能部分是由 `readPin()` 映射造成的

---

## 四、验证代码逻辑

### 当前处理流程

```cpp
// 1. 读取X轴（使用centerX）
x_value = readPin(..., centerX);  // 使用centerX进行映射
x_magnitude = x_value - 0.5;

// 2. 读取Y轴（使用centerY）
y_value = readPin(..., centerY);  // 使用centerY进行映射
y_magnitude = y_value - 0.5;

// 3. 计算角度
angle = atan2(y_magnitude, x_magnitude);  // 角度可能已偏转

// 4. 外圈校准
max_radius = getInterpolatedMaxRadius(angle);  // 使用偏转后的角度
```

### 问题

如果 `centerX != centerY`：
1. X轴和Y轴的缩放比例不同
2. 摇杆点的角度会发生偏转
3. 外圈校准使用的角度是偏转后的角度，而不是原始角度
4. **这可能导致外圈校准数据与实际情况不匹配**

---

## 五、影响分析

### 1. 角度偏转的幅度

**最大偏转角度**取决于中心点的偏差程度：

假设：
- `centerX = 1800`（偏小200）
- `centerY = 2300`（偏大200）
- 摇杆推到45度方向

**X轴缩放比例**: `2047.5 / (4095 - 1800) = 2047.5 / 2295 ≈ 0.892`
**Y轴缩放比例**: `2047.5 / (4095 - 2300) = 2047.5 / 1795 ≈ 1.141`

**角度偏转**: 可能达到 **5-10度**（取决于具体数值）

### 2. 对外圈校准的影响

**问题**:
- 采样时：使用简单的中心点归一化，角度是原始角度
- 应用时：使用 `readPin()` 映射，角度可能已偏转
- **外圈校准数据是基于原始角度收集的，但应用时使用的是偏转后的角度**

**结果**:
- 外圈校准数据可能应用到错误的角度方向
- 校准效果可能不准确

---

## 六、解决方案

### 方案1: 统一使用 readPin() 映射（推荐）

**修改采样阶段，使用与后端相同的映射逻辑**：

```typescript
// 在前端实现与 readPin() 相同的映射
function mapADC(adcValue: number, center: number): number {
    const ADC_MAX = 4095;
    const ADC_HALF = ADC_MAX / 2;
    
    if (center === 0) {
        return adcValue / ADC_MAX;
    }
    
    let mappedValue: number;
    if (adcValue > center) {
        mappedValue = map(adcValue, center, ADC_MAX, ADC_HALF, ADC_MAX);
    } else if (adcValue === center) {
        mappedValue = ADC_HALF;
    } else {
        mappedValue = map(adcValue, 0, center, 0, ADC_HALF);
    }
    
    return mappedValue / ADC_MAX;
}

// 使用映射后的值计算
const x_value = mapADC(data.x, centerXValue);
const y_value = mapADC(data.y, centerYValue);
const x_magnitude = x_value - 0.5;
const y_magnitude = y_value - 0.5;
const distance = Math.sqrt(x_magnitude * x_magnitude + y_magnitude * y_magnitude);
const angle = Math.atan2(y_magnitude, x_magnitude);  // 使用映射后的角度
```

**优点**:
- ✅ 采样和应用使用相同的归一化方式
- ✅ 角度偏转在采样和应用中一致
- ✅ 外圈校准数据基于正确的角度

### 方案2: 移除 readPin() 的映射（不推荐）

**移除 `readPin()` 中的映射，使用简单的中心点归一化**：

```cpp
float AnalogInput::readPin(int stick_num, Pin_t pin_adc, uint16_t center) {
    adc_select_input(pin_adc);
    uint16_t adc_value = adc_read();
    // 简单归一化，不进行映射
    if (center != 0) {
        // 直接使用中心点归一化
        float normalized = (adc_value - center) / (4095.0f / 2.0f);
        return normalized * 0.5f + 0.5f;  // 转换为 0.0-1.0
    }
    return ((float)adc_value) / ADC_MAX;
}
```

**缺点**:
- ❌ 需要修改后端代码，可能影响其他功能
- ❌ 可能破坏现有的中心校准逻辑

### 方案3: 角度补偿（复杂）

**在外圈校准中补偿角度偏转**：

```cpp
// 计算角度偏转量
float angle_offset = calculateAngleOffset(centerX, centerY);

// 在校准时补偿
float corrected_angle = angle - angle_offset;
float max_radius = getInterpolatedMaxRadius(corrected_angle);
```

**缺点**:
- ❌ 实现复杂
- ❌ 需要计算角度偏转量
- ❌ 可能引入新的误差

---

## 七、结论

### ✅ 用户描述完全正确！

1. **`readPin()` 函数确实会针对单个轴进行标准化范围映射**
2. **如果中心点不在45度线上（`centerX != centerY`），X轴和Y轴的缩放比例会不同**
3. **这确实会导致最终输出的摇杆点角度发生偏转**
4. **角度偏转可能达到5-10度**（取决于中心点的偏差程度）
5. **这会影响外圈校准的准确性**，因为采样和应用使用了不同的角度

### 推荐解决方案

**使用方案1：统一使用 `readPin()` 映射**
- 修改采样阶段，使用与后端相同的映射逻辑
- 确保采样和应用使用相同的归一化方式和角度计算
- 这样外圈校准数据就能正确应用

---

## 八、数学证明

### 角度偏转公式

假设：
- `centerX = cx`, `centerY = cy`（`cx != cy`）
- 摇杆位置：`x = px`, `y = py`
- X轴缩放比例：`sx = 2047.5 / (4095 - cx)`
- Y轴缩放比例：`sy = 2047.5 / (4095 - cy)`

**原始角度**:
```
angle_original = atan2(py - cy, px - cx)
```

**映射后角度**:
```
x_mapped = (px - cx) * sx + 2047.5
y_mapped = (py - cy) * sy + 2047.5
x_magnitude = (x_mapped / 4095) - 0.5 = (px - cx) * sx / 4095
y_magnitude = (y_mapped / 4095) - 0.5 = (py - cy) * sy / 4095

angle_mapped = atan2(y_magnitude, x_magnitude)
             = atan2((py - cy) * sy, (px - cx) * sx)
```

**角度偏差**:
```
angle_offset = angle_mapped - angle_original
             = atan2((py - cy) * sy, (px - cx) * sx) - atan2(py - cy, px - cx)
```

**如果 `sx != sy`，角度偏差不为零**，即**角度发生了偏转**。

