// GP-Fusion Lite 滑动菜单：板载方案 / 背键映射 / 摇杆 / 硬件开关 / 功能设置 / 灯光
#include "GPFusionMenuScreen.h"

#include "enums.pb.h"
#include "storagemanager.h"
#include "drivermanager.h"
#include "addons/display.h"
#include "GamepadState.h"
#include "cn_font_lite.h"
#include "fonts/GP_Font_Standard.h"
#include "eventmanager.h"
#include "events/GPStorageSaveEvent.h"
#include "events/GPRestartEvent.h"
#include "hml_back_mapping_preset.h"
#include "addons/analog.h"
#include "addons/mcp3208_adc.h"

#include <cstring>
#include "pico/stdlib.h"

enum OptType { OPT_ENUM, OPT_BOOL, OPT_INT, OPT_ACTION, OPT_SLIDER, OPT_RESERVED, OPT_DEC };

struct LiteOpt {
  const char* label;
  uint8_t type;
  int min, max, step;
  const char* const* names;
  int nameCount;
  const char* unit;
  int (*get)();
  void (*set)(int);
};

struct LiteSection {
  const char* title;
  LiteOpt* opts;
  int count;
};

// ---- config access ----
static GamepadOptions& GOP() { return Storage::getInstance().getGamepadOptions(); }
static DisplayOptions& DOP() { return Storage::getInstance().getDisplayOptions(); }
static AnimationOptions& AOP() { return Storage::getInstance().getAnimationOptions(); }
static LEDOptions& LOP() { return Storage::getInstance().getLedOptions(); }
static AddonOptions& AOP2() { return Storage::getInstance().getAddonOptions(); }
static PeripheralOptions& POP() { return Storage::getInstance().getPeripheralOptions(); }

static bool needsReboot = false;
static bool calibDone = false;
static uint32_t calibBackup[4] = {0, 0, 0, 0};

static void maybeRebootIfNeeded();

// 输入模式映射表：菜单序号 -> InputMode 枚举值
static const int INPUT_MAP[] = {
  0,   // XInput
  18,  // XInput for PC
  4,   // PS4
  17,  // PS4 for PC
  15,  // Switch Pro
  16,  // P5 General
  5,   // Xbox One
  2,   // PS3
  13,  // PS5
  3,   // Keyboard
  6,   // Sega Genesis Mini
  7,   // NEOGEO mini
  8,   // PC Engine Mini
  9,   // EGRET II mini
  10,  // ASTROCITY Mini
  11,  // Playstation Classic
  12,  // Original Xbox
  14,  // Generic HID
};
static const int INPUT_MAP_COUNT = 18;
static const char* const N_INPUT[] = {
  "XInput","XInput PC","PS4","PS4 PC","SW PRO","P5","XB1","PS3","PS5","KBD",
  "MD Mini","NEOGEO","PCE Mini","EGRET II","ASTRO","PS Classic","XB Orig","HID"
};
static const char* const N_SOCD[] = {"UP","NEU","2ND","1ST","BYP"};
static const char* const N_DPAD[] = {"十字键","左摇杆","右摇杆"};
static const char* const N_ANIM[] = {"静态","渐变","追逐","呼吸"};
static const char* const N_COLOR[] = {"BLK","WHT","RED","ORG","YEL","LME","GRN","SEA",
                                      "AQU","SKY","BLU","PUR","PNK","MAG","IND","VIO"};

static int gInput() {
  int cur = (int)GOP().inputMode;
  for (int i = 0; i < INPUT_MAP_COUNT; i++) if (INPUT_MAP[i] == cur) return i;
  return 0;
}
static void sInput(int v) {
  if (v >= 0 && v < INPUT_MAP_COUNT) GOP().inputMode = (InputMode)INPUT_MAP[v];
}
static int gSocd() { return (int)GOP().socdMode; }
static void sSocd(int v) { GOP().socdMode = (SOCDMode)v; }
static int gDpad() { return (int)GOP().dpadMode; }
static void sDpad(int v) { GOP().dpadMode = (DpadMode)v; }
static int gFour() { return GOP().fourWayMode ? 1 : 0; }
static void sFour(int v) { GOP().fourWayMode = v ? true : false; }

// 连发模式开关（对应网页 GNS设置-其他功能 中的 启用连发）
static int gTurbo() { return AOP2().turboOptions.enabled ? 1 : 0; }
static void sTurbo(int v) {
    bool en = v ? true : false;
    if (AOP2().turboOptions.enabled == en) return;
    AOP2().turboOptions.enabled = en;
    // TurboInput 插件加载与否仅在开机时判定，保存重启后生效
    needsReboot = true;
}
static int gInvX() { return GOP().invertXAxis ? 1 : 0; }
static void sInvX(int v) { GOP().invertXAxis = v ? true : false; }
static int gInvY() { return GOP().invertYAxis ? 1 : 0; }
static void sInvY(int v) { GOP().invertYAxis = v ? true : false; }
static int gDebounce() { return (int)GOP().debounceDelay; }
static void sDebounce(int v) { GOP().debounceDelay = (uint32_t)v; }
static int gProfile() { return (int)GOP().profileNumber; }
static void sProfile(int v) { GOP().profileNumber = (uint32_t)v; }
// 静态颜色：alCustomStaticColorIndex 存储的是 0x00RRGGBB 值，需映射到 16 色枚举
static const uint32_t COLOR_RGB[] = {
  0x000000, 0xFFFFFF, 0xFF0000, 0xFFA500, 0xFFFF00, 0x00FF00, 0x008000, 0x2E8B57,
  0x00FFFF, 0x87CEEB, 0x0000FF, 0x800080, 0xFFC0CB, 0xFF00FF, 0x4B0082, 0xEE82EE
};
static int gColor() {
  uint32_t c = AOP().alCustomStaticColorIndex;
  for (int i = 0; i < 16; i++) if (COLOR_RGB[i] == c) return i;
  return 1;
}
static void sColor(int v) {
  if (v >= 0 && v < 16) AOP().alCustomStaticColorIndex = COLOR_RGB[v];
}
static int gLedOff() { return LOP().turnOffWhenSuspended ? 1 : 0; }
static void sLedOff(int v) { LOP().turnOffWhenSuspended = v ? true : false; }
static int gGyro() { return AOP2().lsm6dsrOptions.enabled ? 1 : 0; }
static void sGyro(int v) {
    AOP2().lsm6dsrOptions.enabled = v ? true : false;
    // SPI1 由 LSM6DSR 体感与 nRF24 直连共享：任一开启即使能，全关才关闭
    POP().blockSPI1.enabled =
        AOP2().lsm6dsrOptions.enabled ||
        GOP().nrf24LinkEnabled;
    needsReboot = true;
}

