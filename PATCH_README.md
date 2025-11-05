# Back Stick Mapping Feature Patch

## 功能说明

此patch为GP2040-CE项目添加了在MINILED显示屏上配置左右背键（GPIO22和GPIO25）按键映射的功能。

## 修改的文件

1. `headers/display/ui/screens/MainMenuScreen.h` - 添加菜单项和函数声明
2. `src/display/ui/screens/MainMenuScreen.cpp` - 实现菜单逻辑和映射功能

## 使用方法

### 应用patch

```bash
# 在项目根目录下执行
git apply back-stick-mapping-feature.patch

# 或者使用patch命令
patch -p1 < back-stick-mapping-feature.patch
```

### 如果遇到冲突

如果patch应用时出现冲突，可以手动合并：

1. 查看冲突标记
2. 根据patch文件中的修改手动合并代码
3. 确保所有新增的函数都已实现

## 功能特点

- 三级菜单结构：主菜单 → Back stick → Left stick/Right stick → 按键映射列表
- 当前选中的映射会显示 "*" 标记
- 按键列表使用大写字母，确保良好对齐
- 退出时如果有更改会提示 "Reboot to apply settings"
- 按B1确认重启，按B2取消并停留在选择界面

## 操作流程

1. 进入MINILED主菜单
2. 选择 "Back stick"
3. 选择 "Left stick" 或 "Right stick"
4. 在按键列表中选择要映射的按键（当前选择会显示 "*"）
5. 按B1确认，自动返回Left/Right stick选择界面
6. 按B2退出时，如果有更改会提示重启
7. 按B1确认重启应用设置，或按B2取消

## 注意事项

- GPIO映射更改需要重启设备才能生效
- 修改会保存到GPIO配置中
- 如果取消重启提示，更改会保留但不会应用，需要重新进入菜单才能应用

