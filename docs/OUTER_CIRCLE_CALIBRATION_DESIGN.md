# 手动外圈校准功能设计文档

## 1. 概述

### 1.1 目标
在现有中心点手动校准的基础上，增加手动外圈校准功能，允许用户校准摇杆在各个方向的最大行程，同时保持各个方向的线性度。

### 1.2 当前系统
- **中心点校准**：用户在4个方向（左上、右上、左下、右下）推到极限，计算中心点
- **外圈处理**：通过 `outer_deadzone`（百分比）和 `forced_circularity`（强制外圆）控制
- **线性度**：`readPin()` 函数使用线性映射从中心点到ADC_MAX/ADC_MIN

### 1.3 问题
- 不同方向的物理最大行程可能不同（非圆形）
- 统一的 `outer_deadzone` 百分比无法精确匹配每个方向的实际最大值
- `forced_circularity` 会强制限制所有方向，可能丢失某些方向的精度

## 2. 设计方案

### 2.1 核心思路
1. **36方向采样**：每10度一个样本（0°-350°，共36个方向），提高外圈精度
2. **自动采样**：用户将摇杆推到极限后旋转，系统自动检测并采样各个方向的最大值
3. **存储结构**：为每个摇杆存储36个方向的ADC最大值（相对于中心点的距离）
4. **运行时应用**：根据当前摇杆方向，直接查找或插值计算该方向的外圈限制
5. **功能互斥**：强制圆形开关打开时使用原有功能，关闭时可启用手动外圈校准（此时 `outer_deadzone` 和 `forced_circularity` 参数失效）
6. **保持线性度**：只修改外圈限制计算，不影响中心到外圈的线性映射
7. **固定4圈采样**：要求用户旋转4圈，确保数据充分覆盖和可靠性
8. **95%填充检测**：每圈检测是否95%的方向都有超过95%的值，判断一圈完成
9. **历史最大值累积**：持续累积所有圈的最大值，不清空历史数据
10. **超时机制**：30秒后允许手动完成，但会进行质量检查

### 2.2 数据结构设计

#### 2.2.1 Protobuf 结构
```protobuf
message AnalogOptions {
    // ... 现有字段 ...
    
    // 外圈校准数据（36个方向，每10度一个样本，相对于中心点的ADC距离）
    // 方向顺序：0°, 10°, 20°, ..., 350° (共36个方向)
    // 使用 repeated 数组存储，索引对应角度：index = angle / 10
    repeated uint32 outer_calib_data = 50 [max_count = 36];  // Stick 1 的36个方向数据
    repeated uint32 outer_calib_data2 = 51 [max_count = 36]; // Stick 2 的36个方向数据
    
    // 外圈校准使能标志
    optional bool outer_calib_enabled = 52;
    optional bool outer_calib_enabled2 = 53;
}
```

**注意**：如果 protobuf 不支持 `repeated` 或需要固定大小，可以使用固定36个字段：
```protobuf
// 备选方案：固定36个字段（如果repeated不支持）
optional uint32 outer_calib_0 = 50;   // 0°
optional uint32 outer_calib_10 = 51;  // 10°
// ... 共36个字段 ...
optional uint32 outer_calib_350 = 85; // 350°
```

#### 2.2.2 运行时数据结构
```cpp
// 在 adc_instance 中添加
#define OUTER_CALIB_SAMPLES 36  // 36个方向，每10度一个

struct {
    uint32_t outer_max[OUTER_CALIB_SAMPLES];  // 36个方向的最大ADC距离
    bool outer_calib_enabled;                  // 是否启用外圈校准
} outer_calibration;

// 校准过程中的临时数据结构（不需要持久化，仅在校准过程中使用）
struct OuterCalibrationState {
    // 历史最大值（累积所有圈的最大值，不清空）
    uint32_t historical_max[OUTER_CALIB_SAMPLES];  // 所有圈累积的最大值
    
    // 当前圈的数据（临时存储，用于一圈完成检测）
    uint32_t current_circle_max[OUTER_CALIB_SAMPLES];  // 当前圈的最大值
    
    // 旋转检测
    float last_angle;                         // 上一次的角度（用于检测旋转）
    int rotation_accumulator;                 // 角度累积（用于检测完整360度旋转）
    uint8_t rotation_count;                   // 已完成的旋转圈数
    
    // 超时检测
    uint32_t start_time_ms;                   // 校准开始时间（毫秒）
    
    // 状态标志
    bool initialized;                         // 是否已初始化
};
```

### 2.3 校准流程设计

