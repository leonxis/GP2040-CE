# 摇杆数值计算逻辑流程分析与验证

## 一、用户提出的逻辑流程

### 1.1 流程步骤

1. **获得ADC原始数据**
   - 坐标系：ADC坐标系（左上角为原点(0,0)，xy轴最大值为4095）

2. **坐标平移变换（offset）**
   - 将ADC坐标系进行平移变换，得到adc_offset坐标系
   - 变换：`offset_xy = adc_xy - dX_value`

3. **外圈数据缩放校准**
   - 将adc_offset坐标系的原点移动到(2047.5, 2047.5)，得到adc_offset_center坐标系

4. **缩放变换**
   - 在adc_offset_center坐标系中，沿原点方向按照scale进行缩放变换
   - 得到最终的位于adc_offset_center坐标系中的摇杆坐标scaled_xy

5. **反向变换**
   - 将scaled_xy反向变换回原始ADC坐标系
   - 并对其进行归一化，准备用于手柄协议使用

---

## 二、详细分析

### 2.1 步骤1：ADC原始数据

**坐标系定义**：
- **ADC坐标系**：左上角为原点(0,0)，右下角为(4095, 4095)
- **数据**：`adc_x`, `adc_y`（范围：0-4095）

**特点**：
- 原点在左上角
- X轴向右为正，Y轴向下为正（或向上为正，取决于定义）
- 中心点理论位置：(2047.5, 2047.5)

### 2.2 步骤2：坐标平移变换（offset）

**变换公式**：
```cpp
offset_x = adc_x - dX_value;
offset_y = adc_y - dY_value;
```

其中：
```cpp
dX_value = center_x - 2047.5;
dY_value = center_y - 2047.5;
```

**变换效果**：
- 将中心点从`(center_x, center_y)`移动到`(2047.5, 2047.5)`
- 所有摇杆数据都变换为adc_offset坐标系数据

**数学验证**：
```cpp
// 当adc_x = center_x时（中心点）
offset_x = center_x - (center_x - 2047.5) = 2047.5;  ✅

// 当adc_x = 4095时（最大位置）
offset_x = 4095 - (center_x - 2047.5) = 4095 - center_x + 2047.5;
// 如果center_x = 1900
offset_x = 4095 - 1900 + 2047.5 = 4242.5;  ✅
```

**结论**：✅ **步骤2正确**

### 2.3 步骤3：移动到adc_offset_center坐标系

**变换公式**：
```cpp
// 将adc_offset坐标系的原点移动到(2047.5, 2047.5)
offset_center_x = offset_x - 2047.5;
offset_center_y = offset_y - 2047.5;
```

**坐标系特点**：
- 原点在(2047.5, 2047.5)
- 相对于原点的坐标：`offset_center_x`, `offset_center_y`
- 范围：可能超出[-2047.5, 2047.5]

**数学验证**：
```cpp
// 当offset_x = 2047.5时（中心点）
offset_center_x = 2047.5 - 2047.5 = 0;  ✅

// 当offset_x = 4242.5时（最大位置）
offset_center_x = 4242.5 - 2047.5 = 2195;  ✅
```

**结论**：✅ **步骤3正确**

### 2.4 步骤4：缩放变换

**变换公式**：
```cpp
// 在adc_offset_center坐标系中，沿原点方向按照scale进行缩放
scaled_center_x = offset_center_x / scale;
scaled_center_y = offset_center_y / scale;
```

**变换特点**：
- **径向缩放**：保持角度不变，只缩放距离
- **缩放方向**：沿原点方向（径向）
- **缩放比例**：`1.0 / scale`

**数学验证**：
```cpp
// 当offset_center_x = 2195, scale = 1.0718时
scaled_center_x = 2195 / 1.0718 ≈ 2047.5;  ✅

// 当offset_center_x = 0时（中心点）
scaled_center_x = 0 / scale = 0;  ✅
```

**结论**：✅ **步骤4正确**

### 2.5 步骤5：反向变换回原始ADC坐标系

**变换公式**：
```cpp
// 将scaled_xy反向变换回原始ADC坐标系
scaled_x = scaled_center_x + 2047.5;
scaled_y = scaled_center_y + 2047.5;
```

