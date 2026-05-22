#ifndef _BACKSTICKMAPPINGSCREEN_H_
#define _BACKSTICKMAPPINGSCREEN_H_

#include "GPGFX_UI_widgets.h"
#include "GPGFX_UI_types.h"
#include "enums.pb.h"
#include "GamepadState.h"
#include "GPGFX_core.h"
#include "config.pb.h"

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
            STATE_SELECT_SLOT,
            STATE_MAPPING_VALUE,
        };

        // Slot indices (stored in MenuEntry.optionValue) — order matches HML web mapping page.
        static constexpr int32_t SLOT_GPIO25_EL = 0;   // LBkey
        static constexpr int32_t SLOT_GPIO24_ER = 1;   // RBkey
        static constexpr int32_t SLOT_LEFT_BACK1 = 2;
        static constexpr int32_t SLOT_RIGHT_BACK1 = 3;
        static constexpr int32_t SLOT_LEFT_BACK2 = 4;
        static constexpr int32_t SLOT_RIGHT_BACK2 = 5;
        static constexpr int32_t SLOT_TWOKEY_LEFT = 6;
        static constexpr int32_t SLOT_TWOKEY_RIGHT = 7;
        static constexpr int32_t SLOT_FN_LEFT = 8;
        static constexpr int32_t SLOT_FN_RIGHT = 9;
        static constexpr int32_t SLOT_MT_LEFT = 10;
        static constexpr int32_t SLOT_MT_RIGHT = 11;
        static constexpr int32_t SLOT_EXT_L2 = 12;
        static constexpr int32_t SLOT_EXT_R2 = 13;
        static constexpr int32_t SLOT_COUNT = 14;

        MappingState currentState = STATE_SELECT_SLOT;
        uint16_t prevButtonState = 0;
        uint8_t prevDpadState = 0;
        uint32_t lastSyncedActivePreset_ = 0;

        std::vector<MenuEntry> stickSelectionMenu;
        std::vector<MenuEntry> valueMappingMenu;

        std::vector<MenuEntry>* currentMenu = nullptr;
        std::vector<MenuEntry>* previousMenu = nullptr;

        GPMenu* gpMenu = nullptr;
        uint8_t slotMenuRowsY_ = 5;
        bool isMenuReady = false;

        int32_t activeSlot_ = 0;
        GpioMappingInfo pendingBySlot_[SLOT_COUNT];

        bool changesPending = false;
        Mask_t prevValues = 0;
        int8_t exitToScreen = -1;

        GamepadButtonMapping* mapMenuUp = nullptr;
        GamepadButtonMapping* mapMenuDown = nullptr;
        GamepadButtonMapping* mapMenuLeft = nullptr;
        GamepadButtonMapping* mapMenuRight = nullptr;
        GamepadButtonMapping* mapMenuSelect = nullptr;
        GamepadButtonMapping* mapMenuBack = nullptr;

        void rebuildStickSelectionMenu();
        void loadPendingFromStorage();
        void buildValueMappingMenu();
        void applySimpleAction(GpioMappingInfo* m, GpioAction a);
        GpioMappingInfo* pendingPtr(int32_t slot);
        GpioAction mappingActionForMenu(const GpioMappingInfo& m) const;
        void enterMappingForSlot(int32_t slot);
        void backToSlotMenu();
        void selectStickType();
        int32_t currentStickType();
        int32_t currentEditingAction();
        void selectEditingMapping();
        void saveOptions();
        void updateMenuNavigation(GpioAction action);
};

#endif
