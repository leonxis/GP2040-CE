#include "addons/turbo.h"

#include "addons/hml_back_key.h"
#include "addons/linear_trigger.h"
#include "gamepad.h"
#include "storagemanager.h"
#include "config.pb.h"

#include <algorithm>
#include <cmath>

#define TURBO_LOOP_OFFSET 50 // Extra time to compensate for loop runtime variance, turbo lags a bit otherwise

TurboInput::TurboInput(HmlBackKeyAddon* backKeys, LinearTriggerAddon* linearTrigger)
    : backKeys(backKeys), linearTrigger(linearTrigger) {}

bool TurboInput::available() {
    // 背键插件在 HML2354 上恒可用，Turbo 键可映射于背键/FN/MT；
    // 不再要求标准 GPIO 必须存在 TURBO 映射（SHMUP always-on 亦可独立工作）
    return Storage::getInstance().getAddonOptions().turboOptions.enabled;
}

void TurboInput::setup() {
    reinit();  // 重建 turboPinMask（先清零）
    turboGpioMask = 0;
    longpressGpioMask = 0;
    longpressActiveMask = 0;
    lastHeldGpios = 0;
    lastDpad = 0;
    bTurboFlicker = false;
    nextTimer = getMicro();
    updateTurboShotCount(Storage::getInstance().getAddonOptions().turboOptions.shotCount, false);
}

void TurboInput::reinit() {
    turboPinMask = 0;  // 修复旧实现未清零导致的掩码累积问题
    GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
        if (pinMappings[pin].action == GpioAction::BUTTON_PRESS_TURBO) {
            turboPinMask |= Mask_t{1} << pin;
        }
    }
}

TurboResolvedAction TurboInput::resolvePinToAction(Pin_t pin) {
    TurboResolvedAction action = {};
    Mask_t bit = Mask_t{1} << pin;

    // 背键 10 键（含 FN/MT）：取当前 preset 的三通道解析结果（HML2354 背键恒载，无需判空）
    const ActionMappingCommon::ActionMappingEntry* entry = backKeys->getActionForPin(pin);
    if (entry != nullptr) {
        action.buttonMask = entry->buttonMask;
        action.keyboardMask = entry->keyboardMask;
        action.mouseMask = entry->mouseButtonMask;
        return action;
    }

    // 线性扳机模拟模式：GPIO40/41 固定为 L2/R2 通道语义
    if (linearTrigger != nullptr && linearTrigger->isActive()) {
        if (pin == LINEAR_L2_PIN) { action.buttonMask = GAMEPAD_MASK_L2; return action; }
        if (pin == LINEAR_R2_PIN) { action.buttonMask = GAMEPAD_MASK_R2; return action; }
    }

    // 标准 GPIO：遍历 14 个游戏按键映射（mapButton* 指针逐帧跟随热切层，天然满足映射跟随）
    // 键盘/鼠标映射的解析于第二阶段补充
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad != nullptr) {
        GamepadButtonMapping* buttonMaps[14] = {
            gamepad->mapButtonB1, gamepad->mapButtonB2, gamepad->mapButtonB3, gamepad->mapButtonB4,
            gamepad->mapButtonL1, gamepad->mapButtonR1, gamepad->mapButtonL2, gamepad->mapButtonR2,
            gamepad->mapButtonS1, gamepad->mapButtonS2, gamepad->mapButtonL3, gamepad->mapButtonR3,
            gamepad->mapButtonA1, gamepad->mapButtonA2,
        };
        for (GamepadButtonMapping* m : buttonMaps) {
            if (m != nullptr && (m->pinMask & bit)) {
                action.buttonMask |= m->buttonMask;
            }
        }
    }
    return action;
}