**数学验证**：
```cpp
// 当scaled_center_x = 2047.5时
scaled_x = 2047.5 + 2047.5 = 4095.0;  ✅

// 当scaled_center_x = 0时（中心点）
scaled_x = 0 + 2047.5 = 2047.5;  ✅

// 当scaled_center_x = -2047.5时（最小位置）
scaled_x = -2047.5 + 2047.5 = 0;  ✅
```

**结论**：✅ **步骤5正确**

### 2.6 归一化处理

**归一化公式**：
```cpp
// 归一化到[0.0, 1.0]范围
x_value = (scaled_x - 2047.5) / 4095.0 + 0.5;
y_value = (scaled_y - 2047.5) / 4095.0 + 0.5;
```

**或者更简洁**：
```cpp
x_value = scaled_x / 4095.0;
y_value = scaled_y / 4095.0;
```

**数学验证**：
```cpp
// 当scaled_x = 4095.0时（最大值）
x_value = (4095.0 - 2047.5) / 4095.0 + 0.5
        = 2047.5 / 4095.0 + 0.5
        = 0.5 + 0.5
        = 1.0;  ✅

// 当scaled_x = 2047.5时（中心点）
x_value = (2047.5 - 2047.5) / 4095.0 + 0.5
        = 0 / 4095.0 + 0.5
        = 0.5;  ✅

// 当scaled_x = 0时（最小值）
x_value = (0 - 2047.5) / 4095.0 + 0.5
        = -2047.5 / 4095.0 + 0.5
        = -0.5 + 0.5
        = 0.0;  ✅
```

**结论**：✅ **归一化处理正确**

---

## 三、完整流程验证

### 3.1 完整计算流程

```cpp
// 步骤1：获得ADC原始数据
uint16_t adc_x = adc_read_x();  // ADC坐标系
uint16_t adc_y = adc_read_y();  // ADC坐标系

// 步骤2：坐标平移变换（offset）
float dX_value = center_x - 2047.5;
float dY_value = center_y - 2047.5;
float offset_x = adc_x - dX_value;  // adc_offset坐标系
float offset_y = adc_y - dY_value;  // adc_offset坐标系

// 步骤3：移动到adc_offset_center坐标系
float offset_center_x = offset_x - 2047.5;  // adc_offset_center坐标系
float offset_center_y = offset_y - 2047.5;  // adc_offset_center坐标系

// 步骤4：缩放变换（径向缩放）
float angle = atan2(offset_center_y, offset_center_x);
float scale = getInterpolatedScale(angle);  // 从外圈校准数据获取
float scaled_center_x = offset_center_x / scale;  // adc_offset_center坐标系
float scaled_center_y = offset_center_y / scale;  // adc_offset_center坐标系

// 步骤5：反向变换回原始ADC坐标系
float scaled_x = scaled_center_x + 2047.5;  // ADC坐标系
float scaled_y = scaled_center_y + 2047.5;  // ADC坐标系

// 归一化处理
float x_value = (scaled_x - 2047.5) / 4095.0 + 0.5;  // [0.0, 1.0]
float y_value = (scaled_y - 2047.5) / 4095.0 + 0.5;  // [0.0, 1.0]
```

### 3.2 简化公式

**可以简化为**：
```cpp
// 步骤2-3合并
float offset_center_x = (adc_x - dX_value) - 2047.5;
float offset_center_y = (adc_y - dY_value) - 2047.5;

// 步骤4：缩放
float scale = getInterpolatedScale(atan2(offset_center_y, offset_center_x));
float scaled_center_x = offset_center_x / scale;
float scaled_center_y = offset_center_y / scale;

// 步骤5：反向变换 + 归一化
float x_value = (scaled_center_x + 2047.5 - 2047.5) / 4095.0 + 0.5;
float y_value = (scaled_center_y + 2047.5 - 2047.5) / 4095.0 + 0.5;

// 进一步简化
float x_value = scaled_center_x / 4095.0 + 0.5;
float y_value = scaled_center_y / 4095.0 + 0.5;
```

