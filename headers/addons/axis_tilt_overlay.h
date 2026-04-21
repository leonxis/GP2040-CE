#ifndef _AxisTiltOverlay_H
#define _AxisTiltOverlay_H

#include "gpaddon.h"

#ifndef AXIS_TILT_OVERLAY_ENABLED
#define AXIS_TILT_OVERLAY_ENABLED 0
#endif

#ifndef AXIS_TILT_OVERLAY_LEFT_Y_TRIGGER_BUTTON_MASK
#define AXIS_TILT_OVERLAY_LEFT_Y_TRIGGER_BUTTON_MASK 0
#endif

#ifndef AXIS_TILT_OVERLAY_RIGHT_Y_TRIGGER_BUTTON_MASK
#define AXIS_TILT_OVERLAY_RIGHT_Y_TRIGGER_BUTTON_MASK 0
#endif

#ifndef AXIS_TILT_OVERLAY_LEFT_Y_PERCENT1
#define AXIS_TILT_OVERLAY_LEFT_Y_PERCENT1 0.0f
#endif

#ifndef AXIS_TILT_OVERLAY_LEFT_Y_PERCENT2
#define AXIS_TILT_OVERLAY_LEFT_Y_PERCENT2 0.0f
#endif

#ifndef AXIS_TILT_OVERLAY_RIGHT_Y_PERCENT1
#define AXIS_TILT_OVERLAY_RIGHT_Y_PERCENT1 0.0f
#endif

#ifndef AXIS_TILT_OVERLAY_RIGHT_Y_PERCENT2
#define AXIS_TILT_OVERLAY_RIGHT_Y_PERCENT2 0.0f
#endif

#ifndef AXIS_TILT_OVERLAY_LEFT_Y_ACTIVE_PRESET
#define AXIS_TILT_OVERLAY_LEFT_Y_ACTIVE_PRESET 1
#endif

#ifndef AXIS_TILT_OVERLAY_RIGHT_Y_ACTIVE_PRESET
#define AXIS_TILT_OVERLAY_RIGHT_Y_ACTIVE_PRESET 1
#endif

#define AxisTiltOverlayName "AxisTiltOverlay"

class AxisTiltOverlayInput : public GPAddon {
public:
	virtual bool available();
	virtual void setup() {}
	virtual void preprocess() {}
	virtual void process() {}
	virtual void postprocess(bool) {}
	virtual void reinit() {}
	virtual std::string name() { return AxisTiltOverlayName; }

	void applyFinalProcess(Gamepad* gamepad);

private:
	float getAxisPercent(const AxisTiltOverlayOptions& options, bool isLeft) const;
	uint32_t getAxisTriggerMask(const AxisTiltOverlayOptions& options, bool isLeft) const;
	uint16_t applyPercentDelta(uint16_t axisValue, float percent) const;
};

#endif
