#ifndef _GAMEPAD_H_
#define _GAMEPAD_H_

#include "BoardConfig.h"
#include "types.h"
#include <string.h>
#include <string>

#include "enums.pb.h"
#include "gamepad/GamepadState.h"
#include "gamepad/GamepadAuxState.h"

#include "pico/stdlib.h"

#include "config.pb.h"

// MUST BE DEFINED FOR MPG
extern uint32_t getMillis();
extern uint64_t getMicro();

struct GamepadButtonMapping
{
	GamepadButtonMapping(Mask_t bm) :
		pinMask(0),
		buttonMask(bm)
	{}

	uint32_t pinMask;
	const uint32_t buttonMask;
};

class Gamepad {
public:
	Gamepad();

	void setup();
	void reinit();
	void process();
	void read();
	void save();

	void hotkey();
	void clearState();
	void clearRumbleState();

	/**
	 * @brief Flag to indicate analog trigger support.
	 */
	bool hasAnalogTriggers {false};

	/**
	 * @brief Flag to indicate Left analog stick support.
	 */
	bool hasLeftAnalogStick {false};

	/**
	 * @brief Flag to indicate Right analog stick support.
	 */
	bool hasRightAnalogStick {false};

	/**
	 * @brief Check for a button press. Used by `pressed[Button]` helper methods.
	 */
	inline bool __attribute__((always_inline)) pressedButton(const uint32_t mask) {
		return (state.buttons & mask) == mask;
	}

	/**
	 * @brief Check for a dpad press. Used by `pressed[Dpad]` helper methods.
	 */
	inline bool __attribute__((always_inline)) pressedDpad(const uint8_t mask) {
		return (state.dpad & mask) == mask;
	}

	/**
	 * @brief Check for an aux button press. Same idea as `pressedButton`.
	 */
	inline bool __attribute__((always_inline)) pressedAux(const uint16_t mask) {
		return (state.aux & mask) == mask;
	}

	/**
	 * @brief Check for a hotkey combination press. Checks aux, buttons, and dpad.
	 */
	inline bool __attribute__((always_inline)) pressedHotkey(const HotkeyEntry &hotkey) {
		return (hotkey.action != 0 && pressedButton(hotkey.buttonsMask) &&
				pressedDpad(hotkey.dpadMask) && pressedAux(hotkey.auxMask));
	}

	/**
	 * @brief Remove hotkey bits from the state bitmask and provide pressed action.
	 */
	inline GamepadHotkey __attribute__((always_inline)) selectHotkey(const HotkeyEntry hotkey) {
		state.buttons &= ~(hotkey.buttonsMask);
		state.dpad &= ~(hotkey.dpadMask);
		return static_cast<GamepadHotkey>(hotkey.action);
	}