// USB 验证器开关（对应网页配置 GNS设置-硬件配置 中的 USB验证器，即 blockUSB0.enabled）
static int gUsbAuth() { return POP().blockUSB0.enabled ? 1 : 0; }
static void sUsbAuth(int v) {
    POP().blockUSB0.enabled = v ? 1 : 0;
    // 三互斥：开启 USB 验证器时自动关闭蓝牙模式与无线模式（GPIO18/19 复用冲突）
    if (v) {
        GOP().bluetoothLinkEnabled = false;
        GOP().nrf24LinkEnabled = false;
    }
    needsReboot = true;
}

// 蓝牙模式开关（BLE 输出路径，对应网页 硬件配置 中的 蓝牙模式开关）
static int gBle() { return GOP().bluetoothLinkEnabled ? 1 : 0; }
static void sBle(int v) {
    GOP().bluetoothLinkEnabled = v ? true : false;
    // 三互斥：开启蓝牙时自动关闭无线模式与 USB 验证器（GPIO18/19 复用冲突）
    if (v) {
        GOP().nrf24LinkEnabled = false;
        POP().blockUSB0.enabled = 0;
    }
    needsReboot = true;
}

// 无线模式开关（nRF24 直连 SPI1，对应网页 硬件配置 中的 无线模式开关）
static int gNrf24() { return GOP().nrf24LinkEnabled ? 1 : 0; }
static void sNrf24(int v) {
    GOP().nrf24LinkEnabled = v ? true : false;
    // 三互斥：开启 nRF24 直连时自动关闭蓝牙与 USB 验证器
    if (v) {
        GOP().bluetoothLinkEnabled = false;
        POP().blockUSB0.enabled = 0;
    }
    // SPI1 由 nRF24 直连与 LSM6DSR 体感共享：任一开启即使能，全关才关闭
    POP().blockSPI1.enabled =
        GOP().nrf24LinkEnabled ||
        Storage::getInstance().getAddonOptions().lsm6dsrOptions.enabled;
    needsReboot = true;
}

// 摇杆死区/反死区（原始值0-200，对应0.0%-20.0%，0.1%步进）
static int gInnerDz() { return (int)AOP2().analogOptions.inner_deadzone; }
static void sInnerDz(int v) { AOP2().analogOptions.inner_deadzone = (uint32_t)v; needsReboot = true; }
static int gAntiDz() { return (int)AOP2().analogOptions.anti_deadzone; }
static void sAntiDz(int v) { AOP2().analogOptions.anti_deadzone = (uint32_t)v; needsReboot = true; }
static int gInnerDz2() { return (int)AOP2().analogOptions.inner_deadzone2; }
static void sInnerDz2(int v) { AOP2().analogOptions.inner_deadzone2 = (uint32_t)v; needsReboot = true; }
static int gAntiDz2() { return (int)AOP2().analogOptions.anti_deadzone2; }
static void sAntiDz2(int v) { AOP2().analogOptions.anti_deadzone2 = (uint32_t)v; needsReboot = true; }

// 读取摇杆原始ADC值（尝试两种源）
static bool readRawStick(uint8_t stickNum, uint16_t& rawX, uint16_t& rawY) {
    uint16_t xCenter, yCenter, adcMax;
    bool xValid, yValid;
    if (AnalogInput::getRawStickForProcessor(stickNum, rawX, rawY, xCenter, yCenter, xValid, yValid, adcMax))
        return xValid && yValid;
    if (MCP3208ADCAddon::getRawStickForProcessor(stickNum, rawX, rawY, xCenter, yCenter, xValid, yValid, adcMax))
        return xValid && yValid;
    return false;
}

// 摇杆中心校准：连续读取5次取平均，返回1标记需要保存
static int gCalib() {
    uint32_t sumLX = 0, sumLY = 0, sumRX = 0, sumRY = 0;
    uint8_t validCount = 0;
    for (int i = 0; i < 5; i++) {
        uint16_t lx, ly, rx, ry;
        if (readRawStick(0, lx, ly) && readRawStick(1, rx, ry)) {
            sumLX += lx; sumLY += ly; sumRX += rx; sumRY += ry;
            validCount++;
        }
        busy_wait_us_32(2000);
    }
    if (validCount == 0) return 0;
    AnalogOptions& ao = AOP2().analogOptions;
    calibBackup[0] = ao.joystick_center_x;
    calibBackup[1] = ao.joystick_center_y;
    calibBackup[2] = ao.joystick_center_x2;
    calibBackup[3] = ao.joystick_center_y2;
    ao.joystick_center_x = sumLX / validCount;
    ao.joystick_center_y = sumLY / validCount;
    ao.joystick_center_x2 = sumRX / validCount;
    ao.joystick_center_y2 = sumRY / validCount;
    calibDone = true;
    needsReboot = true;
    return 1;
}
static void sCalib(int) {}
static int gSave() {
  // 菜单跑在 core1，保存必须通过事件交给 core0 执行
  EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));
  maybeRebootIfNeeded();
  return 0;
}
static void sSave(int) {}
static int gReset() { Storage::getInstance().ResetSettings(); Storage::getInstance().save(); return 0; }
static void sReset(int) {}

// 背键配置档 (1-3显示, 0-2存储)
static int gBackPreset() {
  return (int)getHmlBackMappingActivePresetIndex(AOP2()) + 1;
}
static void sBackPreset(int v) {
  HmlBackMappingPresetOptions& opts = getHmlBackMappingPresetOptions(AOP2());
  opts.activePreset = (uint32_t)(v - 1);
  opts.has_activePreset = true;
}

// 背键映射枚举：菜单序号 -> GpioAction 枚举值
// 索引0=无(NONE=-10), 1=上, 2=下, 3=左, 4=右, 5=方块(B3=7), 6=三角(B4=8),
// 7=圆(B2=6), 8=叉(B1=5), 9=菜单(S1=13), 10=选项(S2=14),
// 11=L1(=9), 12=L2(=11), 13=R1(=10), 14=R2(=12), 15=L3(=17), 16=R3(=18)
// 17=OTHER(网页前端设置的菜单外映射项，显示但不可改，保存时保留原值)
static const int BACK_ACTION_MAP[] = {-10, 1, 2, 3, 4, 7, 8, 6, 5, 14, 13, 9, 11, 10, 12, 17, 18};
static const char* const N_BACK_ACTION[] = {
  "无","上","下","左","右","方块","三角","圆","叉","菜单","选项","L1","L2","R1","R2","L3","R3","OTHER"
};
static const int BACK_ACTION_COUNT = 18;
static const int BACK_ACTION_OTHER_IDX = 17;
static int backActionToIndex(int action) {
  for (int i = 0; i < BACK_ACTION_COUNT - 1; i++) if (BACK_ACTION_MAP[i] == action) return i;
  return BACK_ACTION_OTHER_IDX;
}

