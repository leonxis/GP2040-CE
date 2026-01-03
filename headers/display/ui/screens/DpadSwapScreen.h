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
	void buildMenus();
	void resetInputState();

	void updateMenuNavigation(GpioAction action);
	void setDpadMode(int modeIndex);
	int32_t currentMode();

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

	int8_t exitToScreen = -1;
	bool isMenuReady = false;
	bool restartPending = false;
};

#endif

