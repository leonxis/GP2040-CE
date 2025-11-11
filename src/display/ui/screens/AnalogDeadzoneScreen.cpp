#include "AnalogDeadzoneScreen.h"

#include "GPGFX_UI_screens.h"
#include "GPGFX_core.h"
#include "GPRestartEvent.h"
#include "GPStorageSaveEvent.h"
#include "eventmanager.h"
#include "storagemanager.h"
#include "system.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>

void AnalogDeadzoneScreen::init() {
	getRenderer()->clearScreen();

	currentState = State::SELECT_STICK;
	screenIsPrompting = false;
	changesPending = false;
	restartPending = false;
	exitToScreen = -1;

	if (gpMenu == nullptr) {
		gpMenu = new GPMenu();
		gpMenu->setRenderer(getRenderer());
		gpMenu->setPosition(8, 16);
		gpMenu->setMenuSize(18, menuLineSize);
		addElement(gpMenu);
	} else {
		gpMenu->setRenderer(getRenderer());
		gpMenu->setMenuSize(18, menuLineSize);
	}

	if (mapMenuUp) { delete mapMenuUp; mapMenuUp = nullptr; }
	if (mapMenuDown) { delete mapMenuDown; mapMenuDown = nullptr; }
	if (mapMenuLeft) { delete mapMenuLeft; mapMenuLeft = nullptr; }
	if (mapMenuRight) { delete mapMenuRight; mapMenuRight = nullptr; }
	if (mapMenuSelect) { delete mapMenuSelect; mapMenuSelect = nullptr; }
	if (mapMenuBack) { delete mapMenuBack; mapMenuBack = nullptr; }

	mapMenuUp = new GamepadButtonMapping(GAMEPAD_MASK_UP);
	mapMenuDown = new GamepadButtonMapping(GAMEPAD_MASK_DOWN);
	mapMenuLeft = new GamepadButtonMapping(GAMEPAD_MASK_LEFT);
	mapMenuRight = new GamepadButtonMapping(GAMEPAD_MASK_RIGHT);
	mapMenuSelect = new GamepadButtonMapping(GAMEPAD_MASK_B1);
	mapMenuBack = new GamepadButtonMapping(GAMEPAD_MASK_B2);

	GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
	for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
		switch (pinMappings[pin].action) {
			case GpioAction::MENU_NAVIGATION_UP: mapMenuUp->pinMask |= 1 << pin; break;
			case GpioAction::MENU_NAVIGATION_DOWN: mapMenuDown->pinMask |= 1 << pin; break;
			case GpioAction::MENU_NAVIGATION_LEFT: mapMenuLeft->pinMask |= 1 << pin; break;
			case GpioAction::MENU_NAVIGATION_RIGHT: mapMenuRight->pinMask |= 1 << pin; break;
			case GpioAction::MENU_NAVIGATION_SELECT: mapMenuSelect->pinMask |= 1 << pin; break;
			case GpioAction::MENU_NAVIGATION_BACK: mapMenuBack->pinMask |= 1 << pin; break;
			default: break;
		}
	}

	buildMenus();

	currentMenu = &stickSelectionMenu;
	gpMenu->setMenuData(currentMenu);
	gpMenu->setMenuTitle("DeadZone");
	gpMenu->setIndex(0);
	gpMenu->setVisibility(true);

	resetInputState();
	isMenuReady = true;
}

void AnalogDeadzoneScreen::buildMenus() {
	stickSelectionMenu.clear();
	stickSelectionMenu.push_back({"Left JoyStick", nullptr, nullptr,
		[]() -> int32_t { return -1; }, std::bind(&AnalogDeadzoneScreen::enterEdit, this, 0), 0});
	stickSelectionMenu.push_back({"Right JoyStick", nullptr, nullptr,
		[]() -> int32_t { return -1; }, std::bind(&AnalogDeadzoneScreen::enterEdit, this, 1), 1});
}

void AnalogDeadzoneScreen::resetInputState() {
	prevValues = Storage::getInstance().GetGamepad()->debouncedGpio;
	prevButtonState = getGamepad()->state.buttons;
	prevDpadState = getGamepad()->state.dpad;
}

void AnalogDeadzoneScreen::shutdown() {
	clearElements();
	isMenuReady = false;
	currentMenu = nullptr;
	gpMenu = nullptr;

	if (mapMenuUp) { delete mapMenuUp; mapMenuUp = nullptr; }
	if (mapMenuDown) { delete mapMenuDown; mapMenuDown = nullptr; }
	if (mapMenuLeft) { delete mapMenuLeft; mapMenuLeft = nullptr; }
	if (mapMenuRight) { delete mapMenuRight; mapMenuRight = nullptr; }
	if (mapMenuSelect) { delete mapMenuSelect; mapMenuSelect = nullptr; }
	if (mapMenuBack) { delete mapMenuBack; mapMenuBack = nullptr; }
}

