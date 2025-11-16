#ifndef _BACKSTICKMAPPINGSCREEN_H_
#define _BACKSTICKMAPPINGSCREEN_H_

#include "GPGFX_UI_widgets.h"
#include "GPGFX_UI_types.h"
#include "enums.pb.h"
#include "GamepadState.h"
#include "GPGFX_core.h"

class GamepadButtonMapping;

class BackStickMappingScreen : public GPScreen {
    public:
        BackStickMappingScreen() {}
        BackStickMappingScreen(GPGFX* renderer) { setRenderer(renderer); }
        virtual ~BackStickMappingScreen() {}
        virtual int8_t update();
        virtual void init();
        virtual void shutdown();
    protected:
        virtual void drawScreen();
    private:
        enum MappingState {
            STATE_SELECT_STICK,      // Select Left stick or Right stick
            STATE_MAPPING_GPIO15,     // Mapping GPIO15 (Left Plus backstick)
            STATE_MAPPING_GPIO22,     // Mapping GPIO22 (Left backstick)
            STATE_MAPPING_GPIO14,     // Mapping GPIO14 (Right Plus backstick)
            STATE_MAPPING_GPIO25,     // Mapping GPIO25 (Right backstick)
            STATE_COMPLETE           // Show restart prompt
        };
        
        MappingState currentState;
        uint16_t prevButtonState = 0;
        uint8_t prevDpadState = 0;
        
        // Menu data
        std::vector<MenuEntry> stickSelectionMenu;
        std::vector<MenuEntry> gpio15MappingMenu;
        std::vector<MenuEntry> gpio22MappingMenu;
        std::vector<MenuEntry> gpio14MappingMenu;
        std::vector<MenuEntry> gpio25MappingMenu;
        
        // Current menu pointer
        std::vector<MenuEntry>* currentMenu;
        std::vector<MenuEntry>* previousMenu;
        
        // Menu widget
        GPMenu* gpMenu = nullptr;
        const uint8_t menuLineSize = 4;
        bool isMenuReady = false;
        
        // GPIO mapping state
        GpioAction prevGPIO15Action;
        GpioAction updateGPIO15Action;
        GpioAction prevGPIO22Action;
        GpioAction updateGPIO22Action;
        GpioAction prevGPIO14Action;
        GpioAction updateGPIO14Action;
        GpioAction prevGPIO25Action;
        GpioAction updateGPIO25Action;
        bool changesPending = false;
        Mask_t prevValues = 0;
        int8_t exitToScreen = -1;

        GamepadButtonMapping* mapMenuUp = nullptr;
        GamepadButtonMapping* mapMenuDown = nullptr;
        GamepadButtonMapping* mapMenuLeft = nullptr;
        GamepadButtonMapping* mapMenuRight = nullptr;
        GamepadButtonMapping* mapMenuSelect = nullptr;
        GamepadButtonMapping* mapMenuBack = nullptr;
        
        // Helper functions
        void buildButtonMappingMenu(std::vector<MenuEntry>* menu, std::function<int32_t()> currentValueFunc, std::function<void()> selectFunc, bool isGPIO22);
        void selectStickType();
        int32_t currentStickType();
        void enterMapping(int stickIndex);
        void selectGPIO15Mapping();
        int32_t currentGPIO15Mapping();
        void selectGPIO22Mapping();
        int32_t currentGPIO22Mapping();
        void selectGPIO14Mapping();
        int32_t currentGPIO14Mapping();
        void selectGPIO25Mapping();
        int32_t currentGPIO25Mapping();
        void saveOptions();
        void updateMenuNavigation(GpioAction action);
        bool isUSBPeripheralEnabled();
};

#endif