#### 2.3.1 UI流程
1. **中心点校准**（现有）：4个方向采样 → 计算中心点
2. **中心点微调**（新增，Web页面）：
   - 在Web配置页面提供"中心点微调"按钮（常态显示）
   - 进入微调界面，可以对已校准的中心点X、Y值进行增大/减小设置
   - 仅使用鼠标点击加减按钮进行操作（单次点击，无连续操作）
   - 实时显示当前中心点X、Y数值
3. **外圈校准**（新增）：
   - 提示用户将摇杆推到极限位置
   - 用户保持推到极限并缓慢旋转摇杆（需要旋转4圈）
   - 系统自动检测并采样36个方向（每10度）的最大ADC距离
   - **多圈优化采样**：允许用户多次经过同一方向，系统持续更新每个方向的最大值
   - **历史最大值累积**：所有圈的最大值持续累积，确保不丢失任何方向的最大值
   - 实时显示状态提示（简化显示）：
     - 状态提示（继续旋转/可以完成/校准完成）
   - 完成条件：
     - **固定4圈完成**：旋转4圈后自动完成
     - 或**超时完成**：30秒后允许手动完成（但会进行质量检查）
   - 达到完成条件后自动提示用户停止
4. **外圈微调**（新增，Web页面）：
   - 在Web配置页面提供"外圈微调"按钮（常态显示）
   - 进入微调界面，可以对摇杆8个方向的外圈限制进行微调
   - 8个方向：左上、上、右上、左、右、左下、下、右下
   - 仅使用鼠标点击加减按钮进行操作（单次点击，无连续操作）
   - 实时显示当前各方向的数值

#### 2.3.2 校准状态机
```cpp
enum CalibrationState {
    // 中心点校准（现有）
    STATE_STICK1_TOP_LEFT,
    STATE_STICK1_TOP_RIGHT,
    STATE_STICK1_BOTTOM_LEFT,
    STATE_STICK1_BOTTOM_RIGHT,
    STATE_STICK2_TOP_LEFT,
    STATE_STICK2_TOP_RIGHT,
    STATE_STICK2_BOTTOM_LEFT,
    STATE_STICK2_BOTTOM_RIGHT,
    
    // 中心点微调（新增）
    STATE_STICK1_CENTER_FINETUNE,  // Stick 1 中心点微调
    STATE_STICK2_CENTER_FINETUNE,  // Stick 2 中心点微调
    
    // 外圈校准（新增）
    STATE_STICK1_OUTER_CALIB,  // Stick 1 外圈校准进行中
    STATE_STICK2_OUTER_CALIB,  // Stick 2 外圈校准进行中
    
    // 外圈微调（新增）
    STATE_STICK1_OUTER_FINETUNE,  // Stick 1 外圈微调
    STATE_STICK2_OUTER_FINETUNE,  // Stick 2 外圈微调
    
    STATE_COMPLETE
};
```

### 2.4 采样函数设计