**或者更简洁**：
```cpp
float offset_center_x = (adc_x - dX_value) - 2047.5;
float offset_center_y = (adc_y - dY_value) - 2047.5;
float scale = getInterpolatedScale(atan2(offset_center_y, offset_center_x));
float x_value = (offset_center_x / scale) / 4095.0 + 0.5;
float y_value = (offset_center_y / scale) / 4095.0 + 0.5;
```

---

## 四、数学验证（完整流程）

### 4.1 场景1：摇杆在外圈最大位置

**设置**：
- `center_x = 1900`, `center_y = 2047.5`
- `adc_x = 4095`, `adc_y = 2047.5`（外圈最大位置）
- `scale = 1.0718`（该方向的scale值）

**计算过程**：
```cpp
// 步骤1：ADC原始数据
adc_x = 4095;  // ADC坐标系

// 步骤2：坐标平移
dX_value = 1900 - 2047.5 = -147.5;
offset_x = 4095 - (-147.5) = 4242.5;  // adc_offset坐标系

// 步骤3：移动到adc_offset_center坐标系
offset_center_x = 4242.5 - 2047.5 = 2195;  // adc_offset_center坐标系

// 步骤4：缩放变换
scale = 1.0718;
scaled_center_x = 2195 / 1.0718 ≈ 2047.5;  // adc_offset_center坐标系

// 步骤5：反向变换
scaled_x = 2047.5 + 2047.5 = 4095.0;  // ADC坐标系

// 归一化
x_value = (4095.0 - 2047.5) / 4095.0 + 0.5
        = 2047.5 / 4095.0 + 0.5
        = 0.5 + 0.5
        = 1.0;  // ✅ 正确！正好是最大值
```

**验证**：✅ **逻辑正确**

### 4.2 场景2：摇杆在中心位置

**设置**：
- `center_x = 1900`, `center_y = 2047.5`
- `adc_x = 1900`, `adc_y = 2047.5`（中心点）
- `scale = 1.0718`

**计算过程**：
```cpp
// 步骤1：ADC原始数据
adc_x = 1900;  // ADC坐标系

// 步骤2：坐标平移
dX_value = 1900 - 2047.5 = -147.5;
offset_x = 1900 - (-147.5) = 2047.5;  // adc_offset坐标系

// 步骤3：移动到adc_offset_center坐标系
offset_center_x = 2047.5 - 2047.5 = 0;  // adc_offset_center坐标系

// 步骤4：缩放变换
scale = 1.0718;
scaled_center_x = 0 / 1.0718 = 0;  // adc_offset_center坐标系

// 步骤5：反向变换
scaled_x = 0 + 2047.5 = 2047.5;  // ADC坐标系

// 归一化
x_value = (2047.5 - 2047.5) / 4095.0 + 0.5
        = 0 / 4095.0 + 0.5
        = 0.5;  // ✅ 正确！正好是中心值
```

**验证**：✅ **逻辑正确**

### 4.3 场景3：摇杆在中间位置

**设置**：
- `center_x = 1900`, `center_y = 2047.5`
- `adc_x = 3000`, `adc_y = 2047.5`（中间位置）
- `scale = 1.0718`

**计算过程**：
```cpp
// 步骤1：ADC原始数据
adc_x = 3000;  // ADC坐标系

// 步骤2：坐标平移
dX_value = 1900 - 2047.5 = -147.5;
offset_x = 3000 - (-147.5) = 3147.5;  // adc_offset坐标系

// 步骤3：移动到adc_offset_center坐标系
offset_center_x = 3147.5 - 2047.5 = 1100;  // adc_offset_center坐标系

// 步骤4：缩放变换
scale = 1.0718;
scaled_center_x = 1100 / 1.0718 ≈ 1026.3;  // adc_offset_center坐标系

// 步骤5：反向变换
scaled_x = 1026.3 + 2047.5 ≈ 3073.8;  // ADC坐标系

// 归一化
x_value = (3073.8 - 2047.5) / 4095.0 + 0.5
        = 1026.3 / 4095.0 + 0.5
        ≈ 0.2506 + 0.5
        ≈ 0.7506;  // ✅ 在[0.0, 1.0]范围内，正确！
```