// 背键映射读取（从当前活动预设的 HmlBackMappingPreset）
static int gBackLB1() {
  return backActionToIndex((int)getActiveHmlBackPreset(AOP2()).leftBack1Mapping.action);
}
static int gBackRB1() {
  return backActionToIndex((int)getActiveHmlBackPreset(AOP2()).rightBack1Mapping.action);
}
static int gBackLB2() {
  return backActionToIndex((int)getActiveHmlBackPreset(AOP2()).leftBack2Mapping.action);
}
static int gBackRB2() {
  return backActionToIndex((int)getActiveHmlBackPreset(AOP2()).rightBack2Mapping.action);
}
static int gBackLB3() {
  return backActionToIndex((int)getActiveHmlBackPreset(AOP2()).leftBack3Mapping.action);
}
static int gBackRB3() {
  return backActionToIndex((int)getActiveHmlBackPreset(AOP2()).rightBack3Mapping.action);
}
static int gBackLFN() {
  return backActionToIndex((int)getActiveHmlBackPreset(AOP2()).leftFnMapping.action);
}
static int gBackRFN() {
  return backActionToIndex((int)getActiveHmlBackPreset(AOP2()).rightFnMapping.action);
}
static int gBackLMT() {
  return backActionToIndex((int)getActiveHmlBackPreset(AOP2()).leftMtMapping.action);
}
static int gBackRMT() {
  return backActionToIndex((int)getActiveHmlBackPreset(AOP2()).rightMtMapping.action);
}
// 背键映射写入（到当前活动预设）
// OTHER 索引跳过写入，保留网页前端设置的菜单外映射值不被清空
static void sBackLB1(int v) {
  needsReboot = true;
  if (v == BACK_ACTION_OTHER_IDX) return;
  HmlBackMappingPreset& p = getActiveHmlBackPreset(AOP2());
  p.leftBack1Mapping.action = (GpioAction)BACK_ACTION_MAP[v];
  p.leftBack1Mapping.has_action = true;
  p.has_leftBack1Mapping = true;
}
static void sBackRB1(int v) {
  needsReboot = true;
  if (v == BACK_ACTION_OTHER_IDX) return;
  HmlBackMappingPreset& p = getActiveHmlBackPreset(AOP2());
  p.rightBack1Mapping.action = (GpioAction)BACK_ACTION_MAP[v];
  p.rightBack1Mapping.has_action = true;
  p.has_rightBack1Mapping = true;
}
static void sBackLB2(int v) {
  needsReboot = true;
  if (v == BACK_ACTION_OTHER_IDX) return;
  HmlBackMappingPreset& p = getActiveHmlBackPreset(AOP2());
  p.leftBack2Mapping.action = (GpioAction)BACK_ACTION_MAP[v];
  p.leftBack2Mapping.has_action = true;
  p.has_leftBack2Mapping = true;
}
static void sBackRB2(int v) {
  needsReboot = true;
  if (v == BACK_ACTION_OTHER_IDX) return;
  HmlBackMappingPreset& p = getActiveHmlBackPreset(AOP2());
  p.rightBack2Mapping.action = (GpioAction)BACK_ACTION_MAP[v];
  p.rightBack2Mapping.has_action = true;
  p.has_rightBack2Mapping = true;
}
static void sBackLB3(int v) {
  needsReboot = true;
  if (v == BACK_ACTION_OTHER_IDX) return;
  HmlBackMappingPreset& p = getActiveHmlBackPreset(AOP2());
  p.leftBack3Mapping.action = (GpioAction)BACK_ACTION_MAP[v];
  p.leftBack3Mapping.has_action = true;
  p.has_leftBack3Mapping = true;
}
static void sBackRB3(int v) {
  needsReboot = true;
  if (v == BACK_ACTION_OTHER_IDX) return;
  HmlBackMappingPreset& p = getActiveHmlBackPreset(AOP2());
  p.rightBack3Mapping.action = (GpioAction)BACK_ACTION_MAP[v];
  p.rightBack3Mapping.has_action = true;
  p.has_rightBack3Mapping = true;
}
static void sBackLFN(int v) {
  needsReboot = true;
  if (v == BACK_ACTION_OTHER_IDX) return;
  HmlBackMappingPreset& p = getActiveHmlBackPreset(AOP2());
  p.leftFnMapping.action = (GpioAction)BACK_ACTION_MAP[v];
  p.leftFnMapping.has_action = true;
  p.has_leftFnMapping = true;
}
static void sBackRFN(int v) {
  needsReboot = true;
  if (v == BACK_ACTION_OTHER_IDX) return;
  HmlBackMappingPreset& p = getActiveHmlBackPreset(AOP2());
  p.rightFnMapping.action = (GpioAction)BACK_ACTION_MAP[v];
  p.rightFnMapping.has_action = true;
  p.has_rightFnMapping = true;
}
static void sBackLMT(int v) {
  needsReboot = true;
  if (v == BACK_ACTION_OTHER_IDX) return;
  HmlBackMappingPreset& p = getActiveHmlBackPreset(AOP2());
  p.leftMtMapping.action = (GpioAction)BACK_ACTION_MAP[v];
  p.leftMtMapping.has_action = true;
  p.has_leftMtMapping = true;
}
static void sBackRMT(int v) {
  needsReboot = true;
  if (v == BACK_ACTION_OTHER_IDX) return;
  HmlBackMappingPreset& p = getActiveHmlBackPreset(AOP2());
  p.rightMtMapping.action = (GpioAction)BACK_ACTION_MAP[v];
  p.rightMtMapping.has_action = true;
  p.has_rightMtMapping = true;
}

// 按键配置档范围固定 1..2（基础映射1 / 基础映射2；层槽不进选择路径）
static int gProfileMax() {
  return 2;
}

