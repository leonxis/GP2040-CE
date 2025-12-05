# 统一归一化处理方案分析与可行性讨论

## 一、问题现状

### 当前实现的问题

1. **角度畸变问题**：
   - `readPin()` 对X轴和Y轴独立进行映射
   - 当 `centerX != centerY` 时，X轴和Y轴的缩放比例不同
   - 导致摇杆点的角度发生偏转（详见 `ANGLE_DISTORTION_ANALYSIS.md`）

2. **归一化不一致问题**：
   - 采样阶段：使用 `(data.x - centerX) / (ADC_MAX / 2)` 进行归一化
   - 应用阶段：使用 `readPin()` 的映射结果进行归一化
   - 两种归一化方式不一致，导致校准数据无法正确应用（详见 `NORMALIZATION_INCONSISTENCY_ANALYSIS.md`）

3. **数据保真性问题**：
   - 独立轴缩放破坏了摇杆的几何关系
   - 角度信息丢失，影响外圈校准的准确性

---

## 二、用户提出的统一归一化方案

### 核心思想

1. **中心点校准采用平移而非缩放**：
   - 直接使用 `(adc_x - center_x, adc_y - center_y)` 进行坐标平移
   - 不进行独立轴的映射和缩放

2. **统一归一化处理**：
   - 对XY轴使用相同的缩放系数
   - 保持摇杆的几何关系（角度、距离比例）

3. **外圈校准映射到0.5范围**：
   - 将摇杆到中心点的距离缩放到归一化长度0.5范围内
   - 最大距离映射到0.5（即 `ANALOG_CENTER`）

### 方案详细设计

#### 步骤1: 中心点校准（平移）

```cpp
// 读取原始ADC值
uint16_t adc_x = adc_read_x();
uint16_t adc_y = adc_read_y();

// 平移坐标中心点（不缩放）
int16_t offset_x = adc_x - center_x;  // 可能为负
int16_t offset_y = adc_y - center_y;  // 可能为负
```

**特点**：
- ✅ 保持原始几何关系
- ✅ 不改变角度
- ✅ 不改变距离比例

#### 步骤2: 统一归一化（相同缩放系数）

```cpp
// 计算到中心点的距离
float distance = sqrt(offset_x * offset_x + offset_y * offset_y);

// 确定最大可能距离（考虑中心点偏移）
// 最大距离 = max(center_x, ADC_MAX - center_x, center_y, ADC_MAX - center_y)
float max_possible_distance = max(
    max(center_x, ADC_MAX - center_x),
    max(center_y, ADC_MAX - center_y)
);

// 统一归一化（XY轴使用相同的缩放系数）
float normalized_distance = distance / max_possible_distance;
float scale = normalized_distance / distance;  // 统一的缩放系数

float normalized_x = offset_x * scale;
float normalized_y = offset_y * scale;
```

**特点**：
- ✅ XY轴使用相同的缩放系数
- ✅ 保持角度不变
- ✅ 保持距离比例

#### 步骤3: 外圈校准（映射到0.5范围）

```cpp
// 获取当前角度的最大半径（从校准数据）
float max_radius = getInterpolatedMaxRadius(stick_num, atan2(offset_y, offset_x));

if (max_radius > 0.0f) {
    // 将距离缩放到归一化长度0.5范围内
    // max_radius 是采样时收集的最大距离（归一化值，可能>1.0）
    // 需要映射到 0.5（ANALOG_CENTER）
    
    // 方案A: 直接缩放
    float scale_to_0_5 = 0.5f / max_radius;
    normalized_distance *= scale_to_0_5;
    
    // 方案B: 使用采样时的最大可能距离作为基准
    // 假设采样时也使用相同的归一化方式
    float scale_to_0_5 = 0.5f / max_radius;
    normalized_distance *= scale_to_0_5;
    
    // 更新XY坐标
    normalized_x = normalized_x * (normalized_distance / original_distance);
    normalized_y = normalized_y * (normalized_distance / original_distance);
}

// 转换为最终值（0.0-1.0范围）
float x_value = normalized_x + ANALOG_CENTER;  // ANALOG_CENTER = 0.5
float y_value = normalized_y + ANALOG_CENTER;
```