int8_t AnalogDeadzoneScreen::update() {
	if (!isMenuReady) {
		int8_t result = exitToScreen;
		exitToScreen = -1;
		return result;
	}

	GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();
	Mask_t values = Storage::getInstance().GetGamepad()->debouncedGpio;
	uint16_t buttonState = getGamepad()->state.buttons;
	uint8_t dpadState = getGamepad()->state.dpad;

	auto dispatchAction = [&](GpioAction action) {
		switch (currentState) {
			case State::SELECT_STICK: updateMenuNavigation(action); break;
			case State::EDIT_VALUES: updateEditNavigation(action); break;
			case State::PROMPT_RESTART: updatePromptNavigation(action); break;
		}
	};

	if (prevValues != values) {
		if (values & mapMenuUp->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_UP);
		else if (values & mapMenuDown->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_DOWN);
		else if (values & mapMenuLeft->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_LEFT);
		else if (values & mapMenuRight->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_RIGHT);
		else if (values & mapMenuSelect->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_SELECT);
		else if (values & mapMenuBack->pinMask) dispatchAction(GpioAction::MENU_NAVIGATION_BACK);
	}

	if (gamepadOptions.miniMenuGamepadInput) {
		if (prevDpadState != dpadState) {
			if (dpadState == mapMenuUp->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_UP);
			else if (dpadState == mapMenuDown->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_DOWN);
			else if (dpadState == mapMenuLeft->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_LEFT);
			else if (dpadState == mapMenuRight->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_RIGHT);
		}
		if (prevButtonState != buttonState) {
			if (buttonState == mapMenuSelect->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_SELECT);
			else if (buttonState == mapMenuBack->buttonMask) dispatchAction(GpioAction::MENU_NAVIGATION_BACK);
		}
	}

	prevValues = values;
	prevButtonState = buttonState;
	prevDpadState = dpadState;

	if (exitToScreen != -1) {
		int8_t result = exitToScreen;
		exitToScreen = -1;
		return result;
	}

	return -1;
}

void AnalogDeadzoneScreen::drawScreen() {
	if (currentState == State::SELECT_STICK) {
		if (gpMenu) gpMenu->setVisibility(true);
		return;
	}

	getRenderer()->clearScreen();
	if (gpMenu) gpMenu->setVisibility(false);

	if (currentState == State::PROMPT_RESTART) {
		getRenderer()->drawText(3, 0, "[DeadZone]");
		getRenderer()->drawText(4, 1, "Complete!");
		getRenderer()->drawText(3, 3, "Press B1 to");
		getRenderer()->drawText(5, 4, "restart");
		return;
	}

	const char* stickLabel = (editingStick == 0) ? "Left JoyStick" : "Right JoyStick";
	getRenderer()->drawText(3, 0, "[DeadZone]");
	getRenderer()->drawText(2, 1, stickLabel);

	struct RowInfo {
		const char* label;
		int value;
	};

	RowInfo rows[3] = {
		{"Inner", innerDeadzoneValue},
		{"Anti", antiDeadzoneValue},
		{"Error", errorValue},
	};

	for (int i = 0; i < 3; i++) {
		int y = 3 + (i * 2);
		getRenderer()->drawText(2, y, rows[i].label);

		char valueText[6];
		snprintf(valueText, sizeof(valueText), "%2d", rows[i].value);

		if (selectedRow == i) {
			getRenderer()->drawText(10, y, CHAR_LEFT);
		}
		getRenderer()->drawText(12, y, valueText);
		if (selectedRow == i) {
			getRenderer()->drawText(15, y, CHAR_RIGHT);
		}
	}
}

void AnalogDeadzoneScreen::updateMenuNavigation(GpioAction action) {
	if (!gpMenu || currentMenu == nullptr) return;

	uint16_t menuIndex = gpMenu->getIndex();
	uint16_t menuSize = currentMenu->size();

	switch (action) {
		case GpioAction::MENU_NAVIGATION_UP:
			if (menuSize == 0) break;
			if (menuIndex == 0) gpMenu->setIndex(menuSize - 1);
			else gpMenu->setIndex(menuIndex - 1);
			break;
		case GpioAction::MENU_NAVIGATION_DOWN:
			if (menuSize == 0) break;
			if (menuIndex < menuSize - 1) gpMenu->setIndex(menuIndex + 1);
			else gpMenu->setIndex(0);
			break;
		case GpioAction::MENU_NAVIGATION_SELECT:
			if (menuSize == 0) break;
			enterEdit(gpMenu->getIndex());
			break;
		case GpioAction::MENU_NAVIGATION_BACK:
			if (restartPending) {
				currentState = State::PROMPT_RESTART;
				screenIsPrompting = true;
				if (gpMenu) gpMenu->setVisibility(false);
				resetInputState();
			} else {
				exitToScreen = DisplayMode::MAIN_MENU;
				isMenuReady = false;
			}
			break;
		default:
			break;
	}
}

