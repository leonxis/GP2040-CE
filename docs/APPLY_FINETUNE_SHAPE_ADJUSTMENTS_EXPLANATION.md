# applyFinetuneShapeAdjustments 函数逐行解释

## 函数概述
该函数在初始化时对外圈校准数据进行调整，根据"强制圆形"开关状态和百分比/扩大系数设置，修改 `range_data` 数组中的缩放比值。

---

## 逐行代码解释

### 函数签名和初始检查（335-338行）

```cpp
void AnalogInput::applyFinetuneShapeAdjustments(int stick_num) {
    if (!adc_pairs[stick_num].has_range_calibration) {
        return;  // No calibration data to adjust
    }
```

- **335行**：函数定义，接收摇杆编号（0或1）
- **336行**：检查是否有外圈校准数据
- **337行**：如果没有校准数据，直接返回，无需调整

---

### 常量定义（340-346行）

```cpp
    const int CIRCULARITY_DATA_SIZE = 48;
    // Index mapping: angle = (index * 2π / 48) - π
    // 0° (right): index = 24
    // 90° (top): index = 36  
    // 180° (left): index = 0
    // 270° (bottom): index = 12
    const int cardinalIndices[4] = {24, 36, 0, 12};  // Right, Top, Left, Bottom
```

- **340行**：定义圆周数据大小常量（48个角度索引）
- **341-345行**：注释说明索引与角度的映射关系
  - 索引 24 对应 0°（右侧）
  - 索引 36 对应 90°（上方）
  - 索引 0 对应 180°（左侧）
  - 索引 12 对应 270°（下方）
- **346行**：定义四个主要方向的索引数组（右、上、左、下）

---

### 保存原始校准数据（348-352行）

```cpp
    // Save original calibration values before modification
    float originalRangeData[48];
    for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
        originalRangeData[i] = adc_pairs[stick_num].range_data[i];
    }
```

- **348行**：注释说明需要保存原始数据
- **349行**：创建临时数组存储原始校准值
- **350-352行**：循环复制所有48个索引的原始校准值到临时数组
  - **目的**：在修改 `range_data` 前保存原始值，用于后续插值计算

---

### 分支1：强制圆形开启（354-364行）

```cpp
    if (adc_pairs[stick_num].finetune_shape_force_circular) {
        // Case 2: Force circular enabled - apply amplify factor to all indices
        // All scaling ratios = calibration_value / (1 + amplify%)
        float amplifyFactor = 1.0f + adc_pairs[stick_num].finetune_shape_amplify / 100.0f;
        if (amplifyFactor > 0.0f) {
            for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
                if (adc_pairs[stick_num].range_data[i] > 0.0f) {
                    adc_pairs[stick_num].range_data[i] /= amplifyFactor;
                }
            }
        }
    }
```

- **354行**：检查"强制圆形"开关是否开启
- **355-356行**：注释说明此分支的逻辑
- **357行**：计算扩大系数
  - 例如：扩大系数 10% → `amplifyFactor = 1.0 + 10/100 = 1.1`
- **358行**：检查扩大系数是否大于0（防止除零错误）
- **359-363行**：遍历所有48个索引
  - **360行**：检查当前索引是否有有效校准数据（> 0）
  - **361行**：将所有缩放比除以扩大系数
    - **公式**：`新缩放比 = 原始校准值 / (1 + 扩大系数%)`
    - **效果**：扩大系数越大，缩放比越小，摇杆外圈覆盖范围越大

---

### 分支2：强制圆形关闭（365-470行）

#### 2.1 定义主要方向的缩放系数（369-374行）