#### 2.4.1 自动采样策略（固定4圈 + 95%填充检测）
```cpp
#define OUTER_CALIB_SAMPLES 36
#define OUTER_CALIB_ANGLE_STEP 10.0f  // 每10度一个样本
#define REQUIRED_ROTATION_CIRCLES 4  // 固定要求4圈
#define CIRCLE_FILL_THRESHOLD 0.95f  // 一圈完成判断：95%填充
#define JOYSTICK_EXTREME_THRESHOLD 0.95f  // 归一化95%阈值，判断是否推到极限
#define TIMEOUT_SECONDS 30  // 超时时间：30秒
#define MIN_SAMPLED_DIRECTIONS_RATIO 0.8f  // 质量检查：至少80%方向已采样

/**
 * 计算归一化阈值（基于理论最大值）
 */
uint32_t calculateNormalizedThreshold(uint16_t centerX, uint16_t centerY) {
    // 计算理论最大距离（从中心点到ADC边界的最大距离）
    uint16_t center = ADC_MAX / 2;  // 假设中心在ADC中心（也可以基于实际中心点）
    uint32_t maxTheoreticalDistance = sqrt(
        (ADC_MAX - center) * (ADC_MAX - center) * 2
    );
    
    // 归一化95%阈值
    return (uint32_t)(maxTheoreticalDistance * JOYSTICK_EXTREME_THRESHOLD);
}

/**
 * 检查一圈是否完成（95%填充检测）
 */
bool checkCircleComplete(uint32_t current_circle_max[OUTER_CALIB_SAMPLES], uint32_t threshold) {
    // 统计超过阈值的方向数
    int extreme_count = 0;
    for (int i = 0; i < OUTER_CALIB_SAMPLES; i++) {
        if (current_circle_max[i] >= threshold) {
            extreme_count++;
        }
    }
    
    // 计算填充比例
    float fill_ratio = (float)extreme_count / OUTER_CALIB_SAMPLES;
    
    // 95%填充则认为一圈完成
    return fill_ratio >= CIRCLE_FILL_THRESHOLD;
}

/**
 * 检查是否满足完成条件
 */
bool isCalibrationComplete(OuterCalibrationState& state, uint32_t historical_max[OUTER_CALIB_SAMPLES]) {
    // 条件1：固定4圈完成
    if (state.rotation_count >= REQUIRED_ROTATION_CIRCLES) {
        return true;
    }
    
    return false;
}

/**
 * 检查质量（用于超时完成）
 */
bool checkCalibrationQuality(OuterCalibrationState& state, uint32_t historical_max[OUTER_CALIB_SAMPLES]) {
    // 检查至少1圈
    if (state.rotation_count < 1) {
        return false;
    }
    
    // 检查至少80%方向已采样
    int sampled_count = 0;
    for (int i = 0; i < OUTER_CALIB_SAMPLES; i++) {
        if (historical_max[i] > 0) {
            sampled_count++;
        }
    }
    
    float sampled_ratio = (float)sampled_count / OUTER_CALIB_SAMPLES;
    return sampled_ratio >= MIN_SAMPLED_DIRECTIONS_RATIO;
}

/**
 * 自动采样外圈校准数据（固定4圈 + 95%填充检测）
 * @param stickNum 摇杆编号 (0或1)
 * @param centerX 中心点X坐标
 * @param centerY 中心点Y坐标
 * @param calibration_state 校准状态结构（需要外部维护）
 * @param outer_max 输出：36个方向的最大ADC距离数组（最终结果）
 * @param current_time_ms 当前时间（毫秒）
 * @return 采样是否完成（true=完成，false=继续采样）
 */
bool autoSampleOuterCalibration(uint8_t stickNum, 
                                 uint16_t centerX, uint16_t centerY,
                                 OuterCalibrationState& calibration_state,
                                 uint32_t outer_max[OUTER_CALIB_SAMPLES],
                                 uint32_t current_time_ms) {
    uint16_t x, y;
    
    // 初始化（仅在第一次调用时）
    if (!calibration_state.initialized) {
        // 初始化历史最大值（累积所有圈的最大值）
        memset(calibration_state.historical_max, 0, sizeof(calibration_state.historical_max));
        
        // 初始化当前圈数据
        memset(calibration_state.current_circle_max, 0, sizeof(calibration_state.current_circle_max));
        
        // 初始化旋转检测
        calibration_state.last_angle = -1.0f;
        calibration_state.rotation_accumulator = 0;
        calibration_state.rotation_count = 0;
        
        // 记录开始时间
        calibration_state.start_time_ms = current_time_ms;
        
        calibration_state.initialized = true;
    }
    
    // 检查超时（30秒）
    uint32_t elapsed_time_ms = current_time_ms - calibration_state.start_time_ms;
    bool is_timeout = (elapsed_time_ms >= TIMEOUT_SECONDS * 1000);
    
    // 读取ADC值
    readJoystickADC(stickNum, x, y);
    
    // 计算相对于中心点的距离和角度
    int32_t dx = (int32_t)x - (int32_t)centerX;
    int32_t dy = (int32_t)y - (int32_t)centerY;
    uint32_t distance = sqrt(dx*dx + dy*dy);
    
    // 计算归一化阈值
    uint32_t threshold = calculateNormalizedThreshold(centerX, centerY);
    
    // 判断是否推到极限（归一化95%阈值）
    if (distance >= threshold) {
        // 计算角度（0-360度）
        float angle = atan2(dy, dx) * 180.0f / M_PI;
        if (angle < 0) angle += 360.0f;
        
        // 检测完整旋转（跨越0度边界）
        if (calibration_state.last_angle >= 0.0f) {
            float angle_diff = angle - calibration_state.last_angle;
            // 处理角度跨越0度的情况
            if (angle_diff > 180.0f) angle_diff -= 360.0f;
            if (angle_diff < -180.0f) angle_diff += 360.0f;
            calibration_state.rotation_accumulator += (int)(angle_diff * 10.0f);
            
            // 检测是否完成一圈（360度 = 3600）
            if (calibration_state.rotation_accumulator >= 3600) {
                // 检查当前圈是否完成（95%填充）
                if (checkCircleComplete(calibration_state.current_circle_max, threshold)) {
                    calibration_state.rotation_count++;
                    
                    // 合并当前圈到历史最大值（取最大值）
                    for (int i = 0; i < OUTER_CALIB_SAMPLES; i++) {
                        if (calibration_state.current_circle_max[i] > calibration_state.historical_max[i]) {
                            calibration_state.historical_max[i] = calibration_state.current_circle_max[i];
                        }
                    }
                    
                    // 清空当前圈数据，准备下一圈
                    memset(calibration_state.current_circle_max, 0, sizeof(calibration_state.current_circle_max));
                }
                
                calibration_state.rotation_accumulator = 0;
            } else if (calibration_state.rotation_accumulator <= -3600) {
                // 反向旋转处理
                calibration_state.rotation_accumulator += 3600;
            }
        }
        
        calibration_state.last_angle = angle;
        
        // 计算对应的采样索引（每10度一个）
        int index = (int)(angle / OUTER_CALIB_ANGLE_STEP) % OUTER_CALIB_SAMPLES;
        
        // 更新历史最大值（累积所有圈的最大值）
        if (distance > calibration_state.historical_max[index]) {
            calibration_state.historical_max[index] = distance;
        }
        
        // 更新当前圈最大值
        if (distance > calibration_state.current_circle_max[index]) {
            calibration_state.current_circle_max[index] = distance;
        }
    }
    
    // 检查完成条件
    if (isCalibrationComplete(calibration_state, calibration_state.historical_max)) {
        // 将历史最大值复制到输出数组
        memcpy(outer_max, calibration_state.historical_max, sizeof(calibration_state.historical_max));
        return true;  // 固定4圈完成
    }
    
    // 超时检查（允许手动完成，但检查质量）
    if (is_timeout && checkCalibrationQuality(calibration_state, calibration_state.historical_max)) {
        // 质量检查通过，可以完成（但会显示警告）
        memcpy(outer_max, calibration_state.historical_max, sizeof(calibration_state.historical_max));
        return true;  // 超时完成
    }
    
    return false;  // 继续采样
}

/**
 * 重置校准状态（开始新的校准时调用）
 */
void resetOuterCalibrationState(OuterCalibrationState& state) {
    state.initialized = false;
}
```

