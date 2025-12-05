# 模拟摇杆最终输出数据格式

## 一、数据流概览

```
原始ADC值 (0-4095)
  ↓
readPin() 映射和归一化
  ↓
x_value, y_value (0.0-1.0 归一化值) ← **这是转换为手柄协议格式前的最终数据**
  ↓
死区处理、外圈校准等（处理后仍然是归一化值 0.0-1.0）
  ↓
转换为游戏手柄协议格式
  ↓
gamepad->state.lx/ly/rx/ry (16位无符号整数，0-65535)
  ↓
输出给电脑/主机
```

**关键点**：在转换为手柄协议格式之前，摇杆数据的最终结果是 **XY坐标的归一化值（0.0-1.0）**。

---

## 二、最终输出数据格式

### 数据类型

**位置**: `headers/gamepad/GamepadState.h`

```cpp
struct GamepadState {
    uint16_t lx {GAMEPAD_JOYSTICK_MID};  // 左摇杆X轴
    uint16_t ly {GAMEPAD_JOYSTICK_MID};  // 左摇杆Y轴
    uint16_t rx {GAMEPAD_JOYSTICK_MID};  // 右摇杆X轴
    uint16_t ry {GAMEPAD_JOYSTICK_MID};  // 右摇杆Y轴
    // ...
};
```

### 常量定义

```cpp
#define GAMEPAD_JOYSTICK_MIN 0      // 最小值: 0x0000 (0)
#define GAMEPAD_JOYSTICK_MID 0x7FFF // 中心值: 0x7FFF (32767)
#define GAMEPAD_JOYSTICK_MAX 0xFFFF // 最大值: 0xFFFF (65535)
```

### 数据范围

| 属性 | 值 |
|------|-----|
| **数据类型** | `uint16_t` (16位无符号整数) |
| **最小值** | `0` (0x0000) |
| **中心值** | `32767` (0x7FFF) |
| **最大值** | `65535` (0xFFFF) |
| **范围** | `0` 到 `65535` |

---

## 三、转换过程

### 代码位置
`src/addons/analog.cpp` - `process()`

```cpp
// 常量定义
#define ANALOG_MAX 1.0f        // 归一化最大值
#define ANALOG_CENTER 0.5f     // 归一化中心值
#define ANALOG_MINIMUM 0.0f     // 归一化最小值

// 1. 读取并归一化 (readPin返回 0.0-1.0)
adc_pairs[i].x_value = readPin(...);  // 0.0 到 1.0 (归一化值)
adc_pairs[i].y_value = readPin(...);  // 0.0 到 1.0 (归一化值)

// 2. 应用反转、EMA平滑等处理（处理后仍然是归一化值）
if (analog_invert == INVERT_X) {
    adc_pairs[i].x_value = ANALOG_MAX - adc_pairs[i].x_value;  // 仍然是 0.0-1.0
}
if (ema_option) {
    adc_pairs[i].x_value = emaCalculation(...);  // 仍然是 0.0-1.0
}

// 3. 应用死区、外圈校准等处理（处理后仍然是归一化值）
radialDeadzone(i, adc_pairs[i]);
// 此时 adc_pairs[i].x_value 和 adc_pairs[i].y_value 仍然是 0.0-1.0 的归一化值

// 4. 转换为游戏手柄协议格式（归一化值 → 16位整数）
uint32_t joystickMax = GAMEPAD_JOYSTICK_MAX;  // 0xFFFF (65535)
uint16_t clampedX = (uint16_t)std::min(
    (uint32_t)(joystickMax * std::min(adc_pairs[i].x_value, 1.0f)), 
    (uint32_t)0xFFFF
);
uint16_t clampedY = (uint16_t)std::min(
    (uint32_t)(joystickMax * std::min(adc_pairs[i].y_value, 1.0f)), 
    (uint32_t)0xFFFF
);

// 5. 输出到游戏手柄状态
gamepad->state.lx = clampedX;  // 16位无符号整数 (0-65535)
gamepad->state.ly = clampedY;  // 16位无符号整数 (0-65535)
```

**重要**：在步骤3之后、步骤4之前，`adc_pairs[i].x_value` 和 `adc_pairs[i].y_value` 是 **XY坐标的归一化值（0.0-1.0）**，这是转换为手柄协议格式前的最终数据。

### 转换公式

```
clampedX = joystickMax * x_value
         = 65535 * x_value
```

**示例**:
- `x_value = 0.0` → `clampedX = 0` (最小值)
- `x_value = 0.5` → `clampedX = 32767` (中心值)
- `x_value = 1.0` → `clampedX = 65535` (最大值)

---

## 四、数据格式说明

### 1. 不是原始ADC值

**最终输出不是原始ADC值（0-4095）**，而是经过以下处理：
- ✅ 中心校准映射（`readPin()`）
- ✅ 归一化（0.0-1.0）
- ✅ 死区处理
- ✅ 外圈校准
- ✅ 转换为游戏手柄协议格式（0-65535）

### 2. 不是归一化坐标值

**最终输出不是归一化坐标值（0.0-1.0）**，而是：
- ✅ 16位无符号整数（`uint16_t`）
- ✅ 范围：0 到 65535
- ✅ 符合游戏手柄协议标准

### 3. 是游戏手柄协议格式