```cpp
    } else {
        // Case 1: Force circular disabled - apply percentage adjustments to cardinal indices only
        // Cardinal indices: calibration_value * (percent / 100)
        // Other indices: 1.0 (will be interpolated)
        const float cardinalScales[4] = {
            adc_pairs[stick_num].finetune_shape_y_right_percent / 100.0f,    // Right (0°, index 24)
            adc_pairs[stick_num].finetune_shape_x_top_percent / 100.0f,      // Top (90°, index 36)
            adc_pairs[stick_num].finetune_shape_y_left_percent / 100.0f,      // Left (180°, index 0)
            adc_pairs[stick_num].finetune_shape_x_bottom_percent / 100.0f     // Bottom (270°, index 12)
        };
```

- **365行**：`else` 分支，处理强制圆形关闭的情况
- **366-368行**：注释说明此分支的逻辑
- **369-374行**：定义四个主要方向的缩放系数数组
  - 将百分比值（如110%）转换为系数（1.1）
  - 索引对应关系：
    - `cardinalScales[0]` → 右侧（索引24）
    - `cardinalScales[1]` → 上方（索引36）
    - `cardinalScales[2]` → 左侧（索引0）
    - `cardinalScales[3]` → 下方（索引12）

---

#### 2.2 应用百分比调整到主要方向（376-382行）

```cpp
        // Apply percentage adjustments to cardinal indices
        for (int i = 0; i < 4; i++) {
            int cardIndex = cardinalIndices[i];
            if (adc_pairs[stick_num].range_data[cardIndex] > 0.0f) {
                adc_pairs[stick_num].range_data[cardIndex] *= cardinalScales[i];
            }
        }
```

- **376行**：注释说明对主要方向应用百分比调整
- **377-382行**：遍历四个主要方向
  - **378行**：获取当前主要方向的索引（24, 36, 0, 12之一）
  - **379行**：检查该索引是否有有效校准数据
  - **380行**：将原始校准值乘以对应的百分比系数
    - **公式**：`新缩放比 = 原始校准值 × (百分比 / 100)`
    - **示例**：原始值 1.0，百分比 110% → 新值 = 1.0 × 1.1 = 1.1

---

#### 2.3 对非主要方向进行插值（384-469行）

```cpp
        // For non-cardinal indices, set to 1.0 (will be interpolated in getInterpolatedScale)
        // Actually, we need to interpolate here to get proper values for all indices
        for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
            // Check if this is a cardinal index
            bool isCardinal = false;
            for (int j = 0; j < 4; j++) {
                if (i == cardinalIndices[j]) {
                    isCardinal = true;
                    break;
                }
            }
```

- **384-385行**：注释说明需要对非主要方向进行插值
- **386行**：遍历所有48个索引
- **387-394行**：检查当前索引是否是主要方向
  - **388行**：初始化标志变量
  - **389-393行**：遍历四个主要方向索引，检查是否匹配
  - **392行**：如果匹配，设置标志并跳出循环

---

#### 2.4 计算非主要方向的角度（396-407行）

```cpp
            if (!isCardinal && adc_pairs[stick_num].range_data[i] > 0.0f) {
                // ... (注释)
                
                // Interpolate between adjacent cardinal directions to get scaling ratio
                float angle = (i * 2.0f * M_PI / CIRCULARITY_DATA_SIZE) - M_PI;
                float angleDeg = (angle * 180.0f / M_PI + 360.0f);
                while (angleDeg >= 360.0f) angleDeg -= 360.0f;
```

- **396行**：如果当前索引不是主要方向且有有效数据，进入插值逻辑
- **404行**：计算当前索引对应的角度（弧度）
  - **公式**：`angle = (索引 × 2π / 48) - π`
  - **范围**：[-π, π]
- **405行**：将角度转换为度数并归一化到 [0, 360)
  - 先加360确保非负，再取模
- **406行**：确保角度在 [0, 360) 范围内

---

#### 2.5 查找相邻的主要方向（408-438行）