#### 2.4.2 方向索引映射
```cpp
// 角度到索引的转换
inline int angleToIndex(float angle) {
    while (angle < 0) angle += 360.0f;
    while (angle >= 360) angle -= 360.0f;
    return (int)(angle / OUTER_CALIB_ANGLE_STEP) % OUTER_CALIB_SAMPLES;
}

// 索引到角度的转换
inline float indexToAngle(int index) {
    return index * OUTER_CALIB_ANGLE_STEP;
}
```

### 2.5 插值函数设计

#### 2.5.1 角度计算
```cpp
/**
 * 计算当前摇杆方向的角度（0-360度）
 * @param x_magnitude X方向偏移量（相对于中心）
 * @param y_magnitude Y方向偏移量（相对于中心）
 * @return 角度（0-360度）
 */
float calculateAngle(float x_magnitude, float y_magnitude) {
    float angle = atan2(y_magnitude, x_magnitude) * 180.0f / M_PI;
    if (angle < 0) angle += 360.0f;
    return angle;
}
```

#### 2.5.2 查找或插值
```cpp
/**
 * 根据当前角度获取外圈限制
 * @param angle 当前角度（0-360度）
 * @param outer_max 36个方向的最大值数组
 * @return 该角度的外圈限制（ADC距离）
 */
float getOuterLimit(float angle, const uint32_t outer_max[OUTER_CALIB_SAMPLES]) {
    // 将角度归一化到0-360
    while (angle < 0) angle += 360.0f;
    while (angle >= 360) angle -= 360.0f;
    
    // 计算对应的索引（每10度一个）
    float index_f = angle / OUTER_CALIB_ANGLE_STEP;
    int index = (int)index_f % OUTER_CALIB_SAMPLES;
    float fraction = index_f - index;  // 小数部分，用于插值
    
    // 如果正好是采样点，直接返回
    if (fraction < 0.01f) {
        return outer_max[index];
    }
    
    // 否则在相邻两个采样点之间插值
    int index1 = index;
    int index2 = (index + 1) % OUTER_CALIB_SAMPLES;
    
    // 线性插值
    float limit = outer_max[index1] * (1.0f - fraction) + outer_max[index2] * fraction;
    
    return limit;
}
```

### 2.6 运行时应用