void __not_in_flash_func(TurboInput::process)() {
    Gamepad* gamepad = Storage::getInstance().GetGamepad();
    if (gamepad == nullptr) return;
    TurboOptions& options = Storage::getInstance().getAddonOptions().turboOptions;

    // 热键等外部途径修改射速 → 重新钳位并计算闪烁间隔
    if (options.shotCount != lastShotCount) {
        updateTurboShotCount(options.shotCount, false);
    }

    // 长按开关运行时关闭（前端保存不重启）：立即清空长按闩锁。
    // 否则残留 GPIO 物理松开后仍持续恒按输出，且首次按连发键切换实际走
    // "长按→关"分支（绿灯亮但未进入连发），表现为该键无法连发
    if (!options.longPressEnabled && longpressGpioMask != 0) {
        longpressGpioMask = 0;
        longpressActiveMask = 0;
    }

    // ---- 阶段一：合成当前按下 GPIO（板载数字 + 背键 10 键 + 线性扳机模拟通道）----
    Mask_t heldGpios = gamepad->debouncedGpio | backKeys->getPressedGpioMask();
    if (linearTrigger != nullptr && linearTrigger->isActive()) {
        // 模拟模式：剥离 40/41 数字电平（防 ADC 阈值附近抖动），以硬件换算值合成按下态
        heldGpios &= ~((Mask_t{1} << LINEAR_L2_PIN) | (Mask_t{1} << LINEAR_R2_PIN));
        if (linearTrigger->getHwL2() > 0) heldGpios |= Mask_t{1} << LINEAR_L2_PIN;
        if (linearTrigger->getHwR2() > 0) heldGpios |= Mask_t{1} << LINEAR_R2_PIN;
    }

    // ---- SHMUP 常开连发：配置项直接为 GPIO 引脚号（0 = 未设置）----
    // 引脚与按键的对应关系由 BoardConfig 映射决定，输出经 resolvePinToAction 解析，
    // 与 turboGpioMask 走同一连发路径
    Mask_t shmupGpioMask = 0;
    if (options.shmupModeEnabled) {
        const uint32_t shmupPins[4] = {
            options.shmupAlwaysOn1, options.shmupAlwaysOn2,
            options.shmupAlwaysOn3, options.shmupAlwaysOn4,
        };
        for (uint32_t pin : shmupPins) {
            if (pin > 0 && pin < (uint32_t)NUM_BANK0_GPIOS) {
                shmupGpioMask |= Mask_t{1} << pin;
            }
        }
    }

    // ---- 切换检测：Turbo 键（标准 GPIO ∪ 背键 10 键映射）按住期间 ----
    Mask_t turboKeyMask = turboPinMask | backKeys->getTurboKeyMask();
    if (heldGpios & turboKeyMask) {
        // 白名单 GPIO 新按下沿（Turbo 键自身不可被切换；SHMUP 常开引脚恒为连发，不参与切换）
        Mask_t newGpios = (heldGpios ^ lastHeldGpios) & heldGpios
                        & TURBO_GPIO_WHITELIST_MASK & ~turboKeyMask & ~shmupGpioMask;
        // 灯效事件：1=连发开 2=长按开 3=关（同帧多键切换以最后事件为准，单次反馈）
        uint8_t pulseEvent = 0;
        for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS && newGpios; pin++) {
            Mask_t bit = Mask_t{1} << pin;
            if (!(newGpios & bit)) continue;
            // 切换资格（第一阶段）：仅 BUTTONS 分组（解析出游戏按键掩码）；
            // 方向/热键/键盘/鼠标等映射无资格（键盘/鼠标于第二阶段开放）
            if (resolvePinToAction(pin).buttonMask != 0) {
                if (turboGpioMask & bit) {            // 连发 → 长按（长按开关关闭时 → 关）
                    turboGpioMask &= ~bit;
                    if (options.longPressEnabled) { longpressGpioMask |= bit; pulseEvent = 2; }
                    else                           pulseEvent = 3;
                } else if (longpressGpioMask & bit) { // 长按 → 关（同步解除长按输出）
                    longpressGpioMask &= ~bit;
                    longpressActiveMask &= ~bit;
                    pulseEvent = 3;
                } else {                              // 关 → 连发
                    turboGpioMask |= bit;
                    pulseEvent = 1;
                }
            }
            newGpios &= ~bit;
        }
        if (pulseEvent != 0) {
            // WS2812 环境灯一次性反馈（storagemanager seq + Core1 相位机）：
            // 连发开=绿 1 闪，长按开=绿 2 闪，切回正常=黄 1 闪
            Storage& storage = Storage::getInstance();
            if (pulseEvent == 1)      storage.pulseTurboToggle();
            else if (pulseEvent == 2) storage.pulseTurboLongpress();
            else                      storage.pulseTurboOff();
            bTurboFlicker = false;
        }

        // 射速调节：方向键上/下位边沿，1~50；绿灯=+1、黄灯=-1（复用切换反馈灯效）
        uint8_t dpadPressed = gamepad->state.dpad & GAMEPAD_MASK_DPAD;
        if (dpadPressed != lastDpad) {
            if ((dpadPressed & GAMEPAD_MASK_DOWN) && !(lastDpad & GAMEPAD_MASK_DOWN)) {
                if (options.shotCount > TURBO_SHOT_MIN) {
                    updateTurboShotCount(options.shotCount - 1, true);
                    Storage::getInstance().pulseTurboOff();    // -1：黄 1 闪
                }
            } else if ((dpadPressed & GAMEPAD_MASK_UP) && !(lastDpad & GAMEPAD_MASK_UP)) {
                if (options.shotCount < TURBO_SHOT_MAX) {
                    updateTurboShotCount(options.shotCount + 1, true);
                    Storage::getInstance().pulseTurboToggle(); // +1：绿 1 闪
                }
            }
            lastDpad = dpadPressed;
        }
    } else {
        lastDpad = 0;
    }

    // 长按态 GPIO 单独点击：物理按下沿翻转长按输出闩锁（触发/松开）。
    // 连发键按住期间的按下沿属于三态切换操作，不触发翻转
    if (longpressGpioMask != 0) {
        Mask_t lpEdges = (heldGpios ^ lastHeldGpios) & heldGpios & longpressGpioMask;
        if (heldGpios & turboKeyMask) lpEdges = 0;
        longpressActiveMask ^= lpEdges;
    }
    lastHeldGpios = heldGpios;

    // ---- 闪烁计时 ----
    uint64_t now = getMicro();
    if (nextTimer < now) {
        bTurboFlicker ^= true;
        nextTimer = now + uIntervalUS - TURBO_LOOP_OFFSET;
    }

    // ---- 阶段二A：按钮通道分类（第一阶段仅游戏按键，键鼠通道第二阶段实施）----
    uint32_t turboBtns = 0, lpBtns = 0, otherBtns = 0, shmupBtns = 0;
    // 长按闩锁激活的 GPIO 物理松开后仍持续贡献；SHMUP 常开引脚无物理按下亦须参与分类
    Mask_t classify = heldGpios | longpressActiveMask | shmupGpioMask;
    for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
        Mask_t bit = Mask_t{1} << pin;
        if (!(classify & bit)) continue;
        TurboResolvedAction act = resolvePinToAction(pin);
        bool inShmup = (shmupGpioMask & bit) != 0;
        if ((turboGpioMask & bit) || inShmup) {
            turboBtns |= act.buttonMask;
            if (inShmup) shmupBtns |= act.buttonMask;
        }
        else if (longpressGpioMask & bit) {
            // 长按输出闩锁激活：恒定输出（长触发）；未激活但物理按下：正常输出（点击触发的按住过程）
            if (longpressActiveMask & bit)     lpBtns |= act.buttonMask;
            else if (heldGpios & bit)          otherBtns |= act.buttonMask;
        }
        else                               otherBtns |= act.buttonMask;
    }

    // ---- 阶段二B：或集干预 ----
    uint32_t managed = turboBtns | lpBtns;
    if (managed == 0) return;

    uint32_t steady  = lpBtns | otherBtns;          // 恒定输出：长按闩锁 + 其他 GPIO 来源
    uint32_t flicker = turboBtns;                   // 跟随闪烁：连发绑定 + SHMUP 常开引脚
    uint32_t result  = steady | (bTurboFlicker ? 0 : flicker);

    gamepad->state.buttons |= lpBtns;               // 长按强制置位（或集）
    // 普通连发的点亮相位依赖物理按下已置位；SHMUP 常开引脚无物理输入，须主动置位
    if (!bTurboFlicker) gamepad->state.buttons |= shmupBtns;
    // 管辖位且无任何来源贡献才清除；掩码走 uint64 防 state.buttons 高位被误清
    gamepad->state.buttons &= ~(uint64_t)(managed & ~result);

    // 模拟扳机跟随管辖位（数字模式普通映射 / 线性扳机模拟模式统一适用）
    if (managed & GAMEPAD_MASK_L2) {
        if (result & GAMEPAD_MASK_L2) {
            if ((lpBtns | shmupBtns) & GAMEPAD_MASK_L2) gamepad->state.lt = GAMEPAD_TRIGGER_MAX;
        } else {
            gamepad->state.lt = 0;
        }
    }
    if (managed & GAMEPAD_MASK_R2) {
        if (result & GAMEPAD_MASK_R2) {
            if ((lpBtns | shmupBtns) & GAMEPAD_MASK_R2) gamepad->state.rt = GAMEPAD_TRIGGER_MAX;
        } else {
            gamepad->state.rt = 0;
        }
    }
}

void TurboInput::updateTurboShotCount(uint8_t shotCount, bool save) {
    TurboOptions& options = Storage::getInstance().getAddonOptions().turboOptions;
    shotCount = std::clamp<uint8_t>(shotCount, TURBO_SHOT_MIN, TURBO_SHOT_MAX);

    options.shotCount = shotCount;
    lastShotCount = shotCount;

    if (save) {
        EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(false));
    }

    uIntervalUS = (uint32_t)std::floor(1000000.0 / (shotCount * 2));
}