```cpp
                // Find the two adjacent cardinal directions
                int prevCardinalIdx = 3;
                int nextCardinalIdx = 0;
                
                for (int j = 0; j < 4; j++) {
                    int currIdx = j;
                    int nextIdx = (j + 1) % 4;
                    
                    float currAngle = (cardinalIndices[currIdx] * 2.0f * M_PI / CIRCULARITY_DATA_SIZE) - M_PI;
                    float nextAngle = (cardinalIndices[nextIdx] * 2.0f * M_PI / CIRCULARITY_DATA_SIZE) - M_PI;
                    
                    float currAngleDeg = (currAngle * 180.0f / M_PI + 360.0f);
                    while (currAngleDeg >= 360.0f) currAngleDeg -= 360.0f;
                    float nextAngleDeg = (nextAngle * 180.0f / M_PI + 360.0f);
                    while (nextAngleDeg >= 360.0f) nextAngleDeg -= 360.0f;
                    
                    if (nextAngleDeg < currAngleDeg) nextAngleDeg += 360.0f;
                    
                    bool angleInRange = false;
                    if (angleDeg >= currAngleDeg && angleDeg <= nextAngleDeg) {
                        angleInRange = true;
                    } else if (currAngleDeg > 270.0f && angleDeg < 90.0f) {
                        angleInRange = true;
                    }
                    
                    if (angleInRange) {
                        prevCardinalIdx = currIdx;
                        nextCardinalIdx = nextIdx;
                        break;
                    }
                }
```

- **408-410行**：初始化相邻主要方向的索引（默认值）
- **412-438行**：遍历四个主要方向对，找到当前角度所在区间
  - **413-414行**：获取当前主要方向索引和下一个主要方向索引（循环）
  - **416-417行**：计算两个主要方向的角度（弧度）
  - **419-422行**：转换为度数并归一化
  - **424行**：处理角度跨越0°的情况（如从350°到10°）
  - **426-431行**：判断当前角度是否在两个主要方向之间
    - **427-428行**：正常情况：角度在区间内
    - **429-430行**：特殊情况：处理跨越0°的区间（如270°到90°）
  - **433-437行**：如果找到匹配区间，保存索引并跳出循环

---

#### 2.6 计算插值因子（440-460行）

```cpp
                // Calculate interpolation factor
                float prevAngle = (cardinalIndices[prevCardinalIdx] * 2.0f * M_PI / CIRCULARITY_DATA_SIZE) - M_PI;
                float nextAngle = (cardinalIndices[nextCardinalIdx] * 2.0f * M_PI / CIRCULARITY_DATA_SIZE) - M_PI;
                
                float prevAngleDeg = (prevAngle * 180.0f / M_PI + 360.0f);
                while (prevAngleDeg >= 360.0f) prevAngleDeg -= 360.0f;
                float nextAngleDeg = (nextAngle * 180.0f / M_PI + 360.0f);
                while (nextAngleDeg >= 360.0f) nextAngleDeg -= 360.0f;
                
                if (nextAngleDeg < prevAngleDeg) nextAngleDeg += 360.0f;
                
                float t_interp = 0.0f;
                if (prevAngleDeg <= angleDeg && angleDeg <= nextAngleDeg) {
                    t_interp = (angleDeg - prevAngleDeg) / (nextAngleDeg - prevAngleDeg);
                } else if (prevAngleDeg > 270.0f && angleDeg < 90.0f) {
                    float dist = (angleDeg + 360.0f - prevAngleDeg);
                    while (dist >= 360.0f) dist -= 360.0f;
                    float total = (nextAngleDeg + 360.0f - prevAngleDeg);
                    while (total >= 360.0f) total -= 360.0f;
                    t_interp = dist / total;
                }
```

- **440行**：注释说明计算插值因子
- **441-442行**：重新计算相邻主要方向的角度（弧度）
- **444-448行**：转换为度数并归一化，处理跨越0°的情况
- **451行**：初始化插值因子 `t_interp`（范围 [0, 1]）
- **452-454行**：正常情况：计算线性插值因子
  - **公式**：`t = (当前角度 - 前一个角度) / (后一个角度 - 前一个角度)`