void AnalogDeadzoneScreen::updateEditNavigation(GpioAction action) {
	switch (action) {
		case GpioAction::MENU_NAVIGATION_UP:
			selectedRow = (selectedRow + 2) % 3;
			break;
		case GpioAction::MENU_NAVIGATION_DOWN:
			selectedRow = (selectedRow + 1) % 3;
			break;
		case GpioAction::MENU_NAVIGATION_LEFT:
			adjustCurrentValue(-1);
			break;
		case GpioAction::MENU_NAVIGATION_RIGHT:
			adjustCurrentValue(1);
			break;
		case GpioAction::MENU_NAVIGATION_BACK:
			exitEdit(false);
			break;
		default:
			break;
	}
}

void AnalogDeadzoneScreen::updatePromptNavigation(GpioAction action) {
	switch (action) {
		case GpioAction::MENU_NAVIGATION_SELECT:
			restartPending = false;
			EventManager::getInstance().triggerEvent(new GPStorageSaveEvent(true, true));
			EventManager::getInstance().triggerEvent(new GPRestartEvent(System::BootMode::GAMEPAD));
			break;
		case GpioAction::MENU_NAVIGATION_BACK:
			screenIsPrompting = false;
			currentState = State::SELECT_STICK;
			if (gpMenu) {
				gpMenu->setMenuData(&stickSelectionMenu);
				gpMenu->setMenuTitle("DeadZone");
				gpMenu->setIndex(0);
				gpMenu->setVisibility(true);
			}
			isMenuReady = true;
			resetInputState();
			break;
		default:
			break;
	}
}

void AnalogDeadzoneScreen::enterEdit(int stickIndex) {
	editingStick = stickIndex;
	selectedRow = 0;
	changesPending = false;
	currentState = State::EDIT_VALUES;
	screenIsPrompting = false;
	if (gpMenu) gpMenu->setVisibility(false);

	AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;

	if (editingStick == 0) {
		innerDeadzoneValue = std::clamp<int>(analogOptions.inner_deadzone, 0, 10);
		antiDeadzoneValue = std::clamp<int>(analogOptions.anti_deadzone, 0, 10);
		errorValue = convertAnalogErrorToDisplay(analogOptions.analog_error);
	} else {
		innerDeadzoneValue = std::clamp<int>(analogOptions.inner_deadzone2, 0, 10);
		antiDeadzoneValue = std::clamp<int>(analogOptions.anti_deadzone2, 0, 10);
		errorValue = convertAnalogErrorToDisplay(analogOptions.analog_error2);
	}

	resetInputState();
}

void AnalogDeadzoneScreen::exitEdit(bool discardChanges) {
	if (!discardChanges && changesPending) {
		applyChanges();
		restartPending = true;
	}

	currentState = State::SELECT_STICK;
	screenIsPrompting = false;
	if (gpMenu) {
		gpMenu->setMenuData(&stickSelectionMenu);
		gpMenu->setMenuTitle("DeadZone");
		gpMenu->setIndex(static_cast<uint16_t>(editingStick));
		gpMenu->setVisibility(true);
	}
	resetInputState();
}

void AnalogDeadzoneScreen::adjustCurrentValue(int delta) {
	int* target = nullptr;
	switch (selectedRow) {
		case 0: target = &innerDeadzoneValue; break;
		case 1: target = &antiDeadzoneValue; break;
		case 2: target = &errorValue; break;
		default: break;
	}
	if (target == nullptr) return;

	int newValue = std::clamp(*target + delta, 0, 10);
	if (newValue != *target) {
		*target = newValue;
		changesPending = true;
		restartPending = true;
	}
}

void AnalogDeadzoneScreen::applyChanges() {
	AnalogOptions& analogOptions = Storage::getInstance().getAddonOptions().analogOptions;

	if (editingStick == 0) {
		analogOptions.inner_deadzone = static_cast<uint32_t>(innerDeadzoneValue);
		analogOptions.anti_deadzone = static_cast<uint32_t>(antiDeadzoneValue);
		analogOptions.analog_error = convertDisplayToAnalogError(errorValue);
	} else {
		analogOptions.inner_deadzone2 = static_cast<uint32_t>(innerDeadzoneValue);
		analogOptions.anti_deadzone2 = static_cast<uint32_t>(antiDeadzoneValue);
		analogOptions.analog_error2 = convertDisplayToAnalogError(errorValue);
	}

	changesPending = false;
	resetInputState();
}

int AnalogDeadzoneScreen::convertAnalogErrorToDisplay(uint32_t value) const {
	int bestIndex = 0;
	uint32_t bestDiff = std::abs(static_cast<int32_t>(value) - static_cast<int32_t>(errorLookup[0]));
	for (size_t i = 1; i < errorLookup.size(); i++) {
		uint32_t diff = std::abs(static_cast<int32_t>(value) - static_cast<int32_t>(errorLookup[i]));
		if (diff < bestDiff) {
			bestDiff = diff;
			bestIndex = static_cast<int>(i);
		}
	}
	return bestIndex;
}

uint32_t AnalogDeadzoneScreen::convertDisplayToAnalogError(int value) const {
	value = std::clamp(value, 0, static_cast<int>(errorLookup.size() - 1));
	return errorLookup[static_cast<size_t>(value)];
}

