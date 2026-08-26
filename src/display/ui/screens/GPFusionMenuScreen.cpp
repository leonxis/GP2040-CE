// GP-Fusion Lite 滑动菜单：设置 / 灯光
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
#include "addons/ads8332_adc.h"

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
static void sGyro(int v) { AOP2().lsm6dsrOptions.enabled = v ? true : false; needsReboot = true; }

// 摇杆死区/反死区（原始值0-200，对应0.0%-20.0%，0.1%步进）
static int gInnerDz() { return (int)AOP2().analogOptions.inner_deadzone; }
static void sInnerDz(int v) { AOP2().analogOptions.inner_deadzone = (uint32_t)v; needsReboot = true; }
static int gAntiDz() { return (int)AOP2().analogOptions.anti_deadzone; }
static void sAntiDz(int v) { AOP2().analogOptions.anti_deadzone = (uint32_t)v; needsReboot = true; }
static int gInnerDz2() { return (int)AOP2().analogOptions.inner_deadzone2; }
static void sInnerDz2(int v) { AOP2().analogOptions.inner_deadzone2 = (uint32_t)v; needsReboot = true; }
static int gAntiDz2() { return (int)AOP2().analogOptions.anti_deadzone2; }
static void sAntiDz2(int v) { AOP2().analogOptions.anti_deadzone2 = (uint32_t)v; needsReboot = true; }