- **455-459行**：特殊情况：处理跨越0°的区间
  - **456行**：计算从 `prevAngleDeg` 到 `angleDeg` 的距离（考虑跨越0°）
  - **457行**：确保距离在 [0, 360) 范围内
  - **458行**：计算总区间长度
  - **459行**：确保总长度在 [0, 360) 范围内
  - **460行**：计算插值因子

---

#### 2.7 应用插值结果（462-467行）

```cpp
                // Interpolate scaling ratio between cardinal directions
                // For non-cardinal indices, interpolate between adjacent cardinal scales
                float interpolatedScaleRatio = cardinalScales[prevCardinalIdx] * (1.0f - t_interp) + 
                                              cardinalScales[nextCardinalIdx] * t_interp;
                // Apply interpolated scaling ratio to original calibration value
                adc_pairs[stick_num].range_data[i] = originalRangeData[i] * interpolatedScaleRatio;
```

- **462-463行**：注释说明插值缩放系数
- **464-465行**：线性插值计算缩放系数
  - **公式**：`插值系数 = 前一个系数 × (1 - t) + 后一个系数 × t`
  - **示例**：前一个系数 1.1，后一个系数 1.0，t = 0.5 → 插值系数 = 1.1 × 0.5 + 1.0 × 0.5 = 1.05
- **466行**：注释说明应用插值系数到原始校准值
- **467行**：将原始校准值乘以插值系数，得到最终的缩放比
  - **公式**：`最终缩放比 = 原始校准值 × 插值系数`

---

### 方形裁剪（472-491行）

```cpp
    // Apply square trimming to prevent output values > 1.0
    // Convert each radius to cartesian, trim to square, convert back
    for (int i = 0; i < CIRCULARITY_DATA_SIZE; i++) {
        if (adc_pairs[stick_num].range_data[i] > 0.0f) {
            float angle = (i * 2.0f * M_PI / CIRCULARITY_DATA_SIZE) - M_PI;
            float radius = adc_pairs[stick_num].range_data[i];
            
            // Convert polar to cartesian
            float x = radius * std::cos(angle);
            float y = radius * std::sin(angle);
            
            // Trim to square [-1, 1]
            float trimmedX, trimmedY;
            trimToSquare(x, y, trimmedX, trimmedY);
            
            // Convert back to polar
            float trimmedRadius = std::sqrt(trimmedX * trimmedX + trimmedY * trimmedY);
            adc_pairs[stick_num].range_data[i] = trimmedRadius;
        }
    }
```

- **472-473行**：注释说明应用方形裁剪，防止输出值 > 1.0
- **474行**：遍历所有48个索引
- **475行**：检查是否有有效数据
- **476行**：计算当前索引对应的角度（弧度）
- **477行**：获取当前的半径值（缩放比）
- **479-481行**：极坐标转笛卡尔坐标
  - **公式**：`x = 半径 × cos(角度)`，`y = 半径 × sin(角度)`
- **483-485行**：调用 `trimToSquare` 函数进行方形裁剪
  - 将 x, y 限制到 [-1, 1] 范围内
- **487-489行**：笛卡尔坐标转回极坐标
  - **公式**：`裁剪后半径 = √(x² + y²)`
  - **目的**：确保调整后的缩放比不会导致输出值超过 1.0

---

## 总结

该函数的主要功能：

1. **保存原始数据**：在修改前备份所有校准值
2. **分支处理**：
   - **强制圆形开启**：所有索引统一应用扩大系数
   - **强制圆形关闭**：主要方向应用百分比调整，非主要方向通过插值计算
3. **方形裁剪**：确保所有调整后的缩放比不会导致输出值超过 1.0

这样处理后，`range_data` 数组已经包含了所有调整，后续的 `getInterpolatedScale` 函数只需要进行简单的线性插值即可。