#### 2.6.1 修改 radialDeadzone()
```cpp
void AnalogInput::radialDeadzone(int stick_num, adc_instance & adc_inst) {
    float current_magnitude = adc_inst.xy_magnitude;
    
    // 计算动态外圈限制
    float outer_limit;
    if (adc_pairs[stick_num].forced_circularity == true) {
        // 强制圆形开关打开：使用原有的强制圆形和外圈死区校准功能
        outer_limit = adc_pairs[stick_num].out_deadzone;
        // forced_circularity 会限制最大幅度为 ANALOG_CENTER (0.5)
    } else if (adc_pairs[stick_num].outer_calib_enabled) {
        // 强制圆形开关关闭 + 启用手动外圈校准：使用手动校准的外圈限制
        float angle = calculateAngle(adc_inst.x_magnitude, adc_inst.y_magnitude);
        float outer_max_adc = getOuterLimit(angle, adc_pairs[stick_num].outer_max);
        
        // 转换为归一化的外圈限制（0.0-1.0）
        outer_limit = outer_max_adc / ADC_MAX;
    } else {
        // 强制圆形开关关闭 + 未启用手动外圈校准：使用原有的百分比外圈死区
        outer_limit = adc_pairs[stick_num].out_deadzone;
    }
    
    // 计算缩放因子（保持线性度）
    float scaling_factor = (current_magnitude - adc_pairs[stick_num].in_deadzone) / 
                          (outer_limit - adc_pairs[stick_num].in_deadzone);
    
    // 只有在强制圆形开关打开时才应用 forced_circularity
    if (adc_pairs[stick_num].forced_circularity == true) {
        scaling_factor = std::fmin(scaling_factor, ANALOG_CENTER);
    }
    
    // 应用缩放（保持线性度）
    adc_inst.x_value = ((adc_inst.x_magnitude / current_magnitude) * scaling_factor) + ANALOG_CENTER;
    adc_inst.y_value = ((adc_inst.y_magnitude / current_magnitude) * scaling_factor) + ANALOG_CENTER;
    
    adc_inst.x_value = std::clamp(adc_inst.x_value, ANALOG_MINIMUM, ANALOG_MAX);
    adc_inst.y_value = std::clamp(adc_inst.y_value, ANALOG_MINIMUM, ANALOG_MAX);
    
    // ... 原有的 anti_deadzone 处理 ...
}
```

### 2.7 微调功能设计

#### 2.7.1 中心点微调

**数据结构**：
```cpp
// 在 AnalogOptions 中添加
optional int32 center_finetune_x = 54;  // Stick 1 中心点X微调值（有符号，相对于校准值）
optional int32 center_finetune_y = 55;  // Stick 1 中心点Y微调值
optional int32 center_finetune_x2 = 56; // Stick 2 中心点X微调值
optional int32 center_finetune_y2 = 57; // Stick 2 中心点Y微调值
```

**应用方式**：
```cpp
// 在计算中心点时应用微调值
uint16_t final_center_x = base_center_x + center_finetune_x;
uint16_t final_center_y = base_center_y + center_finetune_y;
```

**UI布局**：
- 5x6网格布局（5列×6行，融合中心点微调和外圈微调）
- **第一行**：左上微调、空白、上微调、空白、右上微调
- **第二行**：空白、空白、空白、空白、空白
- **第三行**：空白、空白、中心X微调、空白、空白
- **第四行**：左微调、空白、空白、空白、右微调
- **第五行**：空白、空白、中心Y微调、空白、空白
- **第六行**：左下微调、空白、下微调、空白、右下微调

**每个微调项显示**：
- 上方：标题（"Center X"、"Center Y" 或方向名称如 "Top Left"、"Top" 等）
- 中间：数值输入框（支持直接输入数值，显示为有符号整数，如 "+5" 或 "-3"）
- 左侧：减号按钮（"-"）
- 右侧：加号按钮（"+"）

**操作方式**（Web页面）：
- 使用鼠标点击加减按钮进行操作（单次点击，无连续操作）
- 支持在数值输入框中直接输入数值
- 调整步长：可配置（默认1-10，建议5）

#### 2.7.2 外圈微调

**数据结构**：
```cpp
// 在 AnalogOptions 中添加
// 8个方向的外圈微调值（相对于36方向校准数据）
// 方向映射（中心角度）：左上(135°)、上(90°)、右上(45°)、左(180°)、右(0°)、左下(225°)、下(270°)、右下(315°)
// 索引顺序：0=左上, 1=上, 2=右上, 3=左, 4=右, 5=左下, 6=下, 7=右下
optional int32 outer_finetune_tl = 58;  // Stick 1 左上方向微调值 (索引0, 135°)
optional int32 outer_finetune_t = 59;   // Stick 1 上方向微调值 (索引1, 90°)
optional int32 outer_finetune_tr = 60;  // Stick 1 右上方向微调值 (索引2, 45°)
optional int32 outer_finetune_l = 61;   // Stick 1 左方向微调值 (索引3, 180°)
optional int32 outer_finetune_r = 62;   // Stick 1 右方向微调值 (索引4, 0°)
optional int32 outer_finetune_bl = 63;  // Stick 1 左下方向微调值 (索引5, 225°)
optional int32 outer_finetune_b = 64;   // Stick 1 下方向微调值 (索引6, 270°)
optional int32 outer_finetune_br = 65;  // Stick 1 右下方向微调值 (索引7, 315°)

// Stick 2 的8个方向微调值（字段66-73）
optional int32 outer_finetune_tl2 = 66;
optional int32 outer_finetune_t2 = 67;
optional int32 outer_finetune_tr2 = 68;
optional int32 outer_finetune_l2 = 69;
optional int32 outer_finetune_r2 = 70;
optional int32 outer_finetune_bl2 = 71;
optional int32 outer_finetune_b2 = 72;
optional int32 outer_finetune_br2 = 73;
```