// 读取摇杆原始ADC值（尝试三种源）
static bool readRawStick(uint8_t stickNum, uint16_t& rawX, uint16_t& rawY) {
    uint16_t xCenter, yCenter, adcMax;
    bool xValid, yValid;
    if (AnalogInput::getRawStickForProcessor(stickNum, rawX, rawY, xCenter, yCenter, xValid, yValid, adcMax))
        return xValid && yValid;
    if (MCP3208ADCAddon::getRawStickForProcessor(stickNum, rawX, rawY, xCenter, yCenter, xValid, yValid, adcMax))
        return xValid && yValid;
    if (ADS8332ADCAddon::getRawStickForProcessor(stickNum, rawX, rawY, xCenter, yCenter, xValid, yValid, adcMax))
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

// 按键配置档动态上限：gpioMappingsSets_count + 1（默认配置档1 + 自定义预设数）
static int gProfileMax() {
  return (int)Storage::getInstance().getProfileOptions().gpioMappingsSets_count + 1;
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

// 预留项的空 getter/setter
static int gReserved() { return 0; }
static void sReserved(int) {}

static LiteOpt optConfig[] = {
  {"输入模式", OPT_ENUM, 0, 17, 1, N_INPUT, 18, "", gInput, sInput},
  {"按键配置档", OPT_INT, 1, 1, 1, NULL, 0, "", gProfile, sProfile},
  {"背键配置档", OPT_INT, 1, 3, 1, NULL, 0, "", gBackPreset, sBackPreset},
  {"保存设置", OPT_ACTION, 0, 0, 0, NULL, 0, "", gSave, sSave},
  {"恢复默认", OPT_ACTION, 0, 0, 0, NULL, 0, "", gReset, sReset},
};
static LiteOpt optHandle[] = {
  {"陀螺仪", OPT_BOOL, 0, 1, 1, NULL, 0, "", gGyro, sGyro},
  {"十字键模式", OPT_ENUM, 0, 2, 1, N_DPAD, 3, "", gDpad, sDpad},
  {"背键映射", OPT_RESERVED, 0, 0, 0, NULL, 0, "", gReserved, sReserved},
};
static LiteOpt optStick[] = {
  {"校准", OPT_ACTION, 0, 0, 0, NULL, 0, "", gCalib, sCalib},
  {"左摇杆死区", OPT_DEC, 0, 200, 1, NULL, 0, "%", gInnerDz, sInnerDz},
  {"左摇杆反死区", OPT_DEC, 0, 200, 1, NULL, 0, "%", gAntiDz, sAntiDz},
  {"右摇杆死区", OPT_DEC, 0, 200, 1, NULL, 0, "%", gInnerDz2, sInnerDz2},
  {"右摇杆反死区", OPT_DEC, 0, 200, 1, NULL, 0, "%", gAntiDz2, sAntiDz2},
};
static LiteOpt optFunc[] = {
  {"SOCD模式", OPT_ENUM, 0, 4, 1, N_SOCD, 5, "", gSocd, sSocd},
  {"四向模式", OPT_BOOL, 0, 1, 1, NULL, 0, "", gFour, sFour},
  {"反向X", OPT_BOOL, 0, 1, 1, NULL, 0, "", gInvX, sInvX},
  {"反向Y", OPT_BOOL, 0, 1, 1, NULL, 0, "", gInvY, sInvY},
  {"去抖延迟", OPT_INT, 1, 20, 1, NULL, 0, "ms", gDebounce, sDebounce},
};
static LiteOpt optLed[] = {
  {"动画模式", OPT_ENUM, 0, 3, 1, N_ANIM, 4, "", gAnimMode, sAnimMode},
  {"环境光亮度", OPT_INT, 0, 100, 1, NULL, 0, "%", gAmbientBright, sAmbientBright},
  {"静态颜色", OPT_ENUM, 0, 15, 1, N_COLOR, 16, "", gColor, sColor},
  {"渐变速度", OPT_SLIDER, 1, 10, 1, NULL, 0, "", gGradientSpd, sGradientSpd},
  {"追逐速度", OPT_SLIDER, 0, 100, 1, NULL, 0, "", gChaseSpd, sChaseSpd},
  {"呼吸速度", OPT_SLIDER, 1, 10, 1, NULL, 0, "", gBreathSpd, sBreathSpd},
  {"挂起关灯", OPT_BOOL, 0, 1, 1, NULL, 0, "", gLedOff, sLedOff},
};

static LiteSection secSettings[] = {
  {"配置", optConfig, 5},
  {"手柄", optHandle, 3},
  {"摇杆", optStick, 5},
  {"功能", optFunc, 5},
};
static LiteSection secLed[] = {{"彩灯", optLed, 7}};

static const char* const PAGE_TITLES[] = {"设置", "灯光"};
static const int NUM_PAGES = 2;

// ---- screen state ----
static int page = 0;
static int level = 0;        // 0=页面 1=分区 2=选项
static int section = 0;
static int sel = 0;
static int scroll = 0;
static bool dirty = false;
static bool confirmOpen = false;
static int confirmChoice = 0;
static int snap[8];
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
static int animDir = 0;
static unsigned long animStart = 0;
static int animFrom = 0, animTo = 0;
static int oldPage = 0;

// 列表上下滑动动画
static float selY = 0, selYFrom = 0, selYTo = 0;
static float scrollPx = 0, scrollPxFrom = 0, scrollPxTo = 0;
static bool listAnim = false;
static unsigned long listAnimStart = 0;

static uint16_t prevB = 0;
static uint8_t prevD = 0;
static unsigned long lastRepeat = 0;
static uint8_t repeatMask = 0;

static LiteSection* currentSections() {
  switch (page) {
    case 0: return secSettings;
    case 1: return secLed;
    default: return NULL;
  }
}
static int sectionCount() {
  LiteSection* s = currentSections();
  if (!s) return 0;
  if (page == 0) return sizeof(secSettings) / sizeof(secSettings[0]);
  return 1;
}
static LiteSection& curSection() {
  return currentSections()[section];
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
  // 动态更新按键配置档上限为网页端已设置的预设数量
  int pmax = gProfileMax();
  optConfig[1].max = pmax > 0 ? pmax : 1;
  for (int i = 0; i < s.count && i < 8; i++) {
    if (s.opts[i].type == OPT_ACTION || s.opts[i].type == OPT_RESERVED) { snap[i] = 0; continue; }
    snap[i] = s.opts[i].get();
  }
}
static void restore() {
  LiteSection& s = curSection();
  for (int i = 0; i < s.count && i < 8; i++) {
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

static void drawDisc(int x, int y, int r, int c) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx * dx + dy * dy <= r * r) px(x + dx, y + dy, c);
}

static void drawLine(int x0, int y0, int x1, int y1, int c) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int dy = y1 > y0 ? y1 - y0 : y0 - y1;
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  while (true) {
    px(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 < dx) { err += dx; y0 += sy; }
  }
}

// 页面图标（单色）— 标题在 y=0，图标下移 12px 与标题间隔一行
static void iconSettings(int off) {          // 三条滑杆
  int ys[3] = {34, 42, 50};
  int kn[3] = {56, 72, 64};
  for (int i = 0; i < 3; i++) {
    drawLine(44 + off, ys[i], 84 + off, ys[i], 1);
    drawDisc(kn[i] + off, ys[i], 4, 1);
  }
}

static void iconLight(int off) {             // 灯泡
  drawDisc(64 + off, 41, 10, 1);
  drawLine(64 + off, 24, 64 + off, 29, 1);
  drawLine(54 + off, 29, 57 + off, 32, 1);
  drawLine(74 + off, 29, 71 + off, 32, 1);
  fill(59 + off, 51, 10, 5, 1);
  fill(63 + off, 56, 2, 4, 1);
}

static void drawPageIcon(int off, int p) {
  switch (p) {
    case 0: iconSettings(off); break;
    case 1: iconLight(off); break;
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
  page = 0; level = 0; section = 0; sel = 0; scroll = 0;
  dirty = false; confirmOpen = false; animating = false;
  needsReboot = false; calibDone = false;
  resetListAnim();
  lastSavedInputMode = (int)GOP().inputMode;
  prevB = 0; prevD = 0;
  getRenderer()->clearScreen();
}

void GPFusionMenuScreen::shutdown() {
}

static void slideTo(int dir) {
  oldPage = page;
  page = (page + dir + NUM_PAGES) % NUM_PAGES;
  animDir = dir;
  animFrom = dir * 128;
  animTo = 0;
  animStart = getMillis();
  animating = true;
  level = 0; section = 0; sel = 0; scroll = 0;
  resetListAnim();
}

static void backOne() {
  if (level == 2) {
    LiteSection* secs = currentSections();
    if (secs && page == 0) { level = 1; section = 0; sel = 0; scroll = 0; }
    else { level = 0; section = 0; sel = 0; scroll = 0; }
  } else {
    level = 0; section = 0; sel = 0; scroll = 0;
  }
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

  if (confirmOpen) {
    if (dEdge & 0x0C) confirmChoice = 1 - confirmChoice;
    if (bEdge & GAMEPAD_MASK_B1) {
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
    } else if (bEdge & GAMEPAD_MASK_B2) {
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
    if (bEdge & GAMEPAD_MASK_B1) {   // A enter
      if (sectionCount() > 1) { level = 1; section = 0; sel = 0; scroll = 0; resetListAnim(); }
      else { level = 2; section = 0; sel = 0; scroll = 0; snapshot(); dirty = false; needsReboot = false; calibDone = false; }
    }
    if (bEdge & GAMEPAD_MASK_B2) {   // B exit menu
      return DisplayMode::BUTTONS;
    }
  } else if (level == 1) {
    int cnt = sectionCount();
    if (dEdge & 0x01) sel = (sel + cnt - 1) % cnt;   // UP
    if (dEdge & 0x02) sel = (sel + 1) % cnt;         // DOWN
    if (dEdge & 0x03) startListAnim(sel, 0);
    if (bEdge & GAMEPAD_MASK_B1) { section = sel; level = 2; sel = 0; scroll = 0; resetListAnim(); snapshot(); dirty = false; needsReboot = false; calibDone = false; }
    if (bEdge & GAMEPAD_MASK_B2) { level = 0; sel = 0; resetListAnim(); }
  } else { // options
    LiteSection& s = curSection();
    if (dEdge & 0x01) { // UP（含从顶部回绕到底部）
      sel = (sel == 0) ? s.count - 1 : sel - 1;
      int ts = scroll;
      if (sel < ts) ts = sel;
      if (sel >= ts + 3) ts = sel - 2;
      if (ts > s.count - 3) ts = s.count - 3;
      if (ts < 0) ts = 0;
      scroll = ts;
      startListAnim(sel, scroll);
    }
    if (dEdge & 0x02) { // DOWN（含从底部回绕到顶部）
      sel = (sel + 1) % s.count;
      int ts = scroll;
      if (sel < ts) ts = sel;
      if (sel >= ts + 3) ts = sel - 2;
      if (ts > s.count - 3) ts = s.count - 3;
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
    if (bEdge & GAMEPAD_MASK_B1) {
      if (o->type == OPT_ACTION) { dirty = (o->get() != 0); }
    }
    if (bEdge & GAMEPAD_MASK_B2) {
      if (dirty) { confirmOpen = true; confirmChoice = 0; }
      else backOne();
    }
  }
  return -1;
}

static void drawPagePreview(int p, int off) {
  drawCJK(64 + off, 0, PAGE_TITLES[p], 1, true);
  drawPageIcon(off, p);
}

static void drawMenuPages() {
  int off = 0;
  if (animating) {
    unsigned long dt = getMillis() - animStart;
    float t = (dt >= 140) ? 1.0f : (float)dt / 140.0f;
    float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    off = animFrom + (int)((animTo - animFrom) * e);
  }
  drawPagePreview(page, off);
  if (animating) drawPagePreview(oldPage, off - animDir * 128);
}

static void drawSections() {
  drawCJK(64, 0, "设置", 1, true);
  int cnt = sectionCount();
  fill(0, 13 + (int)selY, 128, 12, 1);
  for (int i = 0; i < cnt; i++) {
    int y = 13 + i * 13;
    drawCJK(8, y, currentSections()[i].title, i == sel ? 0 : 1);
  }
}

static void drawOptions() {
  LiteSection& s = curSection();
  drawCJK(64, 0, s.title, 1, true);
  // 选择框要跟随可视区：selY 是绝对位置，减去 scrollPx 才是屏幕上的位置
  fill(0, 13 + (int)(selY - scrollPx), 128, 12, 1);
  for (int i = 0; i < s.count; i++) {
    int y = 13 + i * 13 - (int)scrollPx;
    if (y < 6 || y > 52) continue;
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
  if (s.count > 3) {
    int th = 40 * 3 / s.count; if (th < 6) th = 6;
    float range = (float)((s.count - 3) * 13);
    int ty = 13 + (range > 0 ? (int)(scrollPx / range * (39 - th)) : 0);
    fill(126, 13, 2, 39, 1);
    fill(126, ty, 2, th, 1);
  }
}

void GPFusionMenuScreen::drawScreen() {
  R = getRenderer();
  fill(0, 0, 128, 64, 0);
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
  else if (level == 1) drawSections();
  else drawOptions();
}