	inline bool __attribute__((always_inline)) pressedUp()    { return pressedDpad(GAMEPAD_MASK_UP); }
	inline bool __attribute__((always_inline)) pressedDown()  { return pressedDpad(GAMEPAD_MASK_DOWN); }
	inline bool __attribute__((always_inline)) pressedLeft()  { return pressedDpad(GAMEPAD_MASK_LEFT); }
	inline bool __attribute__((always_inline)) pressedRight() { return pressedDpad(GAMEPAD_MASK_RIGHT); }
	inline bool __attribute__((always_inline)) pressedB1()    { return pressedButton(GAMEPAD_MASK_B1); }
	inline bool __attribute__((always_inline)) pressedB2()    { return pressedButton(GAMEPAD_MASK_B2); }
	inline bool __attribute__((always_inline)) pressedB3()    { return pressedButton(GAMEPAD_MASK_B3); }
	inline bool __attribute__((always_inline)) pressedB4()    { return pressedButton(GAMEPAD_MASK_B4); }
	inline bool __attribute__((always_inline)) pressedL1()    { return pressedButton(GAMEPAD_MASK_L1); }
	inline bool __attribute__((always_inline)) pressedR1()    { return pressedButton(GAMEPAD_MASK_R1); }
	inline bool __attribute__((always_inline)) pressedL2()    { return pressedButton(GAMEPAD_MASK_L2); }
	inline bool __attribute__((always_inline)) pressedR2()    { return pressedButton(GAMEPAD_MASK_R2); }
	inline bool __attribute__((always_inline)) pressedS1()    { return pressedButton(GAMEPAD_MASK_S1); }
	inline bool __attribute__((always_inline)) pressedS2()    { return pressedButton(GAMEPAD_MASK_S2); }
	inline bool __attribute__((always_inline)) pressedL3()    { return pressedButton(GAMEPAD_MASK_L3); }
	inline bool __attribute__((always_inline)) pressedR3()    { return pressedButton(GAMEPAD_MASK_R3); }
	inline bool __attribute__((always_inline)) pressedA1()    { return pressedButton(GAMEPAD_MASK_A1); }
	inline bool __attribute__((always_inline)) pressedA2()    { return pressedButton(GAMEPAD_MASK_A2); }
	inline bool __attribute__((always_inline)) pressedA3()    { return pressedButton(GAMEPAD_MASK_A3); }
	inline bool __attribute__((always_inline)) pressedA4()    { return pressedButton(GAMEPAD_MASK_A4); }
	inline bool __attribute__((always_inline)) pressedE1()    { return pressedButton(GAMEPAD_MASK_E1); }
	inline bool __attribute__((always_inline)) pressedE2()    { return pressedButton(GAMEPAD_MASK_E2); }
	inline bool __attribute__((always_inline)) pressedE3()    { return pressedButton(GAMEPAD_MASK_E3); }
	inline bool __attribute__((always_inline)) pressedE4()    { return pressedButton(GAMEPAD_MASK_E4); }
	inline bool __attribute__((always_inline)) pressedE5()    { return pressedButton(GAMEPAD_MASK_E5); }
	inline bool __attribute__((always_inline)) pressedE6()    { return pressedButton(GAMEPAD_MASK_E6); }
	inline bool __attribute__((always_inline)) pressedE7()    { return pressedButton(GAMEPAD_MASK_E7); }
	inline bool __attribute__((always_inline)) pressedE8()    { return pressedButton(GAMEPAD_MASK_E8); }
	inline bool __attribute__((always_inline)) pressedE9()    { return pressedButton(GAMEPAD_MASK_E9); }
	inline bool __attribute__((always_inline)) pressedE10()   { return pressedButton(GAMEPAD_MASK_E10); }
	inline bool __attribute__((always_inline)) pressedE11()   { return pressedButton(GAMEPAD_MASK_E11); }
	inline bool __attribute__((always_inline)) pressedE12()   { return pressedButton(GAMEPAD_MASK_E12); }
	
