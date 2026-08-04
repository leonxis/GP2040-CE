/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#ifndef GP2040_H_
#define GP2040_H_

#ifndef WEB_CONFIG_HOSTNAME
#define WEB_CONFIG_HOSTNAME "gns"
#endif

// GP2040 Classes
#include "gamepad.h"
#include "addonmanager.h"
#include "eventmanager.h"
#include "gpdriver.h"

#include "pico/types.h"

struct MainLoopGateStats {
    bool deadlineSchedulingActive;
    GateLateAnalogSource analogSource;
    uint16_t stableCompletions;
    uint32_t phaseMinUs;
    uint32_t phaseMaxUs;
    uint32_t ads8332BurstSetupWcetUs;
    uint32_t ads8332SampleWcetUs;
    uint32_t mcp3208BurstSetupWcetUs;
    uint32_t mcp3208SampleWcetUs;
    uint32_t finalProcessWcetUs;
    uint32_t endpointArmGuardUs;
    uint32_t sampleAgeLastUs;
    uint32_t sampleAgeMaxUs;
    uint32_t deadlineMissCount;
    uint32_t phaseMutationCount;
    uint32_t lateSampleSetCount;
    uint32_t repeatedSampleFrameCount;
    uint32_t frameWithoutFreshSampleCount;
    uint32_t maxSampleSetsPerFrame;
    uint32_t nextTokenEarliestUs;
    uint32_t finalizeDeadlineUs;
};

void getMainLoopGateStats(MainLoopGateStats* stats);

class GP2040 {
public:
    GP2040(){}
    ~GP2040(){}
    void setup();           // setup core0
    void run();             // loop core0
private:
    Gamepad snapshot;
    AddonManager addons;
    // GPIO debouncer
    void debounceGpioGetAll();
    Mask_t buttonGpios;
    uint32_t gpioDebounceTime[NUM_BANK0_GPIOS];

    struct RebootHotkeys {
        RebootHotkeys();
        void process(bool configMode);

        bool active;
        bool waitForHotkeyRelease;

        absolute_time_t noButtonsPressedTimeout;
        absolute_time_t rebootHotkeysHoldTimeout;
    };
    RebootHotkeys rebootHotkeys;

    enum class BootAction {
        NONE,
        ENTER_WEBCONFIG_MODE,
        ENTER_USB_MODE,
        SET_INPUT_MODE_SWITCH,
        SET_INPUT_MODE_XINPUT,
        SET_INPUT_MODE_XINPUTB,
        SET_INPUT_MODE_KEYBOARD,
        SET_INPUT_MODE_GENERIC,
        SET_INPUT_MODE_PS3,
        SET_INPUT_MODE_PS4,
        SET_INPUT_MODE_PS5,
        SET_INPUT_MODE_PS4B,
        SET_INPUT_MODE_P5GENERAL,
        SET_INPUT_MODE_XBONE,
        SET_INPUT_MODE_NEOGEO,
        SET_INPUT_MODE_MDMINI,
        SET_INPUT_MODE_PCEMINI,
        SET_INPUT_MODE_EGRET,
        SET_INPUT_MODE_ASTRO,
        SET_INPUT_MODE_PSCLASSIC,
        SET_INPUT_MODE_XBOXORIGINAL,
        SET_INPUT_MODE_SWITCH_PRO,
    };
    BootAction getBootAction();
    BootAction bootActionFromInputMode(int32_t inputMode);
    void getReinitGamepad(Gamepad * gamepad);

    // GPIO manipulation for setup and profile reinit
    void initializeStandardGpio();
    void deinitializeStandardGpio();

    // event handling checking
    void checkRawState(const GamepadState& prevState, const GamepadState& currState);
    void checkProcessedState(const GamepadState& prevState, const GamepadState& currState);

    void checkSaveRebootState();
    bool saveRequested = false;
    bool forceSave = false;
    bool saveSuccessful = false;
    void handleStorageSave(GPEvent* e);

    bool rebootRequested = false;
    void handleSystemReboot(GPEvent* e);

    System::BootMode rebootMode = System::BootMode::DEFAULT;
};

#endif
