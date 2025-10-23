# 手动校准功能移植指南

## 当前状态
- **功能状态**: ✅ 已完成并测试通过
- **备份分支**: `manual-calibration-feature-backup`
- **开发分支**: `manual-calibration-feature`
- **补丁文件**: `manual_calibration_complete.patch`

## 移植到新版本的步骤

### 1. 准备工作
```bash
# 切换到主分支
git checkout main

# 更新到目标版本
git pull origin main

# 创建移植分支
git checkout -b manual-calibration-v0.7.12
```

### 2. 应用修改
```bash
# 方法1: 使用补丁文件
git apply manual_calibration_complete.patch

# 方法2: 手动合并（推荐）
git cherry-pick manual-calibration-feature-backup
```

### 3. 验证修改
```bash
# 检查修改的文件
git status

# 编译项目
mkdir build && cd build
cmake ..
make -j4
```

### 4. 测试功能
- ✅ 摇杆1独立手动校准
- ✅ 摇杆2独立手动校准  
- ✅ 多步骤校准流程
- ✅ 自动校准功能正常
- ✅ 配置保存/加载

## 关键修改点

### 后端修改
1. **src/addons/analog.cpp**
   - `readPin()`: 修改校准判断逻辑
   - `setup()`: 添加手动校准值支持

2. **src/webconfig.cpp**
   - 添加 `#include "helper.h"`
   - 修改 `getJoystickCenter()`: 只读取摇杆1
   - 添加 `getJoystickCenter2()`: 读取摇杆2
   - 添加API路由

### 前端修改
3. **www/src/Addons/Analog.tsx**
   - 添加 `setFieldValue` 参数
   - 修改默认值: `joystickCenterX/Y/X2/Y2: 0`
   - 实现多步骤校准流程
   - 更新提示信息

### 配置文件
4. **proto/config.proto**
   - 确认包含手动校准字段

## 潜在冲突点

### 1. API路由变化
检查 `src/webconfig.cpp` 中的 `handlerFuncs[]` 数组：
```cpp
static const std::pair<const char*, HandlerFuncPtr> handlerFuncs[] = {
    // ... 现有路由 ...
    { "/api/getJoystickCenter", getJoystickCenter },
    { "/api/getJoystickCenter2", getJoystickCenter2 },  // 新增
    // ... 其他路由 ...
};
```

### 2. 配置字段冲突
检查 `proto/config.proto` 中的字段编号：
```protobuf
optional uint32 joystick_center_x = 24;   // 确认编号不冲突
optional uint32 joystick_center_y = 25;
optional uint32 joystick_center_x2 = 26;
optional uint32 joystick_center_y2 = 27;
```

### 3. 前端组件变化
检查 `www/src/Pages/AddonsConfigPage.tsx` 中的 `AddonPropTypes`：
```typescript
export type AddonPropTypes = {
    values: typeof DEFAULT_VALUES;
    errors: FormikErrors<typeof DEFAULT_VALUES>;
    handleChange: FormikHandlers['handleChange'];
    handleCheckbox: (name: keyof typeof DEFAULT_VALUES) => void;
    setFieldValue: FormikHelpers<typeof DEFAULT_VALUES>['setFieldValue']; // 确认存在
};
```

## 回退方案

如果移植遇到问题，可以使用简化版本：

### 简化版功能
1. **单次校准**: 不使用多步骤，直接读取当前值
2. **基本API**: 只保留一个API端点
3. **简单界面**: 移除复杂的步骤提示

### 简化版修改
```typescript
// 简化的校准函数
const simpleCalibrate = async () => {
    const res = await fetch('/api/getJoystickCenter');
    const data = await res.json();
    setFieldValue('joystickCenterX', data.x);
    setFieldValue('joystickCenterY', data.y);
    alert(`校准完成: X=${data.x}, Y=${data.y}`);
};
```

## 测试清单

### 功能测试
- [ ] 摇杆1手动校准
- [ ] 摇杆2手动校准
- [ ] 多步骤校准流程
- [ ] 校准值计算正确
- [ ] 配置保存/加载
- [ ] 设备重启后保持

### 兼容性测试
- [ ] 自动校准功能正常
- [ ] 现有配置不受影响
- [ ] 其他addon功能正常
- [ ] Web界面无错误

### 性能测试
- [ ] 校准响应速度
- [ ] 内存使用正常
- [ ] 编译无警告

## 成功标准
- ✅ 所有功能测试通过
- ✅ 无编译错误或警告
- ✅ 用户体验良好
- ✅ 向后兼容

## 联系信息
- **开发者**: Leonxis
- **功能版本**: v1.0
- **测试状态**: 通过
- **移植状态**: 准备就绪