---

## 三、可行性分析

### 3.1 数学可行性

#### ✅ 优点

1. **角度保真性**：
   - 平移操作不改变角度：`atan2(y, x) = atan2(y - center_y, x - center_x)`
   - 统一缩放不改变角度：`atan2(scale * y, scale * x) = atan2(y, x)`
   - **角度完全保真**

2. **距离比例保真性**：
   - 平移操作保持距离比例
   - 统一缩放保持距离比例
   - **距离比例完全保真**

3. **几何关系保真性**：
   - 平移和统一缩放都是线性变换
   - 保持摇杆的几何形状（圆形、椭圆形等）
   - **几何关系完全保真**

#### ⚠️ 需要注意的问题

1. **最大可能距离的计算**：
   ```cpp
   // 需要考虑中心点偏移
   float max_possible_distance = max(
       max(center_x, ADC_MAX - center_x),
       max(center_y, ADC_MAX - center_y)
   );
   ```
   - 这个值可能大于 `ADC_MAX / 2`
   - 需要确保归一化后的值不会超出合理范围

2. **外圈校准数据的兼容性**：
   - 当前采样阶段收集的 `distance` 可能大于 1.0
   - 需要调整采样阶段的归一化方式，使其与新的应用阶段一致

3. **边界处理**：
   - 当摇杆超出理论最大范围时，需要适当的裁剪或缩放

### 3.2 实现可行性

#### ✅ 优点

1. **代码简化**：
   - 不需要对X轴和Y轴分别处理
   - 统一的归一化逻辑
   - 代码更清晰、更易维护

2. **性能提升**：
   - 减少重复计算
   - 统一的缩放系数计算一次即可

3. **校准数据一致性**：
   - 采样阶段和应用阶段使用相同的归一化方式
   - 校准数据可以直接应用，无需转换

#### ⚠️ 需要注意的问题

1. **向后兼容性**：
   - 现有的校准数据可能需要重新校准
   - 或者需要数据迁移逻辑

2. **边界情况处理**：
   - 中心点非常接近边界的情况
   - 摇杆超出理论范围的情况

---

## 四、数据保真性分析

### 4.1 角度保真性

#### 当前实现（独立轴缩放）

```cpp
// X轴独立映射
x_mapped = map(x, center_x, ADC_MAX, ADC_MAX/2, ADC_MAX);
x_normalized = x_mapped / ADC_MAX;

// Y轴独立映射
y_mapped = map(y, center_y, ADC_MAX, ADC_MAX/2, ADC_MAX);
y_normalized = y_mapped / ADC_MAX;

// 角度可能发生偏转
angle = atan2(y_normalized - 0.5, x_normalized - 0.5);
// angle ≠ atan2(y - center_y, x - center_x)  // 角度不一致！
```

#### 统一归一化方案

```cpp
// 平移
offset_x = x - center_x;
offset_y = y - center_y;

// 统一归一化（相同缩放系数）
scale = 1.0f / max_possible_distance;
normalized_x = offset_x * scale;
normalized_y = offset_y * scale;

// 角度完全保真
angle = atan2(normalized_y, normalized_x);
// angle = atan2(offset_y, offset_x) = atan2(y - center_y, x - center_x)  // 角度一致！
```

**结论**：✅ **角度完全保真**

### 4.2 距离比例保真性

#### 当前实现

```cpp
// X轴和Y轴的缩放比例不同
scale_x = (ADC_MAX/2) / (ADC_MAX - center_x);  // 可能 ≠ 1.0
scale_y = (ADC_MAX/2) / (ADC_MAX - center_y);  // 可能 ≠ 1.0

// 距离比例可能改变
distance_ratio = sqrt((scale_x * dx)² + (scale_y * dy)²) / sqrt(dx² + dy²);
// distance_ratio ≠ 1.0  // 距离比例不一致！
```

#### 统一归一化方案

```cpp
// 统一的缩放系数
scale = 1.0f / max_possible_distance;

// 距离比例完全保真
distance_ratio = sqrt((scale * dx)² + (scale * dy)²) / sqrt(dx² + dy²);
// distance_ratio = scale * sqrt(dx² + dy²) / sqrt(dx² + dy²) = scale  // 距离比例一致！
```

