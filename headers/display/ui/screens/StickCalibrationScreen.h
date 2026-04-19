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
            STATE_PROMPT,
            STATE_SAMPLING,
        };

        CalibrationState currentState;
        uint16_t prevButtonState;

        /** Average `sampleCount` ADC reads for one stick (plan: 5). */
        void readJoystickCenter(uint8_t stickNum, uint16_t& x, uint16_t& y, int sampleCount);
        void performDualStickCalibrationAndExit();
};

#endif
