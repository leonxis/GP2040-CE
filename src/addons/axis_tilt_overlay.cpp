#include "addons/axis_tilt_overlay.h"

#include "storagemanager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
constexpr float kMotionFeedforwardGain = 12.0f;
constexpr float kMotionFeedforwardDeadzone = 0.0015f;
/** L∞ stick deflection where radial decay of overlay jitter begins (normalized 0..1). */
constexpr float kRcDecayStart = 0.03f;
/** Lower end of RX jitter sample segment (normalized); below rcJitterRadius so min reserved2 (2%) yields non-zero span. */
constexpr float kRcXSampleMinNorm = 0.019f;

// Joystick axis linear map (GAMEPAD_JOYSTICK_*); spans differ by 1 (32768 vs 32767).
constexpr float kJoyMidF = static_cast<float>(GAMEPAD_JOYSTICK_MID);
constexpr float kJoyMinF = static_cast<float>(GAMEPAD_JOYSTICK_MIN);
constexpr float kJoyMaxF = static_cast<float>(GAMEPAD_JOYSTICK_MAX);
constexpr float kJoyPosSpanF =
	static_cast<float>(GAMEPAD_JOYSTICK_MAX - GAMEPAD_JOYSTICK_MID);
constexpr float kJoyNegSpanF =
	static_cast<float>(GAMEPAD_JOYSTICK_MID - GAMEPAD_JOYSTICK_MIN);
constexpr float kInvJoyPosSpan = 1.0f / kJoyPosSpanF;
constexpr float kInvJoyNegSpan = 1.0f / kJoyNegSpanF;

static inline float applyAccelerationFeedforward(float offsetValue, float accelValue) {
	const float accelAbs = std::fabs(accelValue);
	if (accelAbs <= kMotionFeedforwardDeadzone || offsetValue == 0.0f) {
		return offsetValue;
	}
	const float motionAmount = std::clamp(
		(accelAbs - kMotionFeedforwardDeadzone) * kMotionFeedforwardGain,
		0.0f,
		1.0f
	);
	const bool sameDirection = (offsetValue * accelValue) > 0.0f;
	const float scale = sameDirection ? (1.0f + motionAmount) : (1.0f - motionAmount);
	return offsetValue * scale;
}
}

bool AxisTiltOverlayInput::available() {
	const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
	const AxisTiltOverlayOptions& options = addonOptions.axisTiltOverlayOptions;

	if (!addonOptions.ads8332Options.enabled || addonOptions.analogOptions.enabled) {
		return false;
	}

	// Plugin is available when at least one sub-feature is enabled.
	return options.pressEnabled || options.rcGainEnabled;
}

void AxisTiltOverlayInput::setup() {
	reinit();
}

void AxisTiltOverlayInput::reinit() {
	refreshCachedOptions();
	rcOptionsDirty = true;
	resetRcState();
	fillRcXOffsetTemplate();
}

void AxisTiltOverlayInput::refreshCachedOptions() {
	const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
	const AxisTiltOverlayOptions& options = addonOptions.axisTiltOverlayOptions;

	rightYTriggerButtonMaskCached = options.rightYTriggerButtonMask;
	rightYPercent1Cached = options.rightYPercent1;
	rightYPercent2Cached = options.rightYPercent2;
	rightYPercent3Cached = options.rightYPercent3;
	rcGainTriggerButtonMaskCached = options.rcGainTriggerButtonMask;
	rcGainAlwaysOnCached = options.rcGainAlwaysOn;
	rcGainReserved1Cached = options.rcGainReserved1;
	rcGainReserved2Cached = options.rcGainReserved2;
	rcGainReserved3Cached = options.rcGainReserved3;

	const bool anyPressPercent = (rightYPercent1Cached != 0.0f) ||
		(rightYPercent2Cached != 0.0f) ||
		(rightYPercent3Cached != 0.0f);
	pressFeatureEnabled = options.pressEnabled && anyPressPercent;
	rcGainFeatureEnabled = options.rcGainEnabled;
	runtimeEnabled = addonOptions.ads8332Options.enabled &&
		!addonOptions.analogOptions.enabled &&
		(pressFeatureEnabled || rcGainFeatureEnabled);
}

void AxisTiltOverlayInput::resetRcState() {
	motionHistoryReady = false;
	prevCenter = Offset{0.0f, 0.0f};
	prevVelocity = Offset{0.0f, 0.0f};
	rngState = 0xA53C9E17u;
	jitterAccumulator = 1.0f;
	pairIndex = RC_TEMPLATE_COUNT;
	jitterPhase = 0;
	radialAmpScaleCached = 1.0f;
	pendingJitterBaseNorm = 0.0f;
}

