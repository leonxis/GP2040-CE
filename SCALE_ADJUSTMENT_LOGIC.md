# 缩放比及百分比应用逻辑说明

## 1. 前端逻辑 (`applyFinetuneShapeAdjustments`)

### 主要方向映射
```typescript
// Index mapping: angle = (index * 2π / 48) - π
// 0° (right): index = 24
// 90° (top): index = 36  
// 180° (left): index = 0
// 270° (bottom): index = 12

const cardinalIndices = [
  { index: 24, scale: yRightPercent / 100.0, angle: 0 },    // Right (0°)
  { index: 36, scale: xTopPercent / 100.0, angle: 90 },     // Top (90°)
  { index: 0, scale: yLeftPercent / 100.0, angle: 180 },    // Left (180°)
  { index: 12, scale: xBottomPercent / 100.0, angle: 270 }   // Bottom (270°)
];
```

### 缩放因子计算逻辑

#### 情况1：精确的主要方向（0°, 90°, 180°, 270°）
- 直接使用对应方向的百分比作为缩放因子
- 例如：index 36 (90°) → 使用 `xTopPercent / 100.0`

#### 情况2：非主要方向（其他角度）
- 找到该角度所在的两个相邻主要方向
- 使用线性插值计算缩放因子：
  ```
  scaleFactor = prevCardinal.scale * (1 - t) + nextCardinal.scale * t
  ```
  其中 `t` 是角度插值因子（0.0 到 1.0）

### 示例：45°方向（在0°和90°之间）
- 前一个主要方向：0° (Right)，缩放因子 = `yRightPercent / 100.0`
- 后一个主要方向：90° (Top)，缩放因子 = `xTopPercent / 100.0`
- 插值因子：`t = (45 - 0) / (90 - 0) = 0.5`
- 最终缩放因子：`yRightPercent/100 * 0.5 + xTopPercent/100 * 0.5`

### 应用到所有索引
```typescript
return rangeData.map((value: number, index: number) => {
  if (value <= 0) return value;
  
  let scaleFactor = getScaleFactor(index);  // 获取插值后的缩放因子
  
  // 如果启用强制圆形且放大系数 > 0
  if (forceCircular && amplify > 0) {
    scaleFactor *= (1.0 + amplify / 100.0);
  }
  
  // 应用到原始校准值
  return value * scaleFactor;
});
```

## 2. 后端逻辑 (`getInterpolatedScale`)

### 步骤1：获取基础校准数据的插值
```cpp
// 计算角度对应的索引（带小数部分）
float normalizedAngle = (angle + M_PI) / (2.0f * M_PI);
float index = normalizedAngle * CIRCULARITY_DATA_SIZE;

// 获取相邻两个索引
int i0 = ((int)std::floor(index)) % CIRCULARITY_DATA_SIZE;
int i1 = (i0 + 1) % CIRCULARITY_DATA_SIZE;
float t = index - std::floor(index);

// 线性插值
float baseScale = range_data[i0] * (1.0f - t) + range_data[i1] * t;
```

### 步骤2：应用百分比调整（使用插值）
```cpp
// 4个主要方向的百分比
const float cardinalScales[4] = {
    yRightPercent / 100.0f,   // Right (0°)
    xTopPercent / 100.0f,      // Top (90°)
    yLeftPercent / 100.0f,     // Left (180°)
    xBottomPercent / 100.0f    // Bottom (270°)
};

// 如果是主要方向（±3.75°范围内），直接使用对应百分比
// 否则，在两个相邻主要方向之间插值
float scaleFactor = ...;  // 插值计算

// 应用放大系数（如果启用）
if (forceCircular && amplify > 0) {
    scaleFactor *= (1.0 + amplify / 100.0);
}

// 应用到基础校准值
return baseScale * scaleFactor;
```

## 3. 影响范围分析

### 当前行为
- **主要方向（0°, 90°, 180°, 270°）**：直接应用对应百分比
- **相邻区间**：使用线性插值，会受到相邻两个主要方向百分比的影响

### 示例：调整顶部百分比（90°）
- **直接影响**：index 36 (90°) 的缩放比
- **间接影响**：
  - 45°方向：受 0° 和 90° 百分比插值影响
  - 135°方向：受 90° 和 180° 百分比插值影响
  - 所有在 0°-90° 和 90°-180° 之间的角度都会受到影响

### 插值范围
- **0°-90°区间**：受 Right (0°) 和 Top (90°) 百分比影响
- **90°-180°区间**：受 Top (90°) 和 Left (180°) 百分比影响
- **180°-270°区间**：受 Left (180°) 和 Bottom (270°) 百分比影响
- **270°-360°区间**：受 Bottom (270°) 和 Right (0°) 百分比影响

## 4. 代码位置

### 前端
- 文件：`www/src/Addons/JoystickCalibration.tsx`
- 函数：`applyFinetuneShapeAdjustments` (第71-167行)
- 调用位置：
  - 模态框实时更新（第768行）
  - 主页面数据获取（第520行、第581行）

### 后端
- 文件：`src/addons/analog.cpp`
- 函数：`getInterpolatedScale` (第304-449行)
- 调用位置：`process()` 函数中获取缩放比（第150行）

## 5. 总结

**当前实现**：使用线性插值，调整4个主要方向的百分比会影响整个圆周的所有角度，相邻区间的缩放比会根据距离主要方向的远近进行插值计算。

**这是设计行为**：确保缩放比在角度之间平滑过渡，避免突然跳跃。