// 环境光亮度 - 智能映射当前动画模式对应的亮度字段
static int gAmbientBright() {
  int idx = (int)AOP().ambientLightEffectsCountIndex;
  float b = 0.0f;
  switch (idx) {
    case 4: b = AOP().alStaticBrightnessCustomThemeX; break;  // 静态
    case 1: b = AOP().alGradientBrightnessCustomX; break;      // 渐变
    case 2: b = AOP().alChaseBrightnessCustomX; break;         // 追逐
    case 3: b = AOP().alBreathBrightnessCustomX; break;        // 呼吸
    default: b = 0.0f; break;
  }
  int v = (int)(b * 100.0f + 0.5f);
  return v < 0 ? 0 : (v > 100 ? 100 : v);
}
static void sAmbientBright(int v) {
  if (v < 0) v = 0; if (v > 100) v = 100;
  float f = v / 100.0f;
  int idx = (int)AOP().ambientLightEffectsCountIndex;
  switch (idx) {
    case 4: AOP().alStaticBrightnessCustomThemeX = f; break;
    case 1: AOP().alGradientBrightnessCustomX = f; break;
    case 2: AOP().alChaseBrightnessCustomX = f; break;
    case 3: AOP().alBreathBrightnessCustomX = f; break;
  }
}

// 动画模式映射：菜单序号 -> proto 枚举值
// 菜单 0=静态(4=STATIC_THEME) 1=渐变(1=GRADIENT) 2=追逐(2=CHASE) 3=呼吸(3=BREATH)
static const int ANIM_MAP[] = {4, 1, 2, 3};
static const int ANIM_MAP_COUNT = 4;
static int gAnimMode() {
  int cur = (int)AOP().ambientLightEffectsCountIndex;
  for (int i = 0; i < ANIM_MAP_COUNT; i++) if (ANIM_MAP[i] == cur) return i;
  return 0;
}
static void sAnimMode(int v) {
  if (v >= 0 && v < ANIM_MAP_COUNT) AOP().ambientLightEffectsCountIndex = (uint32_t)ANIM_MAP[v];
}

// 渐变速度 (1-10)
static int gGradientSpd() {
  int v = (int)AOP().ambientLightGradientSpeed;
  return v < 1 ? 1 : (v > 10 ? 10 : v);
}
static void sGradientSpd(int v) {
  if (v < 1) v = 1; if (v > 10) v = 10;
  AOP().ambientLightGradientSpeed = (uint32_t)v;
}

// 追逐速度 (int 0-100, 反转显示)
static int gChaseSpd() {
  int s = (int)AOP().ambientLightChaseSpeed;
  int v = 100 - s;
  return v < 0 ? 0 : (v > 100 ? 100 : v);
}
static void sChaseSpd(int v) {
  if (v < 0) v = 0; if (v > 100) v = 100;
  AOP().ambientLightChaseSpeed = 100 - v;
}

// 呼吸速度 (float 0.01-0.1 -> int 1-10, ×100 转换)
static int gBreathSpd() {
  int v = (int)(AOP().ambientLightBreathSpeed * 100.0f + 0.5f);
  return v < 1 ? 1 : (v > 10 ? 10 : v);
}
static void sBreathSpd(int v) {
  if (v < 1) v = 1; if (v > 10) v = 10;
  AOP().ambientLightBreathSpeed = (float)v / 100.0f;
}

// 页面0 板载方案
static LiteOpt optOnboard[] = {
  {"输入模式", OPT_ENUM, 0, 18, 1, N_INPUT, 19, "", gInput, sInput},
  {"按键配置档", OPT_INT, 1, 1, 1, NULL, 0, "", gProfile, sProfile},
  {"背键配置档", OPT_INT, 1, 3, 1, NULL, 0, "", gBackPreset, sBackPreset},
  {"保存设置", OPT_ACTION, 0, 0, 0, NULL, 0, "", gSave, sSave},
  {"恢复默认", OPT_ACTION, 0, 0, 0, NULL, 0, "", gReset, sReset},
};
// 页面1 背键映射：10 个背键/FN/MT，每个可选 18 种映射（含 OTHER 占位）
static LiteOpt optBackMap[] = {
  {"左背键1", OPT_ENUM, 0, 17, 1, N_BACK_ACTION, 18, "", gBackLB1, sBackLB1},
  {"右背键1", OPT_ENUM, 0, 17, 1, N_BACK_ACTION, 18, "", gBackRB1, sBackRB1},
  {"左背键2", OPT_ENUM, 0, 17, 1, N_BACK_ACTION, 18, "", gBackLB2, sBackLB2},
  {"右背键2", OPT_ENUM, 0, 17, 1, N_BACK_ACTION, 18, "", gBackRB2, sBackRB2},
  {"左背键3", OPT_ENUM, 0, 17, 1, N_BACK_ACTION, 18, "", gBackLB3, sBackLB3},
  {"右背键3", OPT_ENUM, 0, 17, 1, N_BACK_ACTION, 18, "", gBackRB3, sBackRB3},
  {"左FN键",  OPT_ENUM, 0, 17, 1, N_BACK_ACTION, 18, "", gBackLFN, sBackLFN},
  {"右FN键",  OPT_ENUM, 0, 17, 1, N_BACK_ACTION, 18, "", gBackRFN, sBackRFN},
  {"左MT键",  OPT_ENUM, 0, 17, 1, N_BACK_ACTION, 18, "", gBackLMT, sBackLMT},
  {"右MT键",  OPT_ENUM, 0, 17, 1, N_BACK_ACTION, 18, "", gBackRMT, sBackRMT},
};
// 页面2 摇杆
static LiteOpt optStick[] = {
  {"校准", OPT_ACTION, 0, 0, 0, NULL, 0, "", gCalib, sCalib},
  {"左摇杆死区", OPT_DEC, 0, 200, 1, NULL, 0, "%", gInnerDz, sInnerDz},
  {"左摇杆反死区", OPT_DEC, 0, 200, 1, NULL, 0, "%", gAntiDz, sAntiDz},
  {"右摇杆死区", OPT_DEC, 0, 200, 1, NULL, 0, "%", gInnerDz2, sInnerDz2},
  {"右摇杆反死区", OPT_DEC, 0, 200, 1, NULL, 0, "%", gAntiDz2, sAntiDz2},
};
// 页面3 硬件开关
static LiteOpt optHardware[] = {
  {"陀螺仪", OPT_BOOL, 0, 1, 1, NULL, 0, "", gGyro, sGyro},
  {"验证器", OPT_BOOL, 0, 1, 1, NULL, 0, "", gUsbAuth, sUsbAuth},
  {"BLE模式", OPT_BOOL, 0, 1, 1, NULL, 0, "", gBle, sBle},
  {"无线模式", OPT_BOOL, 0, 1, 1, NULL, 0, "", gNrf24, sNrf24},
};
// 页面4 功能设置
static LiteOpt optFunc[] = {
  {"SOCD模式", OPT_ENUM, 0, 4, 1, N_SOCD, 5, "", gSocd, sSocd},
  {"四向模式", OPT_BOOL, 0, 1, 1, NULL, 0, "", gFour, sFour},
  {"十字键模式", OPT_ENUM, 0, 2, 1, N_DPAD, 3, "", gDpad, sDpad},
  {"Turbo模式", OPT_BOOL, 0, 1, 1, NULL, 0, "", gTurbo, sTurbo},
  {"反向X", OPT_BOOL, 0, 1, 1, NULL, 0, "", gInvX, sInvX},
  {"反向Y", OPT_BOOL, 0, 1, 1, NULL, 0, "", gInvY, sInvY},
  {"去抖延迟", OPT_INT, 1, 20, 1, NULL, 0, "ms", gDebounce, sDebounce},
};
// 页面5 灯光
static LiteOpt optLed[] = {
  {"动画模式", OPT_ENUM, 0, 3, 1, N_ANIM, 4, "", gAnimMode, sAnimMode},
  {"环境光亮度", OPT_INT, 0, 100, 1, NULL, 0, "%", gAmbientBright, sAmbientBright},
  {"静态颜色", OPT_ENUM, 0, 15, 1, N_COLOR, 16, "", gColor, sColor},
  {"渐变速度", OPT_SLIDER, 1, 10, 1, NULL, 0, "", gGradientSpd, sGradientSpd},
  {"追逐速度", OPT_SLIDER, 0, 100, 1, NULL, 0, "", gChaseSpd, sChaseSpd},
  {"呼吸速度", OPT_SLIDER, 1, 10, 1, NULL, 0, "", gBreathSpd, sBreathSpd},
  {"挂起关灯", OPT_BOOL, 0, 1, 1, NULL, 0, "", gLedOff, sLedOff},
};