**应用方式**：
```cpp
// 在 getOuterLimit() 中应用微调值
float getOuterLimit(float angle, const uint32_t outer_max[OUTER_CALIB_SAMPLES], 
                    const int32_t finetune_values[8]) {
    // ... 原有插值逻辑 ...
    
    // 根据角度确定8个方向中的哪一个
    int direction_index = getDirectionIndex(angle);  // 0-7: tl, t, tr, l, r, bl, b, br
    
    // 应用微调值
    float adjusted_limit = limit + finetune_values[direction_index];
    
    return adjusted_limit;
}

// 方向索引映射（角度到8方向）
// 8方向索引：0=左上(tl), 1=上(t), 2=右上(tr), 3=左(l), 4=右(r), 5=左下(bl), 6=下(b), 7=右下(br)
// 每个方向占45度扇形区域
int getDirectionIndex(float angle) {
    // 归一化角度到0-360
    while (angle < 0) angle += 360.0f;
    while (angle >= 360) angle -= 360.0f;
    
    // 8方向划分（每个方向45度，以22.5度为边界）
    // 右: 337.5°-22.5° (0°为中心)
    if (angle >= 337.5f || angle < 22.5f) return 4;  // r
    // 右上: 22.5°-67.5° (45°为中心)
    if (angle >= 22.5f && angle < 67.5f) return 2;  // tr
    // 上: 67.5°-112.5° (90°为中心)
    if (angle >= 67.5f && angle < 112.5f) return 1;  // t
    // 左上: 112.5°-157.5° (135°为中心)
    if (angle >= 112.5f && angle < 157.5f) return 0;  // tl
    // 左: 157.5°-202.5° (180°为中心)
    if (angle >= 157.5f && angle < 202.5f) return 3;  // l
    // 左下: 202.5°-247.5° (225°为中心)
    if (angle >= 202.5f && angle < 247.5f) return 5;  // bl
    // 下: 247.5°-292.5° (270°为中心)
    if (angle >= 247.5f && angle < 292.5f) return 6;  // b
    // 右下: 292.5°-337.5° (315°为中心)
    if (angle >= 292.5f && angle < 337.5f) return 7;  // br
    
    return 4;  // 默认右方向（不应到达这里）
}
```

**UI布局**：
- 5x6网格布局（5列×6行，融合中心点微调和外圈微调）
- **第一行**：左上微调、空白、上微调、空白、右上微调
- **第二行**：空白、空白、空白、空白、空白
- **第三行**：空白、空白、中心X微调、空白、空白
- **第四行**：左微调、空白、空白、空白、右微调
- **第五行**：空白、空白、中心Y微调、空白、空白
- **第六行**：左下微调、空白、下微调、空白、右下微调

**注意**：中心点微调和外圈微调使用相同的5x6布局，同时显示所有微调项：
- **中心点微调**：第三行和第五行中间显示中心X和中心Y
- **外圈微调**：第一行、第四行、第六行显示8个方向

**每个微调项显示**：
- 上方：标题（如 "Top Left"、"Top"、"Right"、"Center X"、"Center Y" 等）
- 中间：数值输入框（支持直接输入数值，显示为有符号整数，如 "+10" 或 "-5"）
- 左侧：减号按钮（"-"）
- 右侧：加号按钮（"+"）

**操作方式**（Web页面）：
- 使用鼠标点击加减按钮进行操作（单次点击，无连续操作）
- 支持在数值输入框中直接输入数值
- 调整步长：可配置（默认1-10，建议5）

#### 2.7.3 微调界面实现（Web页面）

**实现位置**：Web配置页面（React组件）

**功能说明**：
- 微调功能仅在Web页面实现，使用鼠标点击操作
- 不涉及设备端UI和D-pad操作，简化代码复杂度
- 所有微调项在5x6网格中同时显示
- 点击加减按钮直接调整对应数值，单次点击操作
- 数值输入框支持直接输入数值