**结论**：✅ **距离比例完全保真**

### 4.3 外圈校准数据保真性

#### 当前实现的问题

```cpp
// 采样阶段：distance 可能 > 1.0
distance_sampling = sqrt((x - center_x)² + (y - center_y)²) / (ADC_MAX / 2);
// 可能 > 1.0

// 应用阶段：current_distance 最大约 0.707
current_distance = sqrt(x_magnitude² + y_magnitude²);
// 最大约 0.707（因为 readPin() 的映射限制）

// 结果：校准数据无法正确应用
if (current_distance > max_radius) {  // max_radius 可能 > 0.707
    // 这个条件可能永远不会触发！
}
```

#### 统一归一化方案

```cpp
// 采样阶段：使用统一的归一化方式
distance_sampling = sqrt((x - center_x)² + (y - center_y)²) / max_possible_distance;
// 范围：0.0 到 1.0（或略大于1.0，取决于中心点位置）

// 应用阶段：使用相同的归一化方式
distance_application = sqrt(offset_x² + offset_y²) / max_possible_distance;
// 范围：0.0 到 1.0（或略大于1.0，取决于中心点位置）

// 结果：校准数据可以直接应用
if (distance_application > max_radius) {
    // 可以正确触发，因为归一化方式一致
    scale = max_radius / distance_application;
}
```

**结论**：✅ **外圈校准数据保真性显著提升**

---

## 五、实现方案设计

### 5.1 新的 readPin() 函数设计

```cpp
// 方案：返回相对于中心点的偏移量（不进行归一化）
void AnalogInput::readPinOffset(int stick_num, int16_t &offset_x, int16_t &offset_y) {
    // 读取原始ADC值
    adc_select_input(adc_pairs[stick_num].x_pin_adc);
    uint16_t adc_x = adc_read();
    
    adc_select_input(adc_pairs[stick_num].y_pin_adc);
    uint16_t adc_y = adc_read();
    
    // 平移坐标中心点（不缩放）
    offset_x = (int16_t)adc_x - (int16_t)adc_pairs[stick_num].x_center;
    offset_y = (int16_t)adc_y - (int16_t)adc_pairs[stick_num].y_center;
}
```

### 5.2 统一归一化函数设计

```cpp
void AnalogInput::unifiedNormalize(int stick_num, int16_t offset_x, int16_t offset_y, 
                                   float &normalized_x, float &normalized_y) {
    // 计算最大可能距离
    uint16_t center_x = adc_pairs[stick_num].x_center;
    uint16_t center_y = adc_pairs[stick_num].y_center;
    
    float max_x_range = std::max(center_x, (uint16_t)(ADC_MAX - center_x));
    float max_y_range = std::max(center_y, (uint16_t)(ADC_MAX - center_y));
    float max_possible_distance = std::sqrt(max_x_range * max_x_range + max_y_range * max_y_range);
    
    // 计算当前距离
    float distance = std::sqrt(offset_x * offset_x + offset_y * offset_y);
    
    // 统一归一化（XY轴使用相同的缩放系数）
    float scale = 1.0f / max_possible_distance;
    normalized_x = offset_x * scale;
    normalized_y = offset_y * scale;
    
    // 归一化距离
    float normalized_distance = distance * scale;
    
    // 应用外圈校准
    if (normalized_distance > 0.0f) {
        float angle = std::atan2(offset_y, offset_x);
        float max_radius = getInterpolatedMaxRadius(stick_num, angle);
        
        if (max_radius > 0.0f && normalized_distance > max_radius) {
            // 缩放到校准的最大半径
            float scale_to_max = max_radius / normalized_distance;
            normalized_x *= scale_to_max;
            normalized_y *= scale_to_max;
            normalized_distance = max_radius;
        }
        
        // 映射到0.5范围（如果需要）
        // 假设 max_radius 的最大值是 1.0（或略大于1.0）
        // 需要映射到 0.5
        if (max_radius > 0.0f) {
            float scale_to_0_5 = 0.5f / max_radius;
            normalized_x *= scale_to_0_5;
            normalized_y *= scale_to_0_5;
        }
    }
    
    // 转换为最终值（0.0-1.0范围）
    normalized_x += ANALOG_CENTER;  // ANALOG_CENTER = 0.5
    normalized_y += ANALOG_CENTER;
    
    // 裁剪到有效范围
    normalized_x = std::clamp(normalized_x, ANALOG_MINIMUM, ANALOG_MAX);
    normalized_y = std::clamp(normalized_y, ANALOG_MINIMUM, ANALOG_MAX);
}
```

