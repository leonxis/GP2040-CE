#ifndef _STICKCALIBRATIONSCREEN_H_
#define _STICKCALIBRATIONSCREEN_H_

#include "GPGFX_UI_widgets.h"
#include "GPGFX_UI_types.h"
#include "enums.pb.h"
#include "storagemanager.h"
#include "addons/analog.h"
#include "hardware/adc.h"
#include "helper.h"

class StickCalibrationScreen : public GPScreen {
    public:
        StickCalibrationScreen() {}
        StickCalibrationScreen(GPGFX* renderer) { setRenderer(renderer); }
        virtual ~StickCalibrationScreen() {}
        virtual int8_t update();
        virtual void init();
        virtual void shutdown();
    protected:
        virtual void drawScreen();
    private:
        enum CalibrationState {
            STATE_STICK1_TOP_LEFT,
            STATE_STICK1_TOP_RIGHT,
            STATE_STICK1_BOTTOM_LEFT,
            STATE_STICK1_BOTTOM_RIGHT,
            STATE_STICK2_TOP_LEFT,
            STATE_STICK2_TOP_RIGHT,
            STATE_STICK2_BOTTOM_LEFT,
            STATE_STICK2_BOTTOM_RIGHT,
            STATE_COMPLETE
        };
        
        CalibrationState currentState;
        uint16_t calibrationValues[8]; // x1_tl, y1_tl, x1_tr, y1_tr, x1_bl, y1_bl, x1_br, y1_br
        uint16_t calibrationValues2[8]; // x2_tl, y2_tl, x2_tr, y2_tr, x2_bl, y2_bl, x2_br, y2_br
        uint16_t prevButtonState;
        
        void readJoystickCenter(uint8_t stickNum, uint16_t& x, uint16_t& y);
        void saveCalibration();
        void getStateInfo(const char*& stickLabel, const char*& direction);
};

#endif