**数据结构**（Web端）：
```javascript
// 微调数据状态
const [finetuneData, setFinetuneData] = useState({
  centerX: 0,
  centerY: 0,
  outerTL: 0,  // 左上
  outerT: 0,   // 上
  outerTR: 0,  // 右上
  outerL: 0,   // 左
  outerR: 0,   // 右
  outerBL: 0,  // 左下
  outerB: 0,   // 下
  outerBR: 0   // 右下
});

// 调整步长
const stepSize = 5;

// 调整函数（按钮点击）
const adjustValue = (key, delta) => {
  setFinetuneData(prev => ({
    ...prev,
    [key]: prev[key] + delta * stepSize
  }));
};

// 直接输入数值函数
const setValue = (key, value) => {
  // 验证输入值范围（建议限制在 ±1000 内）
  const clampedValue = Math.max(-1000, Math.min(1000, parseInt(value) || 0));
  setFinetuneData(prev => ({
    ...prev,
    [key]: clampedValue
  }));
};
```

### 2.8 线性度保证

#### 2.8.1 关键原则
1. **不修改 readPin()**：保持中心点到ADC_MAX/ADC_MIN的线性映射
2. **只修改外圈限制**：在 `radialDeadzone()` 中动态计算外圈限制
3. **保持缩放线性**：缩放因子计算保持线性关系
4. **微调值线性叠加**：微调值直接叠加到校准值，保持线性关系

#### 2.8.2 数学验证
- **中心到外圈映射**：`value = center + (adc - center) * scale`
- **缩放因子**：`scale = (magnitude - in_deadzone) / (outer_limit - in_deadzone)`
- **线性度**：`value` 与 `adc` 保持线性关系，只是 `outer_limit` 根据方向动态变化
- **微调应用**：
  - 中心点：`final_center = base_center + finetune_value`
  - 外圈：`final_outer_limit = base_outer_limit + finetune_value`

## 3. 实现步骤

### 3.1 阶段1：数据结构
1. 更新 `config.proto` 添加外圈校准字段
2. 更新 `analog.h` 添加运行时数据结构
3. 更新 `analog.cpp` 的 `setup()` 加载外圈校准数据

### 3.2 阶段2：校准界面
1. 更新 `StickCalibrationScreen.h` 添加外圈校准状态
2. 更新 `StickCalibrationScreen.cpp` 实现外圈校准流程
3. 实现多圈旋转采样函数 `autoSampleOuterCalibration()`（支持稳定性检测）
4. 实现旋转检测和圈数计数逻辑
5. 实现UI状态提示显示（简化显示，仅显示状态提示）

### 3.3 阶段3：运行时应用
1. 实现角度计算函数 `calculateAngle()`
2. 实现查找/插值函数 `getOuterLimit()`（基于36方向数据）
3. 修改 `radialDeadzone()` 根据强制圆形开关状态选择使用原有功能或手动外圈校准
4. 在中心点计算中应用微调值（`center_finetune_x/y`）
5. 在外圈限制计算中应用微调值（8方向微调值）
6. 实现方向索引映射函数 `getDirectionIndex()`（角度到8方向）

### 3.4 阶段4：配置接口
1. 更新 `webconfig.cpp` 支持外圈校准数据读写
2. 更新前端 `Analog.tsx` 添加外圈校准UI
3. 更新 `webconfig.cpp` 支持微调数据读写（中心点和外圈）
4. 更新前端 `Analog.tsx` 添加微调UI（5x6网格布局，鼠标点击操作）
5. 更新前端 `Analog.tsx` 实现强制圆形开关逻辑（打开时使用原功能，关闭时显示手动外圈校准按钮）
6. 确保手动校准按钮和微调按钮常态显示，不根据校准模式进行隐藏

## 4. 测试计划

### 4.1 功能测试
1. 36方向自动采样准确性
2. 固定4圈旋转检测和计数准确性
3. 95%填充检测逻辑（一圈完成的判断）
4. 历史最大值累积（所有圈的最大值持续累积）
5. 归一化95%阈值判断（是否推到极限）
6. 超时机制和质量检查（30秒超时 + 质量验证）
7. 多圈采样优化效果（同一方向多次经过时的最大值更新）
8. 插值计算正确性（非采样点的角度）
9. 线性度验证（各方向保持线性）
10. 与现有功能兼容性（中心点校准）
11. 强制圆形开关打开时，使用原有功能（outer_deadzone 和 forced_circularity）验证
12. 强制圆形开关关闭时，手动外圈校准按钮可用验证
13. 状态提示显示（简化显示，仅显示状态提示）
14. 中心点微调功能（Web页面，X/Y值调整、数值输入框直接输入、鼠标点击操作）
15. 外圈微调功能（Web页面，8方向调整、数值输入框直接输入、鼠标点击操作）
16. 数值输入框验证（输入范围限制、数值格式验证）
16. 微调值应用验证（中心点和外圈限制计算中正确应用微调值）
17. 方向索引映射准确性（角度到8方向的正确映射）
18. 微调界面布局（5x6网格、标题、数值输入框、按钮显示）
19. 按钮常态显示验证（手动校准按钮和微调按钮不根据模式隐藏）
20. 数值输入框直接输入功能（支持直接输入数值、范围验证、格式验证）