### 5.3 修改 process() 函数

```cpp
void AnalogInput::process() {
    Gamepad * gamepad = Storage::getInstance().GetGamepad();
    
    uint32_t joystickMid = GAMEPAD_JOYSTICK_MID;
    uint32_t joystickMax = GAMEPAD_JOYSTICK_MAX;
    if ( DriverManager::getInstance().getDriver() != nullptr ) {
        joystickMid = DriverManager::getInstance().getDriver()->GetJoystickMidValue();
        joystickMax = joystickMid * 2;
    }

    for(int i = 0; i < ADC_COUNT; i++) {
        // 读取偏移量（不归一化）
        int16_t offset_x = 0, offset_y = 0;
        readPinOffset(i, offset_x, offset_y);
        
        // 统一归一化（XY轴使用相同的缩放系数）
        float normalized_x, normalized_y;
        unifiedNormalize(i, offset_x, offset_y, normalized_x, normalized_y);
        
        // 应用反转
        if (adc_pairs[i].analog_invert == InvertMode::INVERT_X || 
            adc_pairs[i].analog_invert == InvertMode::INVERT_XY) {
            normalized_x = ANALOG_MAX - normalized_x;
        }
        if (adc_pairs[i].analog_invert == InvertMode::INVERT_Y || 
            adc_pairs[i].analog_invert == InvertMode::INVERT_XY) {
            normalized_y = ANALOG_MAX - normalized_y;
        }
        
        // 应用EMA平滑
        if (adc_pairs[i].ema_option) {
            normalized_x = emaCalculation(i, normalized_x, adc_pairs[i].x_ema);
            normalized_y = emaCalculation(i, normalized_y, adc_pairs[i].y_ema);
            adc_pairs[i].x_ema = normalized_x;
            adc_pairs[i].y_ema = normalized_y;
        }
        
        // 应用内死区
        float x_magnitude = normalized_x - ANALOG_CENTER;
        float y_magnitude = normalized_y - ANALOG_CENTER;
        float distance = std::sqrt(x_magnitude * x_magnitude + y_magnitude * y_magnitude);
        
        if (distance < adc_pairs[i].in_deadzone) {
            normalized_x = ANALOG_CENTER;
            normalized_y = ANALOG_CENTER;
        }
        
        // 转换为游戏手柄协议格式
        uint16_t clampedX = (uint16_t)std::min(
            (uint32_t)(joystickMax * std::min(normalized_x, 1.0f)), 
            (uint32_t)0xFFFF
        );
        uint16_t clampedY = (uint16_t)std::min(
            (uint32_t)(joystickMax * std::min(normalized_y, 1.0f)), 
            (uint32_t)0xFFFF
        );

        if (adc_pairs[i].analog_dpad == DpadMode::DPAD_MODE_LEFT_ANALOG) {
            gamepad->state.lx = clampedX;
            gamepad->state.ly = clampedY;
        } else if (adc_pairs[i].analog_dpad == DpadMode::DPAD_MODE_RIGHT_ANALOG) {
            gamepad->state.rx = clampedX;
            gamepad->state.ry = clampedY;
        }
    }
}
```

---

## 六、外圈校准数据映射到0.5范围的讨论

### 6.1 当前外圈校准数据的含义

当前采样阶段收集的 `distance` 是：
```typescript
distance = sqrt((x - center_x)² + (y - center_y)²) / (ADC_MAX / 2)
```

这个值的范围可能是：
- **最小值**: 0.0（中心点）
- **最大值**: 可能大于 1.0（如果中心点不在ADC中点）

### 6.2 映射到0.5范围的方案

#### 方案A: 直接缩放