// 每页单分区，section 恒为 0
static LiteSection secOnboard[]  = {{"板载方案", optOnboard, 5}};
static LiteSection secBackMap[]  = {{"背键映射", optBackMap, 10}};
static LiteSection secStick[]    = {{"摇杆", optStick, 5}};
static LiteSection secHardware[] = {{"硬件开关", optHardware, 4}};
static LiteSection secFunc[]     = {{"功能设置", optFunc, 7}};
static LiteSection secLed[]      = {{"彩灯", optLed, 7}};

static const char* const PAGE_TITLES[] = {"板载方案", "背键映射", "摇杆", "硬件开关", "功能设置", "灯光"};
static const int NUM_PAGES = 6;

// ---- screen state ----
static int page = 0;
static int level = 0;        // 0=页面 1=选项
static int sel = 0;
static int scroll = 0;
static bool dirty = false;
static bool confirmOpen = false;
static int confirmChoice = 0;
static bool calibPromptOpen = false;
static int snap[10];
static int lastSavedInputMode = -1;

// 保存后按需重启（输入模式/陀螺仪/校准等改动需要重启生效）
static void maybeRebootIfNeeded() {
  int cur = (int)GOP().inputMode;
  if (cur != lastSavedInputMode) {
    lastSavedInputMode = cur;
    needsReboot = true;
  }
  if (needsReboot) {
    needsReboot = false;
    EventManager::getInstance().triggerEvent(new GPRestartEvent(System::BootMode::GAMEPAD));
  }
}

static bool animating = false;
static unsigned long animStart = 0;
static int animFrom = 0, animTo = 0;

// 列表上下滑动动画
static float selY = 0, selYFrom = 0, selYTo = 0;
static float scrollPx = 0, scrollPxFrom = 0, scrollPxTo = 0;
static bool listAnim = false;
static unsigned long listAnimStart = 0;

static uint16_t prevB = 0;
static uint8_t prevD = 0;
static unsigned long lastRepeat = 0;
static uint8_t repeatMask = 0;
static unsigned long lastB1Act = 0;   // B1 最后触发时间（消抖冷却）
static unsigned long lastB2Act = 0;   // B2 最后触发时间（消抖冷却）
static const unsigned long BTN_COOLDOWN_MS = 500;

static LiteSection* currentSections() {
  switch (page) {
    case 0: return secOnboard;
    case 1: return secBackMap;
    case 2: return secStick;
    case 3: return secHardware;
    case 4: return secFunc;
    case 5: return secLed;
    default: return NULL;
  }
}
static LiteSection& curSection() {
  return currentSections()[0];
}

static void startListAnim(int selIdx, int scrollIdx) {
  selYFrom = selY;
  selYTo = selIdx * 13.0f;
  scrollPxFrom = scrollPx;
  scrollPxTo = scrollIdx * 13.0f;
  listAnimStart = getMillis();
  listAnim = true;
}

static void resetListAnim() {
  selY = 0; selYFrom = 0; selYTo = 0;
  scrollPx = 0; scrollPxFrom = 0; scrollPxTo = 0;
  listAnim = false;
}

static void snapshot() {
  LiteSection& s = curSection();
  // 按键配置档范围固定 1..2
	optOnboard[1].max = gProfileMax();
  for (int i = 0; i < s.count && i < 10; i++) {
    if (s.opts[i].type == OPT_ACTION || s.opts[i].type == OPT_RESERVED) { snap[i] = 0; continue; }
    snap[i] = s.opts[i].get();
  }
}
static void restore() {
  LiteSection& s = curSection();
  for (int i = 0; i < s.count && i < 10; i++) {
    if (s.opts[i].type == OPT_ACTION || s.opts[i].type == OPT_RESERVED) continue;
    s.opts[i].set(snap[i]);
  }
}

// 撤销校准+清除重启标志（确认对话框取消时调用）
static void undoCalibAndFlags() {
  restore();
  if (calibDone) {
    AnalogOptions& ao = AOP2().analogOptions;
    ao.joystick_center_x = calibBackup[0];
    ao.joystick_center_y = calibBackup[1];
    ao.joystick_center_x2 = calibBackup[2];
    ao.joystick_center_y2 = calibBackup[3];
    calibDone = false;
  }
  needsReboot = false;
}

// ---- drawing helpers (clipped to 128x64) ----
static GPGFX* R = NULL;
static void px(int x, int y, int c) {
  if (x >= 0 && x < 128 && y >= 0 && y < 64) R->drawPixel(x, y, c);
}
static void fill(int x, int y, int w, int h, int c) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) px(xx, yy, c);
}

static void drawAscii(int x, int y, const char* s, int color);