**验证**：✅ **逻辑正确**

---

## 五、逻辑流程总结

### 5.1 坐标系变换链

```
ADC坐标系 (0,0) 原点在左上角
  ↓ 步骤2：坐标平移
adc_offset坐标系 (2047.5, 2047.5) 中心点已移动到标准位置
  ↓ 步骤3：移动到中心
adc_offset_center坐标系 (0,0) 原点在(2047.5, 2047.5)
  ↓ 步骤4：缩放变换
adc_offset_center坐标系 (scaled_center_x, scaled_center_y) 缩放后的坐标
  ↓ 步骤5：反向变换
ADC坐标系 (scaled_x, scaled_y) 反向变换回原始坐标系
  ↓ 归一化
[0.0, 1.0]范围 (x_value, y_value) 最终输出值
```

### 5.2 关键点

1. **坐标平移（步骤2）**：
   - 将中心点从`(center_x, center_y)`移动到`(2047.5, 2047.5)`
   - 变换：`offset_xy = adc_xy - dX_value`

2. **移动到中心坐标系（步骤3）**：
   - 将原点移动到`(2047.5, 2047.5)`
   - 变换：`offset_center_xy = offset_xy - 2047.5`

3. **缩放变换（步骤4）**：
   - 径向缩放，保持角度不变
   - 变换：`scaled_center_xy = offset_center_xy / scale`

4. **反向变换（步骤5）**：
   - 将坐标变换回原始ADC坐标系
   - 变换：`scaled_xy = scaled_center_xy + 2047.5`

5. **归一化**：
   - 归一化到[0.0, 1.0]范围
   - 公式：`x_value = (scaled_x - 2047.5) / 4095.0 + 0.5`

---

## 六、结论

### 6.1 用户逻辑流程完全正确

✅ **用户提出的逻辑流程完全正确**：

1. **步骤1**：获得ADC原始数据 ✅
2. **步骤2**：坐标平移变换（offset） ✅
3. **步骤3**：移动到adc_offset_center坐标系 ✅
4. **步骤4**：缩放变换（径向缩放） ✅
5. **步骤5**：反向变换回原始ADC坐标系 ✅
6. **归一化**：归一化到[0.0, 1.0]范围 ✅

### 6.2 数学验证

✅ **所有数学验证都正确**：
- 外圈最大位置 → `x_value = 1.0` ✅
- 中心位置 → `x_value = 0.5` ✅
- 中间位置 → `x_value`在合理范围内 ✅

### 6.3 实现建议

**推荐的实现代码**：
```cpp
// 步骤1：获得ADC原始数据
uint16_t adc_x = adc_read_x();
uint16_t adc_y = adc_read_y();

// 步骤2-3：坐标平移并移动到中心坐标系
float dX_value = center_x - 2047.5;
float dY_value = center_y - 2047.5;
float offset_center_x = (adc_x - dX_value) - 2047.5;
float offset_center_y = (adc_y - dY_value) - 2047.5;

// 步骤4：缩放变换
float angle = atan2(offset_center_y, offset_center_x);
float scale = getInterpolatedScale(angle);
float scaled_center_x = offset_center_x / scale;
float scaled_center_y = offset_center_y / scale;

// 步骤5：反向变换 + 归一化
float x_value = scaled_center_x / 4095.0 + 0.5;
float y_value = scaled_center_y / 4095.0 + 0.5;
```

**或者更简洁**：
```cpp
float offset_center_x = (adc_x - (center_x - 2047.5)) - 2047.5;
float offset_center_y = (adc_y - (center_y - 2047.5)) - 2047.5;
float scale = getInterpolatedScale(atan2(offset_center_y, offset_center_x));
float x_value = (offset_center_x / scale) / 4095.0 + 0.5;
float y_value = (offset_center_y / scale) / 4095.0 + 0.5;
```

---

## 七、最终结论

✅ **用户提出的逻辑流程完全正确，可以作为实现依据！**

所有步骤都经过数学验证，逻辑清晰，实现简单。这是一个优秀的方案设计。