	// Keyboard key press checkers for HID composite device (GPIO pins + addon mask e.g. touchpad)
	inline bool __attribute__((always_inline)) pressedKeyboardKeyA() { return (debouncedGpio & mapKeyboardKeyA->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 0)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyB() { return (debouncedGpio & mapKeyboardKeyB->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 1)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyC() { return (debouncedGpio & mapKeyboardKeyC->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 2)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyD() { return (debouncedGpio & mapKeyboardKeyD->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 3)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyE() { return (debouncedGpio & mapKeyboardKeyE->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 4)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyF() { return (debouncedGpio & mapKeyboardKeyF->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 5)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyG() { return (debouncedGpio & mapKeyboardKeyG->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 6)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyH() { return (debouncedGpio & mapKeyboardKeyH->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 7)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyI() { return (debouncedGpio & mapKeyboardKeyI->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 8)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyJ() { return (debouncedGpio & mapKeyboardKeyJ->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 9)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyK() { return (debouncedGpio & mapKeyboardKeyK->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 10)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyL() { return (debouncedGpio & mapKeyboardKeyL->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 11)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyM() { return (debouncedGpio & mapKeyboardKeyM->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 12)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyN() { return (debouncedGpio & mapKeyboardKeyN->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 13)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyO() { return (debouncedGpio & mapKeyboardKeyO->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 14)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyP() { return (debouncedGpio & mapKeyboardKeyP->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 15)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyQ() { return (debouncedGpio & mapKeyboardKeyQ->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 16)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyR() { return (debouncedGpio & mapKeyboardKeyR->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 17)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyS() { return (debouncedGpio & mapKeyboardKeyS->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 18)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyT() { return (debouncedGpio & mapKeyboardKeyT->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 19)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyU() { return (debouncedGpio & mapKeyboardKeyU->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 20)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyV() { return (debouncedGpio & mapKeyboardKeyV->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 21)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyW() { return (debouncedGpio & mapKeyboardKeyW->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 22)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyX() { return (debouncedGpio & mapKeyboardKeyX->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 23)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyY() { return (debouncedGpio & mapKeyboardKeyY->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 24)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyZ() { return (debouncedGpio & mapKeyboardKeyZ->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 25)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyCtrl() { return (debouncedGpio & mapKeyboardKeyCtrl->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 26)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyShift() { return (debouncedGpio & mapKeyboardKeyShift->pinMask) != 0 || (addonKeyboardKeyMask & (1U << 27)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKeyAltF4() { return (debouncedGpio & mapKeyboardKeyAltF4->pinMask) != 0 || (addonKeyboardKeyMask & (1ULL << 28)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKey0() { return (debouncedGpio & mapKeyboardKey0->pinMask) != 0 || (addonKeyboardKeyMask & (1ULL << 29)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKey1() { return (debouncedGpio & mapKeyboardKey1->pinMask) != 0 || (addonKeyboardKeyMask & (1ULL << 30)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKey2() { return (debouncedGpio & mapKeyboardKey2->pinMask) != 0 || (addonKeyboardKeyMask & (1ULL << 31)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKey3() { return (debouncedGpio & mapKeyboardKey3->pinMask) != 0 || (addonKeyboardKeyMask & (1ULL << 32)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKey4() { return (debouncedGpio & mapKeyboardKey4->pinMask) != 0 || (addonKeyboardKeyMask & (1ULL << 33)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKey5() { return (debouncedGpio & mapKeyboardKey5->pinMask) != 0 || (addonKeyboardKeyMask & (1ULL << 34)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKey6() { return (debouncedGpio & mapKeyboardKey6->pinMask) != 0 || (addonKeyboardKeyMask & (1ULL << 35)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKey7() { return (debouncedGpio & mapKeyboardKey7->pinMask) != 0 || (addonKeyboardKeyMask & (1ULL << 36)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKey8() { return (debouncedGpio & mapKeyboardKey8->pinMask) != 0 || (addonKeyboardKeyMask & (1ULL << 37)) != 0; }
	inline bool __attribute__((always_inline)) pressedKeyboardKey9() { return (debouncedGpio & mapKeyboardKey9->pinMask) != 0 || (addonKeyboardKeyMask & (1ULL << 38)) != 0; }

	// Mouse button mappings for HID composite device (GPIO + addon mappings)
	inline bool __attribute__((always_inline)) pressedMouseLeft() { return (debouncedGpio & mapMouseButtonLeft->pinMask) != 0 || (addonMouseButtonMask & (1U << 0)) != 0; }
	inline bool __attribute__((always_inline)) pressedMouseRight() { return (debouncedGpio & mapMouseButtonRight->pinMask) != 0 || (addonMouseButtonMask & (1U << 1)) != 0; }
	inline bool __attribute__((always_inline)) pressedMouseMiddle() { return (debouncedGpio & mapMouseButtonMiddle->pinMask) != 0 || (addonMouseButtonMask & (1U << 2)) != 0; }

	const GamepadOptions& getOptions() const { return options; }
	const DpadMode getActiveDpadMode() { return activeDpadMode; }

	void setInputMode(InputMode inputMode) { options.inputMode = inputMode; }
	void setSOCDMode(SOCDMode socdMode) { options.socdMode = socdMode; }
	void setDpadMode(DpadMode dpadMode) { options.dpadMode = dpadMode; }

	GamepadState state;
	GamepadState turboState;
	GamepadAuxState auxState;
	GamepadButtonMapping *mapDpadUp;
	GamepadButtonMapping *mapDpadDown;
	GamepadButtonMapping *mapDpadLeft;
	GamepadButtonMapping *mapDpadRight;
	GamepadButtonMapping *mapButtonB1;
	GamepadButtonMapping *mapButtonB2;
	GamepadButtonMapping *mapButtonB3;
	GamepadButtonMapping *mapButtonB4;
	GamepadButtonMapping *mapButtonL1;
	GamepadButtonMapping *mapButtonR1;
	GamepadButtonMapping *mapButtonL2;
	GamepadButtonMapping *mapButtonR2;
	GamepadButtonMapping *mapButtonS1;
	GamepadButtonMapping *mapButtonS2;
	GamepadButtonMapping *mapButtonL3;
	GamepadButtonMapping *mapButtonR3;
	GamepadButtonMapping *mapButtonA1;
	GamepadButtonMapping *mapButtonA2;
	GamepadButtonMapping *mapButtonA3;
	GamepadButtonMapping *mapButtonA4;
	GamepadButtonMapping *mapButtonE1;
	GamepadButtonMapping *mapButtonE2;
	GamepadButtonMapping *mapButtonE3;
	GamepadButtonMapping *mapButtonE4;
	GamepadButtonMapping *mapButtonE5;
	GamepadButtonMapping *mapButtonE6;
	GamepadButtonMapping *mapButtonE7;
	GamepadButtonMapping *mapButtonE8;
	GamepadButtonMapping *mapButtonE9;
	GamepadButtonMapping *mapButtonE10;
	GamepadButtonMapping *mapButtonE11;
	GamepadButtonMapping *mapButtonE12;
	GamepadButtonMapping *mapButtonFn;
	GamepadButtonMapping *mapButtonDP;
	GamepadButtonMapping *mapButtonLS;
	GamepadButtonMapping *mapButtonRS;
	GamepadButtonMapping *mapDigitalUp;
	GamepadButtonMapping *mapDigitalDown;
	GamepadButtonMapping *mapDigitalLeft;
	GamepadButtonMapping *mapDigitalRight;
	GamepadButtonMapping *mapAnalogLSXNeg;
	GamepadButtonMapping *mapAnalogLSXPos;
	GamepadButtonMapping *mapAnalogLSYNeg;
	GamepadButtonMapping *mapAnalogLSYPos;
	GamepadButtonMapping *mapAnalogRSXNeg;
	GamepadButtonMapping *mapAnalogRSXPos;
	GamepadButtonMapping *mapAnalogRSYNeg;
	GamepadButtonMapping *mapAnalogRSYPos;
	GamepadButtonMapping *map48WayMode;
	GamepadButtonMapping *mapFocusMode;
	
	// Keyboard key mappings for HID composite device
	GamepadButtonMapping *mapKeyboardKeyA;
	GamepadButtonMapping *mapKeyboardKeyB;
	GamepadButtonMapping *mapKeyboardKeyC;
	GamepadButtonMapping *mapKeyboardKeyD;
	GamepadButtonMapping *mapKeyboardKeyE;
	GamepadButtonMapping *mapKeyboardKeyF;
	GamepadButtonMapping *mapKeyboardKeyG;
	GamepadButtonMapping *mapKeyboardKeyH;
	GamepadButtonMapping *mapKeyboardKeyI;
	GamepadButtonMapping *mapKeyboardKeyJ;
	GamepadButtonMapping *mapKeyboardKeyK;
	GamepadButtonMapping *mapKeyboardKeyL;
	GamepadButtonMapping *mapKeyboardKeyM;
	GamepadButtonMapping *mapKeyboardKeyN;
	GamepadButtonMapping *mapKeyboardKeyO;
	GamepadButtonMapping *mapKeyboardKeyP;
	GamepadButtonMapping *mapKeyboardKeyQ;
	GamepadButtonMapping *mapKeyboardKeyR;
	GamepadButtonMapping *mapKeyboardKeyS;
	GamepadButtonMapping *mapKeyboardKeyT;
	GamepadButtonMapping *mapKeyboardKeyU;
	GamepadButtonMapping *mapKeyboardKeyV;
	GamepadButtonMapping *mapKeyboardKeyW;
	GamepadButtonMapping *mapKeyboardKeyX;
	GamepadButtonMapping *mapKeyboardKeyY;
	GamepadButtonMapping *mapKeyboardKeyZ;
	GamepadButtonMapping *mapKeyboardKeyCtrl;
	GamepadButtonMapping *mapKeyboardKeyShift;
	GamepadButtonMapping *mapKeyboardKeyAltF4;
	GamepadButtonMapping *mapKeyboardKey0;
	GamepadButtonMapping *mapKeyboardKey1;
	GamepadButtonMapping *mapKeyboardKey2;
	GamepadButtonMapping *mapKeyboardKey3;
	GamepadButtonMapping *mapKeyboardKey4;
	GamepadButtonMapping *mapKeyboardKey5;
	GamepadButtonMapping *mapKeyboardKey6;
	GamepadButtonMapping *mapKeyboardKey7;
	GamepadButtonMapping *mapKeyboardKey8;
	GamepadButtonMapping *mapKeyboardKey9;
	GamepadButtonMapping *mapMouseButtonLeft;
	GamepadButtonMapping *mapMouseButtonRight;
	GamepadButtonMapping *mapMouseButtonMiddle;

	// gamepad specific proxy of debounced buttons --- 1 = active (inverse of the raw GPIO)
	// see GP2040::debounceGpioGetAll for details
	Mask_t debouncedGpio;

	// Addon-driven keyboard key mask (e.g. 4-key touchpad): bit N = KEYBOARD_KEY at enum 131+N (A=0..Z=25, CTRL=26, SHIFT=27, ALT_F4=28, 0=29..9=38)
	uint64_t addonKeyboardKeyMask = 0;
	// Addon-driven mouse buttons: bit0=left, bit1=right, bit2=middle
	uint8_t addonMouseButtonMask = 0;
	// Addon-driven macro trigger mask: bit0=MACRO_1, bit1=MACRO_2, ..., bit5=MACRO_6
	uint8_t addonMacroTriggerMask = 0;

	uint32_t lastReinitProfileNumber = 0;

	// These are special to SOCD
	inline static const SOCDMode resolveSOCDMode(const GamepadOptions& options) {
		return (options.socdMode == SOCD_MODE_BYPASS &&
				(options.inputMode == INPUT_MODE_PS3 ||
				options.inputMode == INPUT_MODE_SWITCH ||
				options.inputMode == INPUT_MODE_NEOGEO ||
				options.inputMode == INPUT_MODE_PS4)) ?
			SOCD_MODE_NEUTRAL : options.socdMode;
	};

private:
	void processHotkeyAction(GamepadHotkey action);

	GamepadOptions & options;
	DpadMode activeDpadMode;
	bool map48WayModeToggle;
	const HotkeyOptions & hotkeyOptions;

	HotkeyEntry hotkeys[16];
	GamepadHotkey lastAction = HOTKEY_NONE;

	absolute_time_t disableFocusModeTimeout = nil_time;
};

#endif
