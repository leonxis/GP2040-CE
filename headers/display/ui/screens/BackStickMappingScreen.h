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
            STATE_SELECT_STICK,      // Select back key (4 items)
            STATE_MAPPING_GPIO24,     // Left back 1  (GPIO24)
            STATE_MAPPING_GPIO25,     // Right back 1 (GPIO25)
            STATE_MAPPING_GPIO26,     // Left back 2  (GPIO26)
            STATE_MAPPING_GPIO27,     // Right back 2 (GPIO27)
            STATE_COMPLETE           // Show restart prompt
        };
        
        MappingState currentState;
        uint16_t prevButtonState = 0;
        uint8_t prevDpadState = 0;
        
        // Menu data
        std::vector<MenuEntry> stickSelectionMenu;
        std::vector<MenuEntry> gpio24MappingMenu;
        std::vector<MenuEntry> gpio25MappingMenu;
        std::vector<MenuEntry> gpio26MappingMenu;
        std::vector<MenuEntry> gpio27MappingMenu;
        
        // Current menu pointer
        std::vector<MenuEntry>* currentMenu;
        std::vector<MenuEntry>* previousMenu;
        
        // Menu widget
        GPMenu* gpMenu = nullptr;
        const uint8_t menuLineSize = 4;
        bool isMenuReady = false;
        
        // GPIO mapping state (GPIO24=左背键1, GPIO25=右背键1, GPIO26=左背键2, GPIO27=右背键2)
        GpioAction prevGPIO24Action;
        GpioAction updateGPIO24Action;
        GpioAction prevGPIO25Action;
        GpioAction updateGPIO25Action;
        GpioAction prevGPIO26Action;
        GpioAction updateGPIO26Action;
        GpioAction prevGPIO27Action;
        GpioAction updateGPIO27Action;
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
        void buildButtonMappingMenu(std::vector<MenuEntry>* menu, std::function<int32_t()> currentValueFunc, std::function<void()> selectFunc, bool isGPIO26);
        void selectStickType();
        int32_t currentStickType();
        void enterMapping(int stickIndex);
        void selectGPIO24Mapping();
        int32_t currentGPIO24Mapping();
        void selectGPIO25Mapping();
        int32_t currentGPIO25Mapping();
        void selectGPIO26Mapping();
        int32_t currentGPIO26Mapping();
        void selectGPIO27Mapping();
        int32_t currentGPIO27Mapping();
        void saveOptions();
        void updateMenuNavigation(GpioAction action);
};

#endif