void AxisTiltOverlayInput::loadRcOptionsIfDirty() {
	if (!rcOptionsDirty) {
		return;
	}

	rcJitterStrength = std::clamp(rcGainReserved1Cached / 100.0f, 0.0f, 1.0f);
	rcJitterRadius = std::clamp(rcGainReserved2Cached / 100.0f, 0.0f, 1.0f);

	const float reserved3Clamped = std::clamp(rcGainReserved3Cached, 0.0f, 100.0f);
	rcRadialDecayActive = (reserved3Clamped > 3.0f);
	if (rcRadialDecayActive) {
		rcDecayOuterNorm = reserved3Clamped / 100.0f;
	} else {
		rcDecayOuterNorm = 0.0f;
	}

	rcOptionsDirty = false;
	resetRcState();
}

uint32_t AxisTiltOverlayInput::randomU32() {
	uint32_t x = rngState;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	if (x == 0) {
		x = 0xA53C9E17u;
	}
	rngState = x;
	return x;
}

void AxisTiltOverlayInput::fillRcXOffsetTemplate() {
	const float invN = 1.0f / static_cast<float>(RC_TEMPLATE_COUNT);
	for (uint16_t i = 0; i < RC_TEMPLATE_COUNT; ++i) {
		offsetBlock[i] = (static_cast<float>(i) + 0.5f) * invN;
	}
}

void AxisTiltOverlayInput::generateOffsetBlock() {
	for (uint16_t i = RC_TEMPLATE_COUNT - 1; i > 0; --i) {
		const uint16_t j = static_cast<uint16_t>(randomU32() % (i + 1));
		std::swap(offsetBlock[i], offsetBlock[j]);
	}
	pairIndex = 0;
}

float AxisTiltOverlayInput::radialAmpScaleFromCenter(float cx, float cy) const {
	if (!rcRadialDecayActive) {
		return 1.0f;
	}
	const float r = std::max(std::fabs(cx), std::fabs(cy));
	if (r <= kRcDecayStart) {
		return 1.0f;
	}
	if (r >= rcDecayOuterNorm) {
		return 0.0f;
	}
	return std::clamp(
		(rcDecayOuterNorm - r) / (rcDecayOuterNorm - kRcDecayStart),
		0.0f,
		1.0f
	);
}

// RC jitter runs in 4-frame units (+A, +1.5A, -1.5A, -A). shouldStartJitterUnit() is evaluated only
// when jitterPhase == 0 (idle / start of cycle); rcJitterStrength gates how often a new unit starts.
bool AxisTiltOverlayInput::shouldStartJitterUnit() {
	if (rcJitterStrength <= 0.0f) {
		jitterAccumulator = 1.0f;
		return false;
	}
	if (rcJitterStrength >= 1.0f) {
		jitterAccumulator = 1.0f;
		return true;
	}

	jitterAccumulator += rcJitterStrength;
	if (jitterAccumulator >= 1.0f) {
		jitterAccumulator -= 1.0f;
		return true;
	}
	return false;
}

float AxisTiltOverlayInput::normalizeAxis(uint16_t v) const {
	const int32_t d = static_cast<int32_t>(v) - static_cast<int32_t>(GAMEPAD_JOYSTICK_MID);
	if (d >= 0) {
		return std::clamp(static_cast<float>(d) * kInvJoyPosSpan, 0.0f, 1.0f);
	}
	return std::clamp(static_cast<float>(d) * kInvJoyNegSpan, -1.0f, 0.0f);
}

uint16_t AxisTiltOverlayInput::denormalizeAxis(float v) const {
	v = std::clamp(v, -1.0f, 1.0f);
	if (v >= 0.0f) {
		const float raw = kJoyMidF + v * kJoyPosSpanF;
		return static_cast<uint16_t>(std::lround(std::clamp(raw, kJoyMidF, kJoyMaxF)));
	}
	const float raw = kJoyMidF + v * kJoyNegSpanF;
	return static_cast<uint16_t>(std::lround(std::clamp(raw, kJoyMinF, kJoyMidF)));
}

float AxisTiltOverlayInput::getRightYOverlayPercent() const {
	// rightYActivePreset 可由热键在运行中修改，每帧读实时值；百分数仍用 refreshCachedOptions 缓存。
	const uint32_t preset =
		Storage::getInstance().getAddonOptions().axisTiltOverlayOptions.rightYActivePreset;
	switch (preset) {
		case 0:
			return 0.0f;
		case 2:
			return rightYPercent2Cached;
		case 3:
			return rightYPercent3Cached;
		case 1:
		default:
			return rightYPercent1Cached;
	}
}

uint16_t AxisTiltOverlayInput::applyPercentDelta(uint16_t axisValue, float percent) const {
	if (percent == 0.0f) {
		return axisValue;
	}

	const float maxTravel = static_cast<float>(GAMEPAD_JOYSTICK_MAX - GAMEPAD_JOYSTICK_MID);
	const int32_t delta = static_cast<int32_t>(std::lround((percent * maxTravel) / 100.0f));
	const int32_t newValue = std::clamp<int32_t>(
		static_cast<int32_t>(axisValue) + delta,
		GAMEPAD_JOYSTICK_MIN,
		GAMEPAD_JOYSTICK_MAX
	);

	return static_cast<uint16_t>(newValue);
}