```cpp
// 假设 max_radius 的最大值是 1.0（或略大于1.0）
// 映射到 0.5
float scale_to_0_5 = 0.5f / max_radius;
normalized_x *= scale_to_0_5;
normalized_y *= scale_to_0_5;
```

**问题**：
- 如果 `max_radius < 0.5`，会导致缩放系数 > 1.0，可能超出范围
- 如果 `max_radius > 1.0`，缩放系数 < 0.5，可能无法充分利用摇杆范围

#### 方案B: 使用采样时的最大可能距离作为基准

```cpp
// 采样时使用的归一化方式
float max_possible_distance_sampling = max(
    max(center_x, ADC_MAX - center_x),
    max(center_y, ADC_MAX - center_y)
);

// 采样时收集的 distance 是相对于 max_possible_distance_sampling 的
// 需要映射到 0.5
float scale_to_0_5 = 0.5f / max_possible_distance_sampling;
normalized_x *= scale_to_0_5;
normalized_y *= scale_to_0_5;
```

**问题**：
- 需要存储采样时的 `max_possible_distance_sampling`
- 或者假设采样时也使用相同的归一化方式

#### 方案C: 不映射到0.5，直接使用校准数据

```cpp
// 不进行额外的映射，直接使用校准数据限制范围
if (normalized_distance > max_radius) {
    float scale = max_radius / normalized_distance;
    normalized_x *= scale;
    normalized_y *= scale;
}

// 然后直接转换为最终值
normalized_x += ANALOG_CENTER;  // 0.5
normalized_y += ANALOG_CENTER;  // 0.5
```

**优点**：
- 简单直接
- 不需要额外的映射步骤
- 校准数据直接限制范围

**问题**：
- 如果 `max_radius > 0.5`，最终值可能超出 [0.0, 1.0] 范围
- 需要确保 `max_radius` 的最大值不超过 0.5

### 6.3 推荐方案

**推荐使用方案C（直接使用校准数据限制范围）**，但需要：

1. **调整采样阶段的归一化方式**：
   ```typescript
   // 使用与后端相同的归一化方式
   const max_possible_distance = Math.max(
       Math.max(centerX, ADC_MAX - centerX),
       Math.max(centerY, ADC_MAX - centerY)
   );
   
   const distance = Math.sqrt(
       (data.x - centerX) ** 2 + (data.y - centerY) ** 2
   ) / max_possible_distance;
   ```

2. **确保 max_radius 的最大值不超过 0.5**：
   - 在采样时，如果 `distance > 0.5`，可以裁剪到 0.5
   - 或者使用 `min(distance, 0.5)` 作为校准数据

3. **或者允许 max_radius > 0.5，但在应用时进行缩放**：
   ```cpp
   if (max_radius > 0.5f) {
       // 如果校准数据超出0.5范围，进行缩放
       float scale = 0.5f / max_radius;
       max_radius = 0.5f;
   }
   ```

---

## 七、总结

### 7.1 可行性结论

✅ **方案完全可行**，具有以下优势：

1. **角度保真性**：✅ 完全保真
2. **距离比例保真性**：✅ 完全保真
3. **外圈校准数据保真性**：✅ 显著提升
4. **代码简化**：✅ 更清晰、更易维护
5. **性能提升**：✅ 减少重复计算

### 7.2 需要注意的问题

1. **向后兼容性**：需要处理现有校准数据的迁移
2. **边界情况**：中心点非常接近边界的情况
3. **外圈校准数据映射**：需要统一采样阶段和应用阶段的归一化方式

### 7.3 推荐实施步骤

1. **第一步**：修改 `readPin()` 函数，改为返回偏移量（不归一化）
2. **第二步**：实现统一归一化函数，使用相同的缩放系数
3. **第三步**：修改采样阶段的归一化方式，使其与后端一致
4. **第四步**：实现外圈校准数据的应用逻辑
5. **第五步**：测试和验证角度保真性、距离比例保真性

### 7.4 数据保真性结论

✅ **数据完全保真**：
- 角度信息完全保留
- 距离比例完全保留
- 几何关系完全保留
- 外圈校准数据可以正确应用

**这是一个优秀的方案，应该实施！**