static int findCJK(uint16_t cp) {
  int lo = 0, hi = CN_FONT_NUM - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (CN_FONT_CODES[mid] == cp) return mid;
    if (CN_FONT_CODES[mid] < cp) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}

static void drawCJKChar(int x, int y, uint16_t cp, int color) {
  int idx = findCJK(cp);
  if (idx < 0) return;
  const uint8_t* g = CN_FONT_GLYPHS[idx];
  for (int row = 0; row < CN_FONT_SIZE; row++) {
    uint16_t bits = ((uint16_t)g[row * 2] << 8) | g[row * 2 + 1];
    for (int col = 0; col < CN_FONT_SIZE; col++) {
      if (bits & (0x8000 >> col)) px(x + col, y + row, color);
    }
  }
}

static int cjkWidth(const char* s) {
  int w = 0;
  while (*s) {
    uint8_t c = (uint8_t)*s;
    if (c < 0x80) { w += 6; s++; }
    else if ((c & 0xE0) == 0xC0) { w += CN_FONT_SIZE; s += 2; }
    else if ((c & 0xF0) == 0xE0) { w += CN_FONT_SIZE; s += 3; }
    else s++;
  }
  return w;
}

static void drawCJK(int x, int y, const char* s, int color, bool centered = false) {
  if (centered) x -= cjkWidth(s) / 2;
  while (*s) {
    uint8_t c = (uint8_t)*s;
    if (c < 0x80) {
      char buf[2] = {(char)c, 0};
      drawAscii(x, y + 2, buf, color);
      x += 6; s++;
      continue;
    }
    uint16_t cp = 0;
    if ((c & 0xE0) == 0xC0) { cp = ((uint16_t)(c & 0x1F) << 6) | ((uint8_t)*++s & 0x3F); s++; }
    else if ((c & 0xF0) == 0xE0) {
      cp = ((uint16_t)(c & 0x0F) << 12) | (((uint8_t)*++s & 0x3F) << 6);
      cp |= ((uint8_t)*++s & 0x3F); s++;
    } else { s++; continue; }
    drawCJKChar(x, y, cp, color);
    x += CN_FONT_SIZE;
  }
}

// 自绘 ASCII（带颜色：选中高亮行上也不会白字白底）
static void drawAscii(int x, int y, const char* s, int color) {
  while (*s) {
    uint8_t c = (uint8_t)*s;
    if (c >= GPGFX_FONT_CHAR_OFFSET && c < GPGFX_FONT_CHAR_OFFSET + 96) {
      const uint8_t* g = &GP_Font_Standard[(c - GPGFX_FONT_CHAR_OFFSET) * 5];
      for (int col = 0; col < 5; col++) {
        uint8_t byte = g[col];
        for (int row = 0; row < 8; row++) {
          if (byte & (1 << row)) px(x + col, y + row, color);
        }
      }
    }
    x += 6;
    s++;
  }
}

static bool isAsciiStr(const char* s) {
  for (const char* p = s; *p; p++) if ((uint8_t)*p >= 0x80) return false;
  return true;
}

static void drawValue(int rightX, int y, const char* s, int color) {
  if (isAsciiStr(s)) {
    int w = (int)strlen(s) * 6;
    drawAscii(rightX - w, y + 2, s, color);
  }
  else drawCJK(rightX - cjkWidth(s), y, s, color);
}

// ---- 24x24 1bpp 页面图标（Lucide stroke-width=2；MSB first，每行 3 字节）----
static const uint8_t ICON_ONBOARD[72] = {
  0x00,0x00,0x00,0x00,0x3C,0x00,0x00,0xFF,0x00,0x03,0xE7,0xC0,0x1F,0x81,0xF8,0x3C,0x00,0x3C,0x70,0x00,0x0E,0x70,0x00,0x0E,0x3C,0x00,0x3C,0x1F,0x81,0xF8,0x03,0xE7,0xC0,0x60,0xFF,0x06,0x70,0x3C,0x0E,0x3C,0x00,0x3C,0x1F,0x81,0xF8,0x03,0xE7,0xC0,0x60,0xFF,0x06,0x70,0x3C,0x0E,0x3C,0x00,0x3C,0x1F,0x81,0xF8,0x03,0xE7,0xC0,0x00,0xFF,0x00,0x00,0x3C,0x00,0x00,0x00,0x00
};

static const uint8_t ICON_BACKKEY[72] = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0xFF,0xF0,0x1F,0xFF,0xF8,0x38,0x00,0x1C,0x30,0x00,0x0C,0x31,0x80,0x0C,0x31,0x80,0x6C,0x77,0xE0,0x6E,0x67,0xE3,0x06,0x61,0x83,0x06,0x61,0x80,0x06,0x60,0x00,0x06,0x60,0xFF,0x06,0x61,0xFF,0x86,0x73,0x81,0xCE,0x3F,0x00,0xFC,0x1E,0x00,0x78,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static const uint8_t ICON_STICK[72] = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3C,0x00,0x00,0x7E,0x00,0x00,0xE7,0x00,0x00,0xC3,0x00,0x00,0xC3,0x00,0x00,0xE7,0x00,0x00,0x7E,0x00,0x00,0x3C,0x00,0x00,0x18,0x00,0x00,0x18,0x00,0x06,0x18,0x00,0x06,0x18,0x00,0x1F,0xFF,0xF8,0x3F,0xFF,0xFC,0x30,0x00,0x0C,0x30,0x00,0x0C,0x30,0x00,0x0C,0x30,0x00,0x0C,0x3F,0xFF,0xFC,0x1F,0xFF,0xF8,0x00,0x00,0x00,0x00,0x00,0x00
};

static const uint8_t ICON_HARDWARE[72] = {
  0x00,0x00,0x00,0x00,0x3C,0x00,0x00,0x7E,0x00,0x00,0x66,0x00,0x00,0xE7,0x00,0x1F,0xC3,0xF8,0x3F,0xC3,0xFC,0x30,0x00,0x0C,0x30,0x3C,0x0C,0x38,0x7E,0x1C,0x1C,0xE7,0x38,0x0C,0xC3,0x30,0x0C,0xC3,0x30,0x1C,0xE7,0x38,0x38,0x7E,0x1C,0x30,0x3C,0x0C,0x30,0x00,0x0C,0x3F,0xC3,0xFC,0x1F,0xC3,0xF8,0x00,0xE7,0x00,0x00,0x66,0x00,0x00,0x7E,0x00,0x00,0x3C,0x00,0x00,0x00,0x00
};

