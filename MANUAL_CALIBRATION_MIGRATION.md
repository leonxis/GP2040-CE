# 手动校准功能移植指南

## 修改文件清单

### 后端文件
1. **src/addons/analog.cpp**
   - 修改 `readPin()` 函数：添加手动校准支持
   - 修改 `setup()` 函数：使用手动校准值

2. **src/webconfig.cpp**
   - 添加 `#include "helper.h"`
   - 修改 `getJoystickCenter()` 函数：只读取摇杆1
   - 添加 `getJoystickCenter2()` 函数：读取摇杆2
   - 添加API路由：`/api/getJoystickCenter2`

### 前端文件
3. **www/src/Addons/Analog.tsx**
   - 修改组件参数：添加 `setFieldValue`
   - 修改默认值：`joystickCenterX/Y/X2/Y2: 0`
   - 添加验证schema：手动校准字段验证
   - 修改校准按钮：独立的多步骤校准流程
   - 更新提示信息：明确标注摇杆1/2

### 配置文件
4. **proto/config.proto** (通常已包含)
   - 确认包含手动校准字段：
     - `joystick_center_x`
     - `joystick_center_y` 
     - `joystick_center_x2`
     - `joystick_center_y2`

## 移植步骤

### 1. 更新到目标版本
```bash
git checkout main
git pull origin main
git checkout -b manual-calibration-v0.7.12
```

### 2. 应用修改
按文件清单逐一应用修改，注意：
- 检查API路由表是否变化
- 确认配置字段是否已存在
- 验证前端组件结构是否变化

### 3. 测试验证
- 编译项目
- 测试手动校准功能
- 验证自动校准仍然工作
- 检查配置保存/加载

## 潜在冲突点

### 1. API路由变化
- 检查 `handlerFuncs[]` 数组结构
- 确认路由注册方式

### 2. 配置字段变化
- 检查 `proto/config.proto` 字段定义
- 确认字段编号不冲突

### 3. 前端组件变化
- 检查 `AddonPropTypes` 类型定义
- 确认组件参数结构

## 回退方案
如果移植遇到问题，可以：
1. 保留核心功能（基本手动校准）
2. 简化多步骤校准流程
3. 使用单次校准替代多步骤校准

