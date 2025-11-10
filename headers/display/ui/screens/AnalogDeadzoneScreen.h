#ifndef _ANALOGDEADZONESCREEN_H_
#define _ANALOGDEADZONESCREEN_H_

#include "GPGFX_UI_widgets.h"
#include "GPGFX_UI_types.h"
#include "GamepadState.h"
#include "enums.pb.h"

#include <array>
#include <vector>

class GamepadButtonMapping;

class AnalogDeadzoneScreen : public GPScreen {
public:
	AnalogDeadzoneScreen() {}
	AnalogDeadzoneScreen(GPGFX* renderer) { setRenderer(renderer); }
	virtual ~AnalogDeadzoneScreen() {}

	virtual void init();
	virtual void shutdown();
	virtual int8_t update();

protected:
	virtual void drawScreen();

private:
	enum class State {
		SELECT_STICK,
		EDIT_VALUES,
		PROMPT_RESTART,
	};

	void buildMenus();
	void resetInputState();

	void updateMenuNavigation(GpioAction action);
	void updateEditNavigation(GpioAction action);
	void updatePromptNavigation(GpioAction action);

	void enterEdit(int stickIndex);
	void exitEdit(bool discardChanges);
	void adjustCurrentValue(int delta);
	void applyChanges();

	int convertAnalogErrorToDisplay(uint32_t value) const;
	uint32_t convertDisplayToAnalogError(int value) const;

	static constexpr std::array<uint16_t, 11> errorLookup = {
		1000, 990, 979, 969, 958, 946, 934, 922, 911, 900, 890,
	};

	State currentState = State::SELECT_STICK;

	GPMenu* gpMenu = nullptr;
	std::vector<MenuEntry> stickSelectionMenu;
	std::vector<MenuEntry>* currentMenu = nullptr;
	const uint8_t menuLineSize = 4;

	GamepadButtonMapping* mapMenuUp = nullptr;
	GamepadButtonMapping* mapMenuDown = nullptr;
	GamepadButtonMapping* mapMenuLeft = nullptr;
	GamepadButtonMapping* mapMenuRight = nullptr;
	GamepadButtonMapping* mapMenuSelect = nullptr;
	GamepadButtonMapping* mapMenuBack = nullptr;

	Mask_t prevValues = 0;
	uint16_t prevButtonState = 0;
	uint8_t prevDpadState = 0;

	int8_t exitToScreen = -1;
	bool isMenuReady = false;
	bool screenIsPrompting = false;
	bool changesPending = false;

	int editingStick = 0;
	int selectedRow = 0;
	int innerDeadzoneValue = 0;
	int antiDeadzoneValue = 0;
	int errorValue = 0;
};

#endif