static const uint8_t ICON_FUNC[72] = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x06,0x00,0x3F,0xE7,0xFC,0x3F,0xE7,0xFC,0x00,0x06,0x00,0x00,0x06,0x00,0x00,0x00,0x00,0x01,0x80,0x00,0x01,0x80,0x00,0x3F,0x9F,0xFC,0x3F,0x9F,0xFC,0x01,0x80,0x00,0x01,0x80,0x00,0x00,0x00,0x00,0x00,0x01,0x80,0x00,0x01,0x80,0x3F,0xF9,0xFC,0x3F,0xF9,0xFC,0x00,0x01,0x80,0x00,0x01,0x80,0x00,0x00,0x00,0x00,0x00,0x00
};

static const uint8_t ICON_LIGHT[72] = {
  0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0xFF,0x00,0x01,0xC3,0x80,0x03,0x81,0xC0,0x07,0x00,0xE0,0x06,0x00,0x60,0x06,0x00,0x60,0x06,0x00,0x60,0x06,0x00,0x60,0x07,0x00,0xE0,0x03,0x81,0xC0,0x01,0xC3,0x80,0x00,0xC3,0x00,0x00,0xC3,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00
};

static const uint8_t* const PAGE_ICONS[NUM_PAGES] = {ICON_ONBOARD, ICON_BACKKEY, ICON_STICK,
                                                       ICON_HARDWARE, ICON_FUNC, ICON_LIGHT};

static void drawIconBmp24(int x, int y, const uint8_t* bmp) {
  for (int row = 0; row < 24; row++)
    for (int col = 0; col < 24; col++)
      if (bmp[row * 3 + (col >> 3)] & (0x80 >> (col & 7)))
        px(x + col, y + row, 1);
}

// 折角选择框：固定框住中间图标（图标区 52..75 x 16..39，框比图标大 3px）
static void drawSelectBrackets() {
  fill(49, 13, 6, 2, 1); fill(49, 13, 2, 6, 1);   // 左上
  fill(73, 13, 6, 2, 1); fill(77, 13, 2, 6, 1);   // 右上
  fill(49, 41, 6, 2, 1); fill(49, 37, 2, 6, 1);   // 左下
  fill(73, 41, 6, 2, 1); fill(77, 37, 2, 6, 1);   // 右下
}

// 左右实心箭头（纯静态提示，无任何逻辑）：左下 ◀ 尖(3,59) 底边 x=9 y55..63，右下 ▶ 镜像
static void drawNavArrows() {
  for (int y = 55; y <= 63; y++) {
    int inset = ((y > 59 ? y - 59 : 59 - y) * 6 + 2) / 4;
    for (int x = 3 + inset; x <= 9; x++) px(x, y, 1);
    for (int x = 118; x <= 124 - inset; x++) px(x, y, 1);
  }
}

// 页面图标条：24px 图标、间距 44，一屏 3 个，中间为当前页；off 由左右滑动动画提供
static void drawPageStrip(int off) {
  for (int k = -2; k <= 2; k++) {
    int idx = (page + k + NUM_PAGES) % NUM_PAGES;
    drawIconBmp24(64 + k * 44 - 12 + off, 16, PAGE_ICONS[idx]);
  }
}

static const char* optValueText(const LiteOpt* o) {
  static char buf[12];
  int v = o->get();
  if (o->type == OPT_ENUM) {
    int i = v - o->min;
    return (i >= 0 && i < o->nameCount) ? o->names[i] : "?";
  }
  if (o->type == OPT_BOOL) return v ? "开启" : "关闭";
  if (o->type == OPT_RESERVED) return "预留";
  if (o->type == OPT_DEC) snprintf(buf, sizeof(buf), "%d.%d%s", v / 10, v % 10, o->unit);
  else snprintf(buf, sizeof(buf), "%d%s", v, o->unit);
  return buf;
}

static void drawSlider(int x, int y, int w, int v, int max, int color) {
  fill(x, y + 3, w, 1, color);             // track
  int tw = (max > 0) ? (w - 4) * v / max : 0;
  if (tw > 0) fill(x, y + 3, tw + 2, 1, color);
  fill(x + tw + 1, y + 1, 3, 5, color);    // thumb
}

void GPFusionMenuScreen::init() {
  page = 0; level = 0; sel = 0; scroll = 0;
  dirty = false; confirmOpen = false; calibPromptOpen = false; animating = false;
  needsReboot = false; calibDone = false;
  resetListAnim();
  lastSavedInputMode = (int)GOP().inputMode;
  prevB = 0; prevD = 0;
  lastB1Act = 0; lastB2Act = 0;
  getRenderer()->clearScreen();
}

void GPFusionMenuScreen::shutdown() {
}

static void slideTo(int dir) {
  page = (page + dir + NUM_PAGES) % NUM_PAGES;
  animFrom = dir * 44;   // 图标条滑动：一页 = 44px
  animTo = 0;
  animStart = getMillis();
  animating = true;
  level = 0; sel = 0; scroll = 0;
  resetListAnim();
}

static void backOne() {
  level = 0; sel = 0; scroll = 0;
  resetListAnim();
  dirty = false;
}

