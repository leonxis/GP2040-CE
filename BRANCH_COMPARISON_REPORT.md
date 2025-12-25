# 分支对比报告：custom-curve vs main

## 概览
- **当前分支**: custom-curve
- **对比分支**: main
- **修改文件总数**: 84 个文件
- **代码变更统计**: +17,661 行新增, -999 行删除
- **提交数量**: 84 个提交

## 主要功能模块

### 1. HML设置页面 (新增)
**位置**: `www/src/Pages/HMLSettings/`

#### 核心文件
- **HMLSettingsPage.jsx** - 主页面容器
- **components/ModeSettings.tsx** (9.4K) - 模式设置（手柄模式选择、Xinput/PS4/键盘配置）
- **components/BackButtonMapping.tsx** (10K) - 背键映射和按键交换
- **components/FunctionButtons.tsx** (1.3K) - 功能按键（热键设置、宏设置导航）
- **components/CalibrationSettings.tsx** (7.3K) - 校准设置容器（Formik上下文管理）
- **components/JoystickCalibration.tsx** (73K) - 摇杆校准UI（中心校准、范围校准、外圈调整）
- **components/JoystickCurveSettings.tsx** (44K) - 摇杆曲线设置（响应曲线编辑器）
- **components/HardwareConfig.tsx** (9.9K) - 硬件配置（USB验证器、显示屏、LED灯条）
- **components/BackupReset.tsx** (7.0K) - 备份重置（重置、保存、导入设置）
- **constants/hmlInputModes.ts** (2.0K) - 常量定义
- **hooks/useGamepadOptions.ts** (2.8K) - 游戏手柄选项Hook
- **hooks/useKeyMappings.ts** (1.2K) - 键盘映射Hook

**功能概述**:
- 新增独立的HML设置页面，包含6个标签页
- 模式设置：支持Xinput、PS4、键盘等8种输入模式
- 背键映射：GPIO引脚映射（GPIO14-25）和按键交换（GPIO2-19）
- 校准设置：摇杆校准和曲线设置功能
- 硬件配置：USB验证器、显示屏、LED灯条控制
- 备份重置：设置备份和恢复功能

### 2. 摇杆曲线设置功能 (新增)
**主要文件**:
- `www/src/Pages/HMLSettings/components/JoystickCurveSettings.tsx` (44K, +1,240行)
- `src/addons/analog.cpp` (修改, +393/-85行)
- `headers/addons/analog.h` (修改, +49/-61行)
- `proto/config.proto` (修改, +40/-6行)

**功能概述**:
- 笛卡尔坐标系曲线编辑器（0-1范围）
- 支持最多3个控制点，形成4段折线
- 死区和反死区可视化
- 曲线启用/禁用开关（折叠/展开功能）
- 前后端一致的曲线应用逻辑
- 严格单调性验证（Y值递增，1%容差）

### 3. 摇杆校准功能增强
**主要文件**:
- `www/src/Pages/HMLSettings/components/JoystickCalibration.tsx` (73K, 新增)
- `www/src/Components/StickCalibrationModal.tsx` (282行, 新增)
- `www/src/Components/RangeCalibrationModal.tsx` (305行, 新增)

**功能概述**:
- 中心校准（X/Y轴独立调整）
- 范围校准（48个角度位置的外圈校准）
- 外圈形状调整（强制圆形、放大系数）
- 死区和反死区设置
- 抖动过滤配置
- 实时摇杆位置可视化

### 4. 后端摇杆处理优化
**主要文件**:
- `src/addons/analog.cpp` (+393/-85行)
- `src/addons/analog_utils.cpp` (70行, 新增)
- `headers/addons/analog_utils.h` (38行, 新增)

**主要改进**:
- 曲线应用逻辑优化（仅在反死区范围之后应用）
- 使用 `std::atan2` 替代 `fastAtan2`
- 性能优化：预计算常量、优化clamp操作
- 曲线启用开关控制（`joystick_curve_enabled`）
- 曲线段预处理和快速查找

### 5. 显示界面增强
**主要文件**:
- `src/display/ui/screens/StickCalibrationScreen.cpp` (287行, 新增)
- `src/display/ui/screens/AnalogDeadzoneScreen.cpp` (325行, 新增)
- `src/display/ui/screens/BackStickMappingScreen.cpp` (559行, 新增)
- `src/display/ui/screens/APMTestScreen.cpp` (210行, 新增)

**功能概述**:
- 摇杆校准屏幕（OLED显示）
- 死区调整屏幕
- 背键映射屏幕
- APM测试屏幕

### 6. 配置和协议更新
**主要文件**:
- `proto/config.proto` (+40/-6行) - 添加曲线点、曲线启用开关等字段
- `src/webconfig.cpp` (+294行修改) - API端点更新
- `src/config_utils.cpp` (+98/-12行) - 配置初始化更新
- `configs/HML/BoardConfig.h` (288行, 新增) - HML板级配置

### 7. 国际化支持
**修改文件**: 多个语言文件
- `www/src/Locales/zh-CN/AddonsConfig.jsx` (+94行修改)
- `www/src/Locales/en/AddonsConfig.jsx` (+95行修改)
- 其他语言文件（de-DE, es-MX, ja-JP, ko-KR, pt-BR）

## 文件大小统计（主要修改文件）

### 大型新增文件 (>10K)
1. `www/src/Pages/HMLSettings/components/JoystickCalibration.tsx` - 73K
2. `www/src/Pages/HMLSettings/components/JoystickCurveSettings.tsx` - 44K
3. `www/src/Pages/HMLSettings/components/BackButtonMapping.tsx` - 10K
4. `www/src/Pages/HMLSettings/components/HardwareConfig.tsx` - 9.9K
5. `www/src/Pages/HMLSettings/components/ModeSettings.tsx` - 9.4K

