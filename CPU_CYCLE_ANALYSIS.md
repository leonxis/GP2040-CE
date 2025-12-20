# Analog Input CPU Cycle Analysis

## 平台信息
- **处理器**: ARM Cortex-M0+ (RP2040)
- **浮点单元**: 有FPU支持
- **时钟频率**: 通常 125-250 MHz

## CPU周期估算基准（ARM Cortex-M0+ with FPU）
- 基本整数运算: 1周期
- 浮点加减: 1-2周期
- 浮点乘除: 1-3周期
- 条件分支: 1-3周期
- 函数调用开销: 2-5周期
- `std::sqrt()`: ~20-30周期
- `std::atan2()`: ~100-200周期（已优化为fastAtan2: ~20-30周期）
- 数组访问: 1-2周期
- 类型转换: 1周期

## process() 函数 - 每个摇杆每次处理的CPU周期消耗

### Step 1: ADC读取和中心偏移 (~10-50周期)
```cpp
cx = readPin(...) - x_center;
cy = readPin(...) - y_center;
```
- `readPin()`: ~5-20周期（ADC读取 + jitter过滤）
- 减法: 1周期
- **总计**: ~10-50周期（取决于jitter过滤是否触发）

### Step 2: 范围校准缩放 (~30-50周期)
```cpp
scale = getInterpolatedScale(i, fastAtan2(cy, cx));
sx = cx / scale;
sy = cy / scale;
```
- `fastAtan2()`: ~20-30周期
- `getInterpolatedScale()`: ~10-20周期（无校准数据时1周期）
- 除法: 2周期 × 2 = 4周期
- **总计**: ~30-50周期（有校准数据），~25-35周期（无校准数据）

### Step 3: 归一化和反转 (~5-10周期)
```cpp
nx = sx / ADC_MAX_HALF;
ny = sy / ADC_MAX_HALF;
// 反转检查（可能执行0-2次）
```
- 除法: 2周期 × 2 = 4周期
- 条件检查: 1-2周期 × 2 = 2-4周期
- 取反: 1周期 × 0-2 = 0-2周期
- **总计**: ~5-10周期

### Step 4: 死区和反死区处理 (~5-50周期)
```cpp
dist_sq = nx * nx + ny * ny;
if (dist_sq < deadzone_sq) {
    // 死区处理: ~3周期
} else if (anti_deadzone > 0.0f) {
    dist = std::sqrt(dist_sq);  // ~20-30周期
    if (dist < baseline) {
        scale_factor = baseline / dist;  // 除法: 2周期
        nx = nx * scale_factor;  // 乘法: 1周期
        ny = ny * scale_factor;  // 乘法: 1周期
    }
}
```
- **死区情况**: ~5周期（2次乘法 + 1次比较 + 赋值）
- **反死区应用**: ~30-35周期（sqrt + 除法 + 2次乘法）
- **反死区启用但未应用**: ~25-30周期（sqrt + 比较）
- **总计**: ~5-50周期（取决于情况）

### Step 5: Square trimming (~5-10周期)
```cpp
nx_before = nx;
ny_before = ny;
nx = std::clamp(nx, -1.0f, 1.0f);
ny = std::clamp(ny, -1.0f, 1.0f);
coords_changed = (nx != nx_before) || (ny != ny_before);
```
- `std::clamp()`: ~2-3周期 × 2 = 4-6周期
- 比较: 1周期 × 2 = 2周期
- **总计**: ~5-10周期

### Step 6: 响应曲线应用 (~10-50周期)
```cpp
if (curve_points_sorted_count > 0) {
    if (coords_changed) {
        magnitude_sq = nx * nx + ny * ny;  // 2周期
    } else {
        // 复用或更新 dist/dist_sq: ~0-5周期
    }
    applyResponseCurveToCoordinates(...);
}
```
- **无曲线**: 0周期
- **有曲线，coords_changed**: ~30-40周期
- **有曲线，!coords_changed，复用dist**: ~20-30周期
- **有曲线，!coords_changed，需要sqrt**: ~40-50周期