int8_t GPFusionMenuScreen::update() {
  R = getRenderer();
  Gamepad* gamepad = Storage::getInstance().GetGamepad();
  uint16_t b = gamepad->state.buttons;
  // 用原始物理方向：D-Pad 被设为摇杆模式时 state.dpad 会被清空，菜单就收不到左右
  uint8_t d = gamepad->state.dpadOriginal;
  uint16_t bEdge = b & ~prevB;
  uint8_t dEdge = d & ~prevD;
  prevB = b; prevD = d;

  unsigned long now = getMillis();

  // B1/B2 消抖冷却：500ms 内同一按键的重复边沿（机械抖动导致）被忽略，
  // 防止按住时因抖动产生多重边沿触发连续返回/确认
  if (bEdge & GAMEPAD_MASK_B1) {
    if (now - lastB1Act < BTN_COOLDOWN_MS) bEdge &= ~GAMEPAD_MASK_B1;
    else lastB1Act = now;
  }
  if (bEdge & GAMEPAD_MASK_B2) {
    if (now - lastB2Act < BTN_COOLDOWN_MS) bEdge &= ~GAMEPAD_MASK_B2;
    else lastB2Act = now;
  }

  // slide animation tick
  if (animating) {
    unsigned long dt = now - animStart;
    if (dt >= 140) {
      animating = false;
    }
  }
  if (listAnim) {
    unsigned long dt = now - listAnimStart;
    float t = (dt >= 140) ? 1.0f : (float)dt / 140.0f;
    float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    selY = selYFrom + (selYTo - selYFrom) * e;
    scrollPx = scrollPxFrom + (scrollPxTo - scrollPxFrom) * e;
    if (t >= 1.0f) listAnim = false;
  }

  // 校准完成提示框：按 B1 或 B2 关闭
  if (calibPromptOpen) {
    if (bEdge & (GAMEPAD_MASK_B1 | GAMEPAD_MASK_B2)) {
      calibPromptOpen = false;
    }
    return -1;
  }

  if (confirmOpen) {
    if (dEdge & 0x0C) confirmChoice = 1 - confirmChoice;
    if (bEdge & GAMEPAD_MASK_B2) {  // B2=确定
      confirmOpen = false;
      if (confirmChoice == 0) {
        EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true));
        maybeRebootIfNeeded();
        dirty = false;
      } else {
        undoCalibAndFlags();
        dirty = false;
      }
      backOne();
    } else if (bEdge & GAMEPAD_MASK_B1) {  // B1=取消
      confirmOpen = false;
      undoCalibAndFlags();
      dirty = false;
      backOne();
    }
    return -1;
  }

  // repeat for held up/down/left/right
  uint8_t held = d & 0x0F;
  if (held && now - lastRepeat >= (repeatMask == held ? 160 : 420)) {
    lastRepeat = now;
    repeatMask = held;
    dEdge |= held;   // synthesize repeats
  }
  if (!held) { repeatMask = 0; lastRepeat = 0; }

  if (level == 0) {
    if (dEdge & 0x04) slideTo(-1);   // LEFT
    if (dEdge & 0x08) slideTo(+1);   // RIGHT
    if (bEdge & GAMEPAD_MASK_B2) {   // B2=确定 enter
      level = 1; sel = 0; scroll = 0;
      snapshot(); dirty = false; needsReboot = false; calibDone = false;
      resetListAnim();
    }
    if (bEdge & GAMEPAD_MASK_B1) {   // B1=取消 exit menu
      return DisplayMode::BUTTONS;
    }
  } else { // options
    LiteSection& s = curSection();
    if (dEdge & 0x01) { // UP（含从顶部回绕到底部）
      sel = (sel == 0) ? s.count - 1 : sel - 1;
      int ts = scroll;
      if (sel < ts) ts = sel;
      if (sel >= ts + 4) ts = sel - 3;
      if (ts > s.count - 4) ts = s.count - 4;
      if (ts < 0) ts = 0;
      scroll = ts;
      startListAnim(sel, scroll);
    }
    if (dEdge & 0x02) { // DOWN（含从底部回绕到顶部）
      sel = (sel + 1) % s.count;
      int ts = scroll;
      if (sel < ts) ts = sel;
      if (sel >= ts + 4) ts = sel - 3;
      if (ts > s.count - 4) ts = s.count - 4;
      if (ts < 0) ts = 0;
      scroll = ts;
      startListAnim(sel, scroll);
    }
    LiteOpt* o = &s.opts[sel];
    if (dEdge & 0x0C) {
      int dir = (dEdge & 0x04) ? -1 : 1;
      if (o->type != OPT_ACTION && o->type != OPT_RESERVED) {
        int v = o->get() + dir * ((o->type == OPT_INT || o->type == OPT_SLIDER || o->type == OPT_DEC) ? o->step : 1);
        if (v < o->min) v = o->min;
        if (v > o->max) v = o->max;
        o->set(v);
        dirty = true;
      }
    }
    if (bEdge & GAMEPAD_MASK_B2) {  // B2=确定
      if (o->type == OPT_ACTION) {
        dirty = (o->get() != 0);
        // 校准完成后弹出提示框
        if (calibDone) calibPromptOpen = true;
      }
    }
    if (bEdge & GAMEPAD_MASK_B1) {  // B1=取消
      if (dirty) { confirmOpen = true; confirmChoice = 0; }
      else backOne();
    }
  }
  return -1;
}

static void drawMenuPages() {
  int off = 0;
  if (animating) {
    unsigned long dt = getMillis() - animStart;
    float t = (dt >= 140) ? 1.0f : (float)dt / 140.0f;
    float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    off = animFrom + (int)((animTo - animFrom) * e);
  }
  drawCJK(64, 52, PAGE_TITLES[page], 1, true);
  drawPageStrip(off);
  drawSelectBrackets();
  drawNavArrows();
}

static void drawOptions() {
  LiteSection& s = curSection();
  // 选择框要跟随可视区：selY 是绝对位置，减去 scrollPx 才是屏幕上的位置
  fill(0, (int)(selY - scrollPx), 128, 12, 1);
  for (int i = 0; i < s.count; i++) {
    int y = i * 13 - (int)scrollPx;
    if (y < -12 || y > 52) continue;
    LiteOpt* o = &s.opts[i];
    drawCJK(2, y, o->label, i == sel ? 0 : 1);
    if (o->type == OPT_SLIDER) {
      drawSlider(70, y + 4, 48, o->get(), o->max, i == sel ? 0 : 1);
    } else {
      if (o->type == OPT_ACTION) drawAscii(122, y + 2, ">", i == sel ? 0 : 1);
      else drawValue(124, y, optValueText(o), i == sel ? 0 : 1);
    }
  }
  // scrollbar
  if (s.count > 4) {
    int th = 52 * 4 / s.count; if (th < 6) th = 6;
    float range = (float)((s.count - 4) * 13);
    int ty = (range > 0 ? (int)(scrollPx / range * (52 - th)) : 0);
    fill(126, 0, 2, 52, 1);
    fill(126, ty, 2, th, 1);
  }
}

void GPFusionMenuScreen::drawScreen() {
  R = getRenderer();
  fill(0, 0, 128, 64, 0);
  if (calibPromptOpen) {
    fill(20, 16, 88, 36, 0);
    fill(21, 17, 86, 34, 1);
    fill(23, 19, 82, 30, 0);
    drawCJK(64, 30, "校准已保存", 1, true);
    return;
  }
  if (confirmOpen) {
    fill(24, 18, 80, 36, 0);
    fill(25, 19, 78, 34, 1);
    fill(27, 21, 74, 30, 0);
    drawCJK(64, 24, "是否立即保存", 1, true);
    if (confirmChoice == 0) {
      fill(48, 40, 20, 12, 1);
      drawCJK(58, 40, "是", 0, true);
      drawCJK(82, 40, "否", 1, true);
    } else {
      drawCJK(58, 40, "是", 1, true);
      fill(72, 40, 20, 12, 1);
      drawCJK(82, 40, "否", 0, true);
    }
    return;
  }
  if (level == 0) drawMenuPages();
  else drawOptions();
}
