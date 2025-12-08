# 外圈微调非圆度实现分析

## 概述

在网页项目（ds4）中，外圈微调（Finetune）功能提供了两种模式：
1. **Center Mode（中心模式）**：调整摇杆中心点
2. **Circularity Mode（圆形度模式）**：调整摇杆外圈的圆形度，包括非圆度调整

## 非圆度调整的实现

### 1. 调整的数据

非圆度调整同时影响两类数据：

#### A. 四个方向的输入值（LL, LT, LR, LB 或 RL, RT, RR, RB）
- **数据类型**：手柄协议原始值，范围 0-65535
- **作用**：直接控制摇杆在四个主要方向（左、上、右、下）的最大输出值
- **调整方式**：
  - **LL（左）/ LT（上）**：增加输入值 → 使摇杆在这些方向更容易达到最大值
  - **LR（右）/ LB（下）**：减少输入值 → 使摇杆在这些方向更难达到最大值

#### B. Circularity Data 数组（ll_data 或 rr_data）
- **数据类型**：48个角度的半径数据数组（归一化值）
- **作用**：用于可视化显示和误差率计算
- **调整方式**：所有48个角度的半径值都增加一个固定偏移量

### 2. 调整逻辑

#### 滑块映射
```javascript
const maxAdjustment = 175; // 最大调整值
const totalAdjustment = (value / 100) * maxAdjustment; // value 是滑块值 0-100
```

#### 四个方向输入值的调整
```javascript
// 对于左/上方向（LL/LT）
if (suffix.endsWith('L') || suffix.endsWith('T')) {
    newValue = Math.min(65535, startValues[suffix] + totalAdjustment);
}
// 对于右/下方向（LR/LB）
else if (suffix.endsWith('R') || suffix.endsWith('B')) {
    newValue = Math.max(0, startValues[suffix] - totalAdjustment);
}
```

#### Circularity Data 的调整
```javascript
const adjustmentConstant = 0.00085; // 调整常数
const totalAdjustmentFromBase = totalAdjustment * adjustmentConstant;

// 所有48个角度的半径都增加相同的偏移量
startingData.forEach((value, i) => 
    circData[i] = Math.max(0, value + totalAdjustmentFromBase)
);

// 修剪到正方形范围
_trimCircularityDataToSquare(circData);
```

### 3. 数据修剪函数

`_trimCircularityDataToSquare` 函数的作用：
1. 将极坐标（半径、角度）转换为笛卡尔坐标（x, y）
2. 将坐标修剪到 -1 到 1 的正方形范围内
3. 转换回极坐标（半径）

```javascript
_trimCircularityDataToSquare(data) {
    const numSectors = data.length;
    data.forEach((radius, i) => {
        const angle = (i * 2 * Math.PI) / numSectors;
        
        // 极坐标 → 笛卡尔坐标
        const x = radius * Math.cos(angle);
        const y = radius * Math.sin(angle);
        
        // 修剪到正方形
        const trimmedX = Math.max(-1, Math.min(1, x));
        const trimmedY = Math.max(-1, Math.min(1, y));
        
        // 笛卡尔坐标 → 极坐标
        const trimmedRadius = Math.sqrt(trimmedX * trimmedX + trimmedY * trimmedY);
        data[i] = trimmedRadius;
    });
}
```

## 非圆度调整的效果

### 调整方向输入值的效果

当滑块向右移动（增加非圆度）时：
- **LL（左）/ LT（上）** 增加 → 摇杆在这些方向更容易达到最大值
- **LR（右）/ LB（下）** 减少 → 摇杆在这些方向更难达到最大值

这导致摇杆的外圈形状从圆形变为**椭圆形**或**方形**，具体取决于调整的程度。

### 调整 Circularity Data 的效果

- 所有角度的半径都增加相同的偏移量
- 然后通过 `_trimCircularityDataToSquare` 修剪到正方形范围
- 这确保了可视化数据与实际调整保持一致

## 关键参数

1. **maxAdjustment = 175**：滑块最大值（100）对应的调整值
2. **adjustmentConstant = 0.00085**：Circularity Data 调整的缩放系数
3. **滑块范围**：0-100，映射到调整范围 0-175

## 在 GP2040-CE 中的适配

### 当前实现

GP2040-CE 项目目前使用：
- **48个缩放比数据**（`joystickRangeData1` 和 `joystickRangeData2`）
- 每个角度对应一个 `scale` 值（实际外圈距离 / 标准外圈半径）

### 适配建议

如果要实现类似的非圆度调整功能，可以考虑：

1. **调整四个方向的 scale 值**：
   - 左/上方向：增加 scale
   - 右/下方向：减少 scale
   - 其他角度：使用插值平滑过渡

2. **或者调整四个方向的校准数据**：
   - 类似于网页项目，直接调整四个主要方向的校准值
   - 然后重新计算或插值其他角度的 scale

3. **保持数据一致性**：
   - 确保调整后的数据仍然在合理范围内
   - 可能需要类似 `_trimCircularityDataToSquare` 的修剪函数

## 总结

网页项目中的非圆度调整：
- **主要调整**：四个方向的输入值（LL, LT, LR, LB）
- **辅助调整**：Circularity Data 数组（用于可视化）
- **效果**：使摇杆外圈从圆形变为椭圆形/方形
- **目的**：模拟 Sony 手柄的工厂校准特性（7-9% 的平均圆形度误差）