void AxisTiltOverlayInput::applyFinalProcess(Gamepad* gamepad) {
	if (gamepad == nullptr) {
		return;
	}
	if (!runtimeEnabled) {
		return;
	}

	const uint32_t buttons = gamepad->state.buttons;

	if (pressFeatureEnabled) {
		const uint32_t rightMask = rightYTriggerButtonMaskCached;
		if ((rightMask != 0) && ((buttons & rightMask) != 0)) {
			gamepad->state.ry = applyPercentDelta(gamepad->state.ry, getRightYOverlayPercent());
		}
	}

	if (rcGainFeatureEnabled) {
		const uint32_t rcMask = rcGainTriggerButtonMaskCached;
		const bool rcTriggeredByButton = (rcMask != 0) && ((buttons & rcMask) != 0);
		const bool rcTriggered = rcGainAlwaysOnCached || rcTriggeredByButton;
		if (!rcTriggered) {
			motionHistoryReady = false;
			prevCenter = Offset{0.0f, 0.0f};
			prevVelocity = Offset{0.0f, 0.0f};
			jitterPhase = 0;
			jitterAccumulator = 1.0f;
			pairIndex = RC_TEMPLATE_COUNT;
			radialAmpScaleCached = 1.0f;
			return;
		}

		// Cached zero strength: skip reload and remap. (Do not fast-path on radius alone:
		// strength in (0,1) must still run shouldStartJitterUnit while radius is 0.)
		if (!rcOptionsDirty && (rcJitterStrength <= 0.0f)) {
			jitterAccumulator = 1.0f;
			jitterPhase = 0;
			pairIndex = RC_TEMPLATE_COUNT;
			return;
		}

		loadRcOptionsIfDirty();
		const bool rcHasEffect =
			(rcJitterStrength > 0.0f) && (rcJitterRadius > kRcXSampleMinNorm);
		if (!rcHasEffect) {
			if (rcJitterStrength <= 0.0f) {
				jitterAccumulator = 1.0f;
			} else if (jitterPhase == 0) {
				(void)shouldStartJitterUnit();
			}
			jitterPhase = 0;
			pairIndex = RC_TEMPLATE_COUNT;
			return;
		}

		const bool startJitterUnit = (jitterPhase == 0) && shouldStartJitterUnit();
		const bool rcOverlayActive = (jitterPhase > 0) || startJitterUnit;

		const float observedX = normalizeAxis(gamepad->state.rx);
		const float observedY = normalizeAxis(gamepad->state.ry);
		const Offset center{observedX, observedY};

		Offset velocity{0.0f, 0.0f};
		Offset acceleration{0.0f, 0.0f};
		if (motionHistoryReady) {
			velocity = Offset{center.x - prevCenter.x, center.y - prevCenter.y};
			if (rcOverlayActive) {
				acceleration = Offset{velocity.x - prevVelocity.x, velocity.y - prevVelocity.y};
			}
		} else {
			motionHistoryReady = true;
		}
		prevCenter = center;
		prevVelocity = velocity;

		if (!rcOverlayActive) {
			return;
		}

		Offset offset{0.0f, 0.0f};

		if (startJitterUnit) {
			if (pairIndex >= RC_TEMPLATE_COUNT) {
				generateOffsetBlock();
			}
			const float t = offsetBlock[pairIndex];
			const float span = rcJitterRadius - kRcXSampleMinNorm;
			const float base = kRcXSampleMinNorm + t * span;
			pendingJitterBaseNorm = base;
			radialAmpScaleCached = rcRadialDecayActive
				? radialAmpScaleFromCenter(center.x, center.y)
				: 1.0f;
			offset.x = pendingJitterBaseNorm * radialAmpScaleCached;
			jitterPhase = 1;
		} else if (jitterPhase == 1) {
			offset.x = pendingJitterBaseNorm * radialAmpScaleCached * 1.5f;
			jitterPhase = 2;
		} else if (jitterPhase == 2) {
			offset.x = pendingJitterBaseNorm * radialAmpScaleCached * -1.5f;
			jitterPhase = 3;
		} else if (jitterPhase == 3) {
			offset.x = pendingJitterBaseNorm * radialAmpScaleCached * -1.0f;
			jitterPhase = 0;
			++pairIndex;
			if (pairIndex >= RC_TEMPLATE_COUNT) {
				generateOffsetBlock();
			}
		}

		offset.x = applyAccelerationFeedforward(offset.x, acceleration.x);
		// RX-only overlay: offset.y stays 0; skip Y feedforward (would only call fabs on accel).

		const float outX = std::clamp(center.x + offset.x, -1.0f, 1.0f);
		const float outY = std::clamp(center.y, -1.0f, 1.0f);
		gamepad->state.rx = denormalizeAxis(outX);
		gamepad->state.ry = denormalizeAxis(outY);
	}
}