### Step 7: 最终转换和输出 (~15-20周期)
```cpp
x_value = nx * 0.5f + ANALOG_CENTER;
y_value = ny * 0.5f + ANALOG_CENTER;
clamped_x = std::clamp(x_value, 0.0f, 1.0f);
clamped_y = std::clamp(y_value, 0.0f, 1.0f);
clampedX = (uint16_t)std::min((uint32_t)(joystickMax * clamped_x), (uint32_t)0xFFFF);
clampedY = (uint16_t)std::min((uint32_t)(joystickMax * clamped_y), (uint32_t)0xFFFF);
```
- 乘法和加法: 2周期 × 2 = 4周期
- `std::clamp()`: 2周期 × 2 = 4周期
- 类型转换和min: 2周期 × 2 = 4周期
- 赋值: 1周期 × 2 = 2周期
- **总计**: ~15-20周期

## 关键函数CPU周期消耗

### fastAtan2() (~20-30周期)
- 条件检查: 1-2周期
- 绝对值计算: 1周期 × 2 = 2周期
- 除法和比较: 2周期
- 多项式计算: ~10-15周期
  - ratio_sq: 1周期
  - ratio_sq_sq: 1周期
  - ratio_sq_cu: 1周期
  - 多项式: 4次乘法 + 3次加法 = ~7周期
- 象限调整: 2-3周期
- **总计**: ~20-30周期

### getInterpolatedScale() (~10-20周期)
- 无校准数据: 1周期（快速返回）
- 有校准数据:
  - 角度归一化: 2周期（加法 + 除法）
  - 索引计算: 1周期（乘法）
  - 类型转换: 1周期
  - 条件检查: 1-2周期
  - 数组访问: 2周期 × 2 = 4周期
  - 线性插值: 3周期（2次乘法 + 1次加法）
  - **总计**: ~10-20周期

### applyResponseCurveToCoordinates() (~15-40周期)
- 快速返回（magnitude_sq <= 0）: 1周期
- 段查找循环: ~5-15周期（取决于段数量，最多4段）
  - 每次迭代: 2次比较 + 1次分支 = ~3周期
- sqrt计算（如果需要）: ~20-30周期
- 曲线计算: 2周期（1次乘法 + 1次加法）
- 缩放应用: 3周期（1次除法 + 2次乘法）
- **总计**: ~15-40周期（取决于是否需要sqrt）

## 总体CPU周期消耗（每个摇杆每次处理）

### 最佳情况（无校准，无死区，无曲线）: ~70-100周期
- Step 1: 10周期
- Step 2: 25周期
- Step 3: 5周期
- Step 4: 5周期
- Step 5: 5周期
- Step 6: 0周期
- Step 7: 15周期
- **总计**: ~65周期

### 典型情况（有校准，有死区，无曲线）: ~120-180周期
- Step 1: 20周期
- Step 2: 40周期
- Step 3: 7周期
- Step 4: 25周期
- Step 5: 7周期
- Step 6: 0周期
- Step 7: 18周期
- **总计**: ~117周期

### 最坏情况（全功能启用）: ~200-300周期
- Step 1: 30周期
- Step 2: 50周期
- Step 3: 10周期
- Step 4: 50周期（反死区应用）
- Step 5: 10周期
- Step 6: 50周期（曲线应用，需要sqrt）
- Step 7: 20周期
- **总计**: ~220周期

## 优化效果总结

### 已实现的优化
1. **fastAtan2**: 节省 ~80-170周期（每次调用）
2. **统一坐标系**: 减少精度损失，避免重复转换
3. **复用sqrt**: 节省 ~20-30周期（当反死区启用时）
4. **条件判断替代取模**: 节省 ~15-30周期（每次调用）
5. **预计算常量**: 节省 ~1-2周期（每次处理）
6. **多项式优化**: 节省 ~2-4周期（每次fastAtan2调用）

### 总优化效果
- **每次处理节省**: ~120-250周期（取决于配置）
- **在125MHz下**: 节省 ~1-2微秒（每个摇杆每次处理）
- **在1000Hz处理频率下**: 节省 ~0.12-0.25ms CPU时间（每个摇杆）

## 性能瓶颈分析

### 主要CPU周期消耗点（按重要性排序）
1. **sqrt计算**: ~20-30周期（如果无法复用）
2. **fastAtan2**: ~20-30周期（已优化）
3. **范围校准插值**: ~10-20周期（有校准数据时）
4. **响应曲线查找**: ~5-15周期（取决于段数量）
5. **ADC读取**: ~5-20周期（取决于jitter过滤）

### 进一步优化建议
1. **考虑使用快速sqrt近似**: 如果精度要求不高，可以使用快速sqrt近似（~5-10周期）
2. **预计算更多数据**: 如果可能，预计算更多查找表
3. **SIMD优化**: 如果支持，可以使用SIMD指令并行处理X和Y坐标