**最终输出是游戏手柄协议标准格式**：
- ✅ **16位无符号整数** (`uint16_t`)
- ✅ **范围**: `0` 到 `65535` (0x0000 到 0xFFFF)
- ✅ **中心值**: `32767` (0x7FFF)
- ✅ **最小值**: `0` (0x0000)
- ✅ **最大值**: `65535` (0xFFFF)

---

## 五、不同驱动协议的处理

### XInput协议

**位置**: `src/drivers/xinput/XInputDriver.cpp`

```cpp
// XInput使用16位有符号整数，范围 -32768 到 32767
// 需要将无符号值转换为有符号值
newInputReport.leftStickX = static_cast<int16_t>(gamepad->state.lx) + INT16_MIN;
newInputReport.leftStickY = static_cast<int16_t>(~gamepad->state.ly) + INT16_MIN;
```

### PS3/PS4协议

**位置**: `src/drivers/ps3/PS3Driver.cpp`, `src/drivers/ps4/PS4Driver.cpp`

```cpp
// PS3/PS4使用8位无符号整数，范围 0 到 255
// 需要将16位值压缩到8位
ps3Report.leftStickX = static_cast<uint8_t>(gamepad->state.lx >> 8);
ps3Report.leftStickY = static_cast<uint8_t>(gamepad->state.ly >> 8);
```

### Switch Pro协议

**位置**: `src/drivers/switchpro/SwitchProDriver.cpp`

```cpp
// Switch Pro使用12位值，范围 0 到 4095
uint16_t scaleLeftStickX = scale16To12(gamepad->state.lx);
uint16_t scaleLeftStickY = scale16To12(gamepad->state.ly);
```

---

## 六、数据流详细示例

### 示例：摇杆推到最大位置

**步骤1: 读取原始ADC值**
```
adc_read() → x = 4095, y = 4095
```

**步骤2: readPin() 映射和归一化**
```
假设 centerX = 2047, centerY = 2047
x_value = readPin(..., centerX) → 1.0
y_value = readPin(..., centerY) → 1.0
```

**步骤3: 死区和外圈校准处理**
```
x_value = 1.0 (经过外圈校准后，可能被限制)
y_value = 1.0 (经过外圈校准后，可能被限制)
```

**步骤4: 转换为游戏手柄协议格式**
```
joystickMax = 65535
clampedX = 65535 * 1.0 = 65535
clampedY = 65535 * 1.0 = 65535
```

**步骤5: 输出到游戏手柄状态**
```
gamepad->state.lx = 65535
gamepad->state.ly = 65535
```

**步骤6: 驱动协议转换（以XInput为例）**
```
leftStickX = (int16_t)65535 + INT16_MIN = 32767
leftStickY = (int16_t)65535 + INT16_MIN = 32767
```

---

## 七、关键点总结

### 最终输出数据

| 属性 | 值 |
|------|-----|
| **数据类型** | `uint16_t` (16位无符号整数) |
| **数值范围** | `0` 到 `65535` |
| **中心值** | `32767` (0x7FFF) |
| **单位** | 游戏手柄协议标准单位（无物理单位） |
| **格式** | 符合USB HID游戏手柄协议规范 |

### 数据转换链

```
原始ADC (0-4095)
  → readPin() 映射
  → 归一化值 (0.0-1.0)
  → 死区/外圈校准处理
  → 游戏手柄协议值 (0-65535)
  → 驱动协议转换（根据目标协议）
  → 输出给电脑/主机
```

---

## 八、关键理解

### 转换前的数据格式

**在转换为手柄协议格式之前，摇杆数据的最终结果是 XY坐标的归一化值（0.0-1.0）**。

- ✅ **数据类型**: `float` (浮点数)
- ✅ **数值范围**: `0.0` 到 `1.0`
- ✅ **中心值**: `0.5`
- ✅ **存储位置**: `adc_pairs[i].x_value`, `adc_pairs[i].y_value`
- ✅ **常量定义**:
  - `ANALOG_MAX = 1.0f`
  - `ANALOG_CENTER = 0.5f`
  - `ANALOG_MINIMUM = 0.0f`

### 转换后的数据格式

**模拟摇杆最终用于输出给电脑确定摇杆位置的数据是：16位无符号整数（uint16_t），范围 0 到 65535，符合游戏手柄协议标准格式。**

- ✅ **数据类型**: `uint16_t` (16位无符号整数)
- ✅ **数值范围**: `0` 到 `65535` (0x0000 到 0xFFFF)
- ✅ **中心值**: `32767` (0x7FFF)
- ✅ **最小值**: `0` (0x0000)
- ✅ **最大值**: `65535` (0xFFFF)
- ✅ **存储位置**: `gamepad->state.lx/ly/rx/ry`

### 转换关系

```
归一化值 (0.0-1.0) × 65535 = 游戏手柄协议值 (0-65535)
```

**示例**:
- `x_value = 0.0` → `输出 = 0` (最小值)
- `x_value = 0.5` → `输出 = 32767` (中心值)
- `x_value = 1.0` → `输出 = 65535` (最大值)

### 结论

1. **转换前**: XY坐标的归一化值（0.0-1.0），这是所有校准和处理后的最终结果
2. **转换后**: 16位无符号整数（0-65535），符合游戏手柄协议标准格式
3. **转换目的**: 将归一化值映射到游戏手柄协议要求的数值范围

这个格式符合USB HID游戏手柄协议规范，可以被各种游戏手柄驱动（XInput、PS3/PS4、Switch Pro等）转换为对应的协议格式。

