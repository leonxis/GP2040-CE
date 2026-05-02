#ifndef _AxisTiltOverlay_H
#define _AxisTiltOverlay_H

#include "gpaddon.h"

#include <array>

#ifndef AXIS_TILT_OVERLAY_ENABLED
#define AXIS_TILT_OVERLAY_ENABLED 0
#endif

#ifndef AXIS_TILT_OVERLAY_PRESS_ENABLED
#define AXIS_TILT_OVERLAY_PRESS_ENABLED 0
#endif

#ifndef AXIS_TILT_OVERLAY_RIGHT_Y_TRIGGER_BUTTON_MASK
#define AXIS_TILT_OVERLAY_RIGHT_Y_TRIGGER_BUTTON_MASK 0
#endif

#ifndef AXIS_TILT_OVERLAY_RIGHT_Y_PERCENT1
#define AXIS_TILT_OVERLAY_RIGHT_Y_PERCENT1 0.0f
#endif

#ifndef AXIS_TILT_OVERLAY_RIGHT_Y_PERCENT2
#define AXIS_TILT_OVERLAY_RIGHT_Y_PERCENT2 0.0f
#endif

#ifndef AXIS_TILT_OVERLAY_RIGHT_Y_PERCENT3
#define AXIS_TILT_OVERLAY_RIGHT_Y_PERCENT3 0.0f
#endif

#ifndef AXIS_TILT_OVERLAY_RIGHT_Y_ACTIVE_PRESET
#define AXIS_TILT_OVERLAY_RIGHT_Y_ACTIVE_PRESET 0
#endif

#define AxisTiltOverlayName "AxisTiltOverlay"

class AxisTiltOverlayInput : public GPAddon {
public:
	virtual bool available();
	virtual void setup();
	virtual void preprocess() {}
	virtual void process() {}
	virtual void postprocess(bool) {}
	virtual void reinit();
	virtual std::string name() { return AxisTiltOverlayName; }

	void applyFinalProcess(Gamepad* gamepad);

private:
	struct Offset {
		float x;
		float y;
	};

	static constexpr uint16_t RC_BLOCK_SIZE = 32;
	static constexpr uint16_t RC_PAIR_COUNT = RC_BLOCK_SIZE / 2;

	void refreshCachedOptions();
	void loadRcOptionsIfDirty();
	uint32_t randomU32();
	float randomFloat(float minValue, float maxValue);
	Offset randomCircularOffset();
	void generateOffsetBlock();
	bool shouldStartJitterUnit();
	float radialAmpScaleFromCenter(float cx, float cy) const;
	float normalizeAxis(uint16_t v) const;
	uint16_t denormalizeAxis(float v) const;
	void resetRcState();

	float getRightYOverlayPercent() const;
	uint16_t applyPercentDelta(uint16_t axisValue, float percent) const;

	// Cached immutable options for runtime fast-path.
	bool runtimeEnabled {false};
	bool pressFeatureEnabled {false};
	bool rcGainFeatureEnabled {false};
	uint32_t rightYTriggerButtonMaskCached {0};
	uint32_t rcGainTriggerButtonMaskCached {0};
	bool rcGainAlwaysOnCached {false};
	float rightYPercent1Cached {0.0f};
	float rightYPercent2Cached {0.0f};
	float rightYPercent3Cached {0.0f};
	float rcGainReserved1Cached {0.0f};
	float rcGainReserved2Cached {0.0f};
	float rcGainReserved3Cached {0.0f};

	// Cached, normalized RC params from rcGainReserved1..3
	bool rcOptionsDirty {true};
	float rcJitterStrength {0.0f}; // 0..1
	float rcJitterRadius {0.0f}; // 0..1, circular offset bound
	bool rcRadialDecayActive {false};
	float rcDecayOuterNorm {0.0f}; // reserved3/100 when rcRadialDecayActive

	// RC runtime state
	bool motionHistoryReady {false};
	Offset prevCenter {0.0f, 0.0f};
	Offset prevVelocity {0.0f, 0.0f};
	uint32_t rngState {0xA53C9E17u};
	float jitterAccumulator {1.0f};
	uint16_t pairIndex {RC_PAIR_COUNT};
	bool jitterAwaitNeg {false};
	Offset pendingJitterA {0.0f, 0.0f};
	float radialAmpScaleCached {1.0f};
	std::array<Offset, RC_BLOCK_SIZE> offsetBlock {};
};

#endif