### 大型修改文件
1. `src/addons/analog.cpp` - +393/-85行
2. `src/webconfig.cpp` - +294行修改
3. `www/src/Addons/Analog.tsx` - +333行修改
4. `src/display/ui/screens/BackStickMappingScreen.cpp` - 559行新增
5. `src/display/ui/elements/GPMenu.cpp` - +303行修改

## 文档文件（新增）

### 技术文档 (docs/)
- `OUTER_CIRCLE_CALIBRATION_DESIGN.md` (791行) - 外圈校准设计文档
- `UNIFIED_NORMALIZATION_PROPOSAL.md` (583行) - 统一归一化提案
- `RANGE_CALIBRATION_COMPLETE_ANALYSIS.md` (629行) - 范围校准完整分析
- `CORRECTED_FINAL_VALUE_CALCULATION.md` (498行) - 修正后的最终值计算
- `DETAILED_UNIFIED_NORMALIZATION_FLOW.md` (511行) - 详细统一归一化流程
- 以及其他20+个技术分析文档

### 分析文档
- `CPU_CYCLE_ANALYSIS.md` (208行) - CPU周期分析
- `SCALE_ADJUSTMENT_LOGIC.md` (136行) - 缩放调整逻辑

## 删除的文件
- `docs/ddi-socd.md` (119行)
- `configs/FlatboxRev8/FlatboxRev8.cmake`
- `configs/Pico2/Pico2.cmake`
- `configs/PicoW/PicoW.cmake`
- `configs/SparkFunProMicroRP2350/SparkFunProMicroRP2350.cmake`

## 主要技术改进

### 1. 摇杆处理算法
- 统一归一化流程
- 范围校准数据应用
- 外圈形状调整
- 响应曲线应用
- 抖动过滤

### 2. 前端架构
- 模块化组件设计
- 自定义Hooks（useGamepadOptions, useKeyMappings）
- Formik表单管理
- Canvas 2D图形绘制
- 实时数据可视化

### 3. 后端优化
- 性能优化（预计算、常量优化）
- 代码重构（工具函数提取）
- 配置管理改进
- API端点扩展

## 代码质量改进
- 移除未使用的变量和导入
- 修复编译警告
- 代码清理和重构
- 类型安全改进（TypeScript）
- 错误处理增强

## 总结

本分支主要实现了以下核心功能：
1. **HML设置页面** - 全新的设置界面，包含6个功能标签页
2. **摇杆曲线设置** - 响应曲线编辑器，支持自定义摇杆响应
3. **摇杆校准增强** - 完整的校准工具集（中心、范围、外圈、死区）
4. **后端算法优化** - 摇杆处理逻辑的全面改进和性能优化
5. **显示界面增强** - OLED屏幕上的校准和配置界面

**代码规模**: 新增约17,661行，删除999行，净增约16,662行代码。

## 固件体积增加分析（约700KB）

### 主要原因

#### 1. 前端代码打包（最主要，约400-500KB）
- **www/ 目录会被编译打包进固件**
  - React应用通过 `makefsdata.js` 编译后嵌入到 `lib/httpd/fsdata.c`
  - 新增的HMLSettings页面和相关组件
  - `JoystickCalibration.tsx`: 3,655行源代码
  - `JoystickCurveSettings.tsx`: ~1,240行源代码
  - 其他HMLSettings组件: ~2,000行源代码
  - **编译后的JavaScript代码（压缩后）**: 约400-500KB

#### 2. C++代码编译（约150-250KB）
- **新增的显示屏幕文件**:
  - `BackStickMappingScreen.cpp`: 22K源代码 → 编译后 ~60-80KB
  - `AnalogDeadzoneScreen.cpp`: 12K源代码 → 编译后 ~30-40KB
  - `StickCalibrationScreen.cpp`: 9.0K源代码 → 编译后 ~25-35KB
  - `APMTestScreen.cpp`: 6.7K源代码 → 编译后 ~15-20KB
- **修改的C++文件**:
  - `analog.cpp`: +393行 → 编译后 ~50-80KB
  - `webconfig.cpp`: +294行 → 编译后 ~30-50KB

#### 3. 数据结构增加（约0.64KB）
- `range_data[48]` (每个摇杆): 192字节 × 2 = 384字节
- `curve_points_sorted[5]` (每个摇杆): 40字节 × 2 = 80字节
- `curve_segments[4]` (每个摇杆): 96字节 × 2 = 192字节
- **总计**: 656字节 ≈ 0.64KB

#### 4. 标准库链接（约20-50KB）
- `std::atan2` (替代 `fastAtan2`) - 数学库函数
- `std::clamp` - C++17标准库
- `std::sqrt` - 数学库函数
- `std::algorithm` - 算法库

#### 5. 字符串资源（约10-30KB）
- 国际化字符串（多语言支持）
- UI文本和提示信息
- 错误消息

### 体积估算
| 类别 | 估算大小 |
|------|---------|
| 前端代码（压缩后） | 400-500KB |
| C++代码编译 | 150-250KB |
| 标准库链接 | 20-50KB |
| 字符串资源 | 10-30KB |
| 数据结构 | 0.64KB |
| **总计** | **~580-830KB** |

### 说明
- **源代码行数与编译后二进制大小不是线性关系**
- React/TypeScript代码编译后会生成大量JavaScript
- JavaScript代码会被压缩后嵌入固件，但仍占用相当空间
- C++代码编译后通常比源代码大2-5倍（取决于优化级别）
- 链接标准库函数会增加二进制大小

