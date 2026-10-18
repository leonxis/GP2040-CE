#ifndef TURBO_H_
#define TURBO_H_

#include "gpaddon.h"
#include "storagemanager.h"
#include "eventmanager.h"
#include "enums.pb.h"

#ifndef TURBO_ENABLED
#define TURBO_ENABLED 0
#endif

#ifndef DEFAULT_SHOT_PER_SEC
#define DEFAULT_SHOT_PER_SEC 10
#endif  // DEFAULT_SHOT_PER_SEC

// TURBO SHMUP MODE
#ifndef TURBO_SHMUP_MODE
#define TURBO_SHMUP_MODE 0
#endif

#ifndef SHMUP_ALWAYS_ON1
#define SHMUP_ALWAYS_ON1 0
#endif

#ifndef SHMUP_ALWAYS_ON2
#define SHMUP_ALWAYS_ON2 0
#endif

#ifndef SHMUP_ALWAYS_ON3
#define SHMUP_ALWAYS_ON3 0
#endif

#ifndef SHMUP_ALWAYS_ON4
#define SHMUP_ALWAYS_ON4 0
#endif

// Turbo Module Name
#define TurboName "Turbo"

// 连发 GPIO 白名单（18 个）：boardconfig 10 板载按键 GPIO（B1-B4/L1/R1/L2/R2/L3/R3）
// + 6 背键 GPIO（31,30,33,34,35,36）+ 左右 FN（45,44）。
// MT 键（46/43）不参与切换，但其输出参与或集；方向键等其余 GPIO 无切换资格。
#define TURBO_GPIO_WHITELIST_MASK ( \
        Mask_t{1} << 5  | Mask_t{1} << 7  | Mask_t{1} << 10 | Mask_t{1} << 15 | \
        Mask_t{1} << 17 | Mask_t{1} << 26 | Mask_t{1} << 30 | Mask_t{1} << 31 | \
        Mask_t{1} << 33 | Mask_t{1} << 34 | Mask_t{1} << 35 | Mask_t{1} << 36 | \
        Mask_t{1} << 37 | Mask_t{1} << 40 | Mask_t{1} << 41 | Mask_t{1} << 44 | \
        Mask_t{1} << 45 | Mask_t{1} << 47 )

#define TURBO_SHOT_MIN 1
#define TURBO_SHOT_MAX 50

class HmlBackKeyAddon;
class LinearTriggerAddon;

// GPIO 当前映射的三通道动作解析结果（键盘/鼠标通道于第二阶段消费）
struct TurboResolvedAction
{
    uint32_t buttonMask;    // 游戏按键（含背键自定义掩码位）
    uint64_t keyboardMask;  // 键盘键位（addonKeyboardKeyMask 位布局）
    uint8_t  mouseMask;     // 鼠标键位（addonMouseButtonMask 位布局）
    bool empty() const { return !(buttonMask || keyboardMask || mouseMask); }
};

class TurboInput : public GPAddon {
public:
    TurboInput(HmlBackKeyAddon* backKeys, LinearTriggerAddon* linearTrigger);
    virtual bool available();
    virtual void setup();
    virtual void reinit();
    virtual void preprocess() {}
    virtual void process();
    virtual void postprocess(bool sent) {}
    virtual std::string name() { return TurboName; }
private:
    void updateTurboShotCount(uint8_t turboShotCount, bool save = true);
    TurboResolvedAction resolvePinToAction(Pin_t pin);
    Mask_t turboPinMask;        // 标准 GPIO 上映射 TURBO 动作的掩码（背键部分经 getTurboKeyMask 实时查询）
    Mask_t turboGpioMask;       // 已绑定连发的 GPIO
    Mask_t longpressGpioMask;   // 长按闩锁中的 GPIO
    Mask_t lastHeldGpios;       // 上一帧按下的 GPIO（新按下沿检测）
    uint8_t lastDpad;           // Last d-pad pressed (for Turbo Change)
    uint32_t uIntervalUS;       // Turbo Interval in microseconds
    bool bTurboFlicker;         // 闪烁熄灭相位标志
    uint64_t nextTimer;         // Turbo Timer
    uint8_t lastShotCount;      // Last shot count for comparison
    HmlBackKeyAddon* backKeys;      // HML2354 恒载
    LinearTriggerAddon* linearTrigger; // 未启用时为 nullptr，访问须判空
};
#endif  // TURBO_H_
