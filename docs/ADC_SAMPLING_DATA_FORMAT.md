# 采样过程中ADC数据格式说明

## API返回的ADC值

### 1. API端点

**位置**: `src/webconfig.cpp`

```cpp
std::string getJoystickCenter() {
    uint16_t x = 0, y = 0;
    bool success = readJoystickADC(0, x, y);  // 读取左摇杆（stick 0）
    // ...
    o["x"] = x;  // 原始ADC值
    o["y"] = y;  // 原始ADC值
}
```

### 2. ADC读取函数

**位置**: `src/addons/analog_utils.cpp`

```cpp
bool readJoystickADC(uint8_t stickNum, uint16_t& x, uint16_t& y) {
    // ...
    // 直接读取原始ADC值
    adc_select_input(xPin - ADC_PIN_OFFSET);
    x = adc_read();  // 原始ADC值，0-4095
    
    adc_select_input(yPin - ADC_PIN_OFFSET);
    y = adc_read();  // 原始ADC值，0-4095
    // ...
}
```

---

## ADC值范围

### 范围定义

**ADC值范围**: **0 到 4095**（12位ADC）

```cpp
#define ADC_MAX ((1 << 12) - 1) // 4095
```

### 物理含义

- **0**: 对应 0V（GND）
- **4095**: 对应 3.3V（VREF）
- **中间值（约2047-2048）**: 对应 1.65V（理论中心电压）

---

## 坐标中心

### 重要说明

**API返回的是原始ADC值，没有固定的坐标中心！**

API直接返回 `adc_read()` 的原始值，范围是 0-4095。这些值**没有经过任何中心校准或归一化**。

### 前端处理时的中心点

在前端采样代码中，使用**已校准的中心值**作为参考点进行归一化：

**位置**: `www/src/Components/RangeCalibrationModal.tsx`

```typescript
// 获取中心值（已校准的中心点）
const ADC_MAX = 4095;
const centerXValue = centerX !== undefined ? centerX : ADC_MAX / 2;  // 默认2047.5
const centerYValue = centerY !== undefined ? centerY : ADC_MAX / 2;  // 默认2047.5

// 转换为归一化坐标 (-1 到 1)，以校准中心为原点
const stickX = ((data.x - centerXValue) / (ADC_MAX / 2));
const stickY = ((data.y - centerYValue) / (ADC_MAX / 2));
```

### 中心值的来源

1. **如果已进行中心校准**:
   - `centerX` = `joystickCenterX`（已校准的中心X值，0-4095）
   - `centerY` = `joystickCenterY`（已校准的中心Y值，0-4095）

2. **如果未进行中心校准**:
   - `centerX` = `ADC_MAX / 2` = `2047.5`（理论中心）
   - `centerY` = `ADC_MAX / 2` = `2047.5`（理论中心）

---

## 数据流示例

### 场景1: 已校准中心的情况

假设：
- 已校准中心：`joystickCenterX = 2100`, `joystickCenterY = 2050`
- 当前ADC值：`x = 3000`, `y = 2500`

**前端处理**:
```typescript
const centerXValue = 2100;  // 使用已校准中心
const centerYValue = 2050;

// 归一化计算
const stickX = ((3000 - 2100) / (4095 / 2)) = 900 / 2047.5 ≈ 0.439
const stickY = ((2500 - 2050) / (4095 / 2)) = 450 / 2047.5 ≈ 0.220

// 计算距离
const distance = sqrt(0.439² + 0.220²) ≈ 0.491
```

### 场景2: 未校准中心的情况

假设：
- 未校准中心：使用默认值
- 当前ADC值：`x = 3000`, `y = 2500`

**前端处理**:
```typescript
const centerXValue = 4095 / 2 = 2047.5;  // 使用理论中心
const centerYValue = 4095 / 2 = 2047.5;

// 归一化计算
const stickX = ((3000 - 2047.5) / (4095 / 2)) = 952.5 / 2047.5 ≈ 0.465
const stickY = ((2500 - 2047.5) / (4095 / 2)) = 452.5 / 2047.5 ≈ 0.221

// 计算距离
const distance = sqrt(0.465² + 0.221²) ≈ 0.515
```

---

## 关键点总结

### API返回的数据

| 属性 | 值 |
|------|-----|
| **数据类型** | `uint16_t` (无符号16位整数) |
| **数值范围** | **0 到 4095** |
| **单位** | ADC原始值（无单位） |
| **坐标中心** | **无固定中心**（原始ADC值） |
| **是否校准** | **未校准**（直接读取的原始值） |

### 前端处理

| 属性 | 值 |
|------|-----|
| **中心参考** | 已校准中心值（`joystickCenterX/Y`）或理论中心（2047.5） |
| **归一化范围** | -1.0 到 1.0 |
| **距离范围** | 0.0 到 1.0（归一化距离） |

---

## 代码位置总结

| 功能 | 文件 | 函数/代码 |
|------|------|-----------|
| API端点 | `src/webconfig.cpp` | `getJoystickCenter()`, `getJoystickCenter2()` |
| ADC读取 | `src/addons/analog_utils.cpp` | `readJoystickADC()` |
| ADC定义 | `src/addons/analog.cpp` | `#define ADC_MAX 4095` |
| 前端处理 | `www/src/Components/RangeCalibrationModal.tsx` | `checkDataProgress()` |

---

## 注意事项

1. **API返回的是原始ADC值**，不是归一化值
2. **没有固定的坐标中心**，前端使用已校准的中心值或理论中心进行归一化
3. **中心值影响归一化结果**，如果中心校准不准确，会影响外圈校准的准确性
4. **建议先进行中心校准**，再进行外圈校准，以获得更准确的结果

---

## 数据流图

```
硬件ADC读取
  │
  ├─ adc_read() → 原始ADC值 (0-4095)
  │
  └─ 无中心概念，无校准
       │
       ▼
API返回 (getJoystickCenter)
  │
  ├─ JSON: {"success": true, "x": 3000, "y": 2500}
  │
  └─ 原始ADC值，范围 0-4095
       │
       ▼
前端接收
  │
  ├─ data.x = 3000 (原始ADC值)
  │
  ├─ data.y = 2500 (原始ADC值)
  │
  └─ 获取中心值: centerX/Y (已校准或默认2047.5)
       │
       ▼
前端归一化
  │
  ├─ stickX = (data.x - centerX) / (4095 / 2)  → -1 到 1
  │
  ├─ stickY = (data.y - centerY) / (4095 / 2)  → -1 到 1
  │
  └─ distance = sqrt(stickX² + stickY²)  → 0.0 到 1.0
       │
       ▼
采样判断
  │
  └─ if (distance > 0.85) { 记录数据 }
```

---

## 结论

**在采样过程中，API得到的ADC值范围是 0-4095（12位ADC原始值），没有固定的坐标中心。前端使用已校准的中心值（或理论中心2047.5）作为参考点进行归一化处理。**