### 4.2 边界测试
1. 未校准时的回退行为（使用 outer_deadzone）
2. 旋转不足4圈时的行为（应继续采样）
3. 部分方向未采样完成的情况（应继续采样直到完成）
4. 极端角度（0°、10°、350°等）的查找和插值
5. 用户旋转速度过快时的采样覆盖（是否能正确检测完整旋转）
6. 用户未推到极限时的采样跳过
7. 旋转方向改变（顺时针/逆时针切换）的处理
8. 角度跨越0度边界时的旋转检测准确性
9. 超时完成的质量检查（至少1圈 + 80%方向）
10. 95%填充检测的准确性（是否正确定位一圈完成）
11. 微调值边界（最大值、最小值、溢出处理）
12. 微调值未设置时的默认行为（应为0，不影响校准值）
13. 8方向边界角度（22.5°、67.5°、112.5°等）的方向索引映射
14. 微调值保存和加载（配置持久化，Web页面）
15. 强制圆形开关状态切换（打开/关闭时功能切换）
16. 鼠标点击操作（单次点击，无连续操作）

## 5. 注意事项

1. **内存占用**：
   - 36个方向 × 2个摇杆 × 4字节 = 288字节（可接受）
   - 微调值：中心点4个（2摇杆×2轴）× 4字节 + 外圈16个（2摇杆×8方向）× 4字节 = 80字节
   - 总计：368字节（可接受）
2. **校准顺序**：必须先完成中心点校准，再进行外圈校准；微调在校准完成后进行
3. **向后兼容**：
   - **强制圆形开关打开**：使用原有的强制圆形和外圈死区校准功能，`outer_deadzone` 和 `forced_circularity` 参数有效
   - **强制圆形开关关闭**：显示手动外圈校准按钮，此时可以启用手动外圈校准
   - 手动外圈校准按钮和微调按钮**常态显示**，不根据校准模式进行隐藏
4. **功能互斥**：
   - 强制圆形开关打开时，使用原有的强制圆形和外圈死区校准功能，手动外圈校准功能不可用（按钮禁用，但保持显示）
   - 强制圆形开关关闭时，可以启用手动外圈校准，此时 `outer_deadzone` 和 `forced_circularity` 参数失效
   - 手动校准按钮和微调按钮**常态显示**，不根据校准模式进行隐藏
5. **UI提示**（简化显示）：
   - 明确指示用户将摇杆推到极限位置
   - 提示用户保持推到极限并缓慢旋转摇杆（需要旋转4圈）
   - 实时显示状态提示（简化）：
     - **状态提示**: 
       - "继续旋转..."（未达到4圈）
       - "可以完成"（达到4圈后）
       - "校准完成"（完成时）
   - 达到完成条件后自动提示用户停止
6. **固定4圈采样策略**：
   - 检测摇杆是否推到极限（归一化95%阈值）
   - **固定4圈要求**：必须旋转4圈才能完成
   - **95%填充检测**：每圈检测是否95%的方向都有超过95%的值
   - **历史最大值累积**：所有圈的最大值持续累积，不清空历史数据
   - **支持多圈旋转**：允许用户多次经过同一方向，持续更新每个方向的最大值
   - **超时机制**：30秒后允许手动完成，但会进行质量检查（至少1圈 + 80%方向）
   - 采样频率约100Hz（10ms间隔）
7. **固定4圈的优势**：
   - **标准化**：与业界成熟工具（DualShock Tools）一致
   - **数据充分**：4圈确保有足够的数据覆盖所有方向
   - **确定性高**：用户知道需要旋转几圈，体验更可预测
   - **可靠性强**：通过多圈采样找到真正的极限值
8. **微调功能注意事项**：
   - **实现位置**：微调功能仅在Web页面实现，使用鼠标点击操作，简化代码复杂度
   - **微调值范围**：建议限制在 ±1000 ADC单位内，避免过度调整
   - **微调值默认**：未设置时默认为0，不影响校准值
   - **微调值应用**：微调值直接叠加到校准值，保持线性关系
   - **8方向映射**：使用45度扇形区域（每个方向45度）进行方向识别
   - **UI布局**：5x6网格，融合中心点微调和外圈微调，同时显示所有微调项
   - **操作方式**：仅使用鼠标点击加减按钮，单次点击操作（无连续操作）
   - **操作反馈**：调整时实时显示数值变化，提供视觉反馈
   - **保存机制**：微调值在点击保存按钮时保存到配置
   - **按钮显示**：手动校准按钮和微调按钮常态显示，不根据校准模式进行隐藏

