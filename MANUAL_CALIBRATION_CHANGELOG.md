# 手动校准功能修改记录

## 功能概述
为GP2040-CE项目添加了独立的摇杆手动校准功能，支持摇杆1和摇杆2的独立校准，包括多步骤校准流程以提高精度。

## 修改文件清单

### 核心功能文件
1. **src/addons/analog.cpp** - 模拟摇杆核心逻辑
2. **src/webconfig.cpp** - Web配置API
3. **www/src/Addons/Analog.tsx** - 前端界面

### 配置文件
4. **proto/config.proto** - 配置协议定义
5. **configs/Pico/BoardConfig.h** - Pico板配置

### 本地化文件
6. **www/src/Locales/en/AddonsConfig.jsx** - 英文本地化
7. **www/src/Locales/zh-CN/AddonsConfig.jsx** - 中文本地化
8. **www/src/Locales/zh-CN/LayoutConfig.jsx** - 中文布局配置

### 头文件
9. **headers/addons/analog.h** - 模拟摇杆头文件
10. **headers/buttonlayouts.h** - 按钮布局头文件
11. **headers/drivers/ps4/PS4Descriptors.h** - PS4驱动描述符

## 详细修改内容

### 1. src/addons/analog.cpp

#### 修改内容：
- **readPin()函数**: 修改校准判断逻辑，支持手动校准
  ```cpp
  // 修改前
  if (adc_pairs[stick_num].auto_calibration) {
  
  // 修改后
  if (adc_pairs[stick_num].auto_calibration || center != 0) {
  ```

- **setup()函数**: 添加手动校准值的使用逻辑
  ```cpp
  // 添加手动校准值支持
  adc_pairs[i].joystick_center_x = analogOptions.joystick_center_x;
  adc_pairs[i].joystick_center_y = analogOptions.joystick_center_y;
  ```

#### 功能改进：
- 支持手动校准和自动校准的混合使用
- 手动校准值持久化保存
- 校准逻辑更加智能

### 2. src/webconfig.cpp

#### 修改内容：
- **添加头文件**: `#include "helper.h"`
- **修改getJoystickCenter()**: 只读取摇杆1的中心值
- **新增getJoystickCenter2()**: 专门读取摇杆2的中心值
- **添加API路由**: `/api/getJoystickCenter2`

#### API功能：
```cpp
// 摇杆1校准API
std::string getJoystickCenter() {
    // 读取摇杆1的ADC值
    // 返回: {"success": true, "x": 2048, "y": 1956}
}

// 摇杆2校准API  
std::string getJoystickCenter2() {
    // 读取摇杆2的ADC值
    // 返回: {"success": true, "x": 2048, "y": 1956}
}
```

### 3. www/src/Addons/Analog.tsx

#### 修改内容：
- **组件参数**: 添加`setFieldValue`参数
- **默认值**: 手动校准值改为0（原来是2048）
- **验证schema**: 添加手动校准字段验证
- **校准按钮**: 实现独立的多步骤校准流程
- **提示信息**: 明确标注摇杆1/2

#### 多步骤校准流程：
1. 左上方向回中 → 记录中心值1
2. 右上方向回中 → 记录中心值2  
3. 左下方向回中 → 记录中心值3
4. 右下方向回中 → 记录中心值4
5. 计算四个点的平均值作为最终中心值

#### 前端功能：
```typescript
// 摇杆1独立校准
const calibrateStick1 = async () => {
    // 4步骤校准流程
    // 只更新 joystickCenterX, joystickCenterY
}

// 摇杆2独立校准  
const calibrateStick2 = async () => {
    // 4步骤校准流程
    // 只更新 joystickCenterX2, joystickCenterY2
}
```

### 4. proto/config.proto

#### 添加字段：
```protobuf
optional uint32 joystick_center_x = 24;
optional uint32 joystick_center_y = 25;
optional uint32 joystick_center_x2 = 26;
optional uint32 joystick_center_y2 = 27;
```

### 5. 本地化文件

#### 添加翻译：
- **英文**: `'analog-calibrate-button': 'Calibrate'`
- **中文**: `'analog-calibrate-button': '手动校准'`

## 功能特性

### ✅ 独立校准
- 摇杆1和摇杆2可以独立进行手动校准
- 每个摇杆有自己的校准按钮和中心值显示
- 互不干扰，可以分别优化

### ✅ 多步骤校准
- 4步骤校准流程：左上、右上、左下、右下
- 计算四个回中值的平均值
- 比单次测量更准确

### ✅ 智能校准逻辑
- 自动校准优先于手动校准
- 手动校准值持久化保存
- 支持混合模式使用

### ✅ 用户友好界面
- 清晰的步骤提示
- 详细的校准结果显示
- 可以随时取消校准
- 明确的状态指示

## 技术优势

### 1. 更高的精度
- 通过四个方向的回中值计算平均值
- 减少了单次测量的误差
- 更准确地反映摇杆的真实中心位置

### 2. 更好的用户体验
- 分步骤指导，用户不会感到困惑
- 可以随时取消，不会强制完成
- 详细的反馈信息，用户了解校准过程

### 3. 向后兼容
- 不影响现有的自动校准功能
- 手动校准字段有默认值
- 可以独立启用/禁用

## 测试验证

### 测试项目：
- ✅ 摇杆1独立手动校准
- ✅ 摇杆2独立手动校准
- ✅ 多步骤校准流程
- ✅ 自动校准功能正常
- ✅ 配置保存/加载
- ✅ 设备重启后校准值保持
- ✅ 混合模式（一个自动，一个手动）

### 测试结果：
- 功能运行正常
- 校准精度显著提高
- 用户体验良好
- 无兼容性问题

## 版本信息
- **基础版本**: GP2040-CE 0.7.12-beta3
- **修改日期**: 2024年
- **功能状态**: 已完成并测试通过
- **移植状态**: 准备就绪

## 移植准备
- ✅ 修改记录完整
- ✅ 功能测试通过
- ✅ 文档齐全
- ✅ 备份策略准备
- ✅ 移植脚本准备
