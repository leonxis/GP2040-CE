#ifndef _DPADSWAPSCREEN_H_
#define _DPADSWAPSCREEN_H_

#include "GPGFX_UI_widgets.h"
#include "GPGFX_UI_types.h"
#include "GamepadState.h"
#include "enums.pb.h"

#include <vector>

class GamepadButtonMapping;

class DpadSwapScreen : public GPScreen {
public:
	DpadSwapScreen() {}
	DpadSwapScreen(GPGFX* renderer) { setRenderer(renderer); }
	virtual ~DpadSwapScreen() {}

	virtual void init();
	virtual void shutdown();
	virtual int8_t update();

protected:
	virtual void drawScreen();

private:
	enum class State {
		SELECT_MODE,
		EDIT_VALUES,
	};

	void buildMenus();
	void resetInputState();

	void updateMenuNavigation(GpioAction action);
	void updateEditNavigation(GpioAction action);
	void enterEdit();
	void exitEdit(bool discardChanges);
	void adjustCurrentValue(int delta);
	void applyChanges();
	void setDpadMode(int modeIndex);
	int32_t currentMode();

	State currentState = State::SELECT_MODE;

	GPMenu* gpMenu = nullptr;
	std::vector<MenuEntry> modeSelectionMenu;
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
	bool changesPending = false;
	bool restartPending = false;

	int selectedRow = 0;
	int angleValue = 10;      // dpadTriggerThreshold (0-90)
	int deadzoneValue = 10;   // dpadDeadzone (0-90)
};

#endif

