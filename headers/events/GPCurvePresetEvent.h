#ifndef _GPCURVEPRESETEVENT_H_
#define _GPCURVEPRESETEVENT_H_

#include "GPEvent.h"

class GPCurvePresetChangeEvent : public GPEvent {
    public:
        GPCurvePresetChangeEvent() {}
        GPCurvePresetChangeEvent(uint8_t preset, bool isLeft) {
            this->presetIndex = preset;
            this->isLeftStick = isLeft;
        }
        virtual ~GPCurvePresetChangeEvent() {}

        GPEventType eventType() { return this->_eventType; }

        uint8_t presetIndex;
        bool isLeftStick;
    private:
        GPEventType _eventType = GP_EVENT_CURVE_PRESET_CHANGE;
};

#endif


