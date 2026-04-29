#include "addons/axis_tilt_overlay.h"

#include "config.pb.h"
#include "storagemanager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
constexpr float kMotionFeedforwardGain = 12.0f;
constexpr float kMotionFeedforwardDeadzone = 0.0015f;
constexpr bool kUsePairedBlockAlgorithm = false;
}

bool AxisTiltOverlayInput::available() {
	const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
	const AxisTiltOverlayOptions& options = addonOptions.axisTiltOverlayOptions;

	if (!options.enabled) {
		return false;
	}

	if (!addonOptions.ads8332Options.enabled || addonOptions.analogOptions.enabled) {
		return false;
	}

	// Plugin is available when master switch is on and at least one sub-feature is enabled.
	return options.pressEnabled || options.rcGainEnabled;
}

void AxisTiltOverlayInput::setup() {
	rcOptionsDirty = true;
	reinit();
}

void AxisTiltOverlayInput::reinit() {
	rcOptionsDirty = true;
	resetRcState();
}

void AxisTiltOverlayInput::resetRcState() {
	motionHistoryReady = false;
	prevCenter = Offset{0.0f, 0.0f};
	prevVelocity = Offset{0.0f, 0.0f};
	rngState = 0xA53C9E17u;
	jitterAccumulator = 1.0f;
	blockIndex = RC_BLOCK_SIZE;
}

void AxisTiltOverlayInput::loadRcOptionsIfDirty(const AxisTiltOverlayOptions& options) {
	const bool optionsChanged = (options.rcGainReserved1 != rcRawReserved1) ||
		(options.rcGainReserved2 != rcRawReserved2) ||
		(options.rcGainReserved3 != rcRawReserved3);
	if (!rcOptionsDirty && !optionsChanged) {
		return;
	}

	rcRawReserved1 = options.rcGainReserved1;
	rcRawReserved2 = options.rcGainReserved2;
	rcRawReserved3 = options.rcGainReserved3;

	rcJitterStrength = std::clamp(rcRawReserved1 / 100.0f, 0.0f, 1.0f);
	rcDiamondA = std::clamp(rcRawReserved2 / 100.0f, 0.0f, 1.0f);
	rcDiamondB = std::clamp(rcRawReserved3 / 100.0f, 0.0f, 1.0f);
	if (rcDiamondB > rcDiamondA) {
		std::swap(rcDiamondA, rcDiamondB);
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

float AxisTiltOverlayInput::randomFloat(float minValue, float maxValue) {
	constexpr float kInv24 = 1.0f / 16777215.0f;
	const float t = static_cast<float>(randomU32() & 0x00FFFFFFu) * kInv24;
	return minValue + (maxValue - minValue) * t;
}

AxisTiltOverlayInput::Offset AxisTiltOverlayInput::randomDiamondOffset() {
	float u = 0.0f;
	float v = 0.0f;
	do {
		u = randomFloat(-1.0f, 1.0f);
		v = randomFloat(-1.0f, 1.0f);
	} while ((std::fabs(u) + std::fabs(v)) > 1.0f);

	return Offset{u * rcDiamondA, v * rcDiamondB};
}

void AxisTiltOverlayInput::generateBlockClassic() {
	const uint16_t halfSize = RC_BLOCK_SIZE / 2;
	for (uint16_t i = 0; i < halfSize; ++i) {
		const Offset q = randomDiamondOffset();
		offsetBlock[i] = q;
		offsetBlock[i + halfSize] = Offset{-q.x, -q.y};
	}

	for (uint16_t i = RC_BLOCK_SIZE - 1; i > 0; --i) {
		const uint16_t j = static_cast<uint16_t>(randomU32() % (i + 1));
		std::swap(offsetBlock[i], offsetBlock[j]);
	}
	blockIndex = 0;
}

void AxisTiltOverlayInput::generateBlockPaired() {
	uint16_t outIndex = 0;
	while (outIndex < RC_BLOCK_SIZE) {
		const Offset q = randomDiamondOffset();
		offsetBlock[outIndex++] = q;
		offsetBlock[outIndex++] = Offset{-q.x, -q.y};
	}
	blockIndex = 0;
}

void AxisTiltOverlayInput::generateBlock() {
	if (kUsePairedBlockAlgorithm) {
		generateBlockPaired();
	} else {
		generateBlockClassic();
	}
}

bool AxisTiltOverlayInput::shouldApplyJitter() {
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

AxisTiltOverlayInput::Offset AxisTiltOverlayInput::resolveCenter(float observedX, float observedY) const {
	// Center snapping is intentionally disabled: always use the live observed stick point.
	return Offset{observedX, observedY};
}

float AxisTiltOverlayInput::normalizeAxis(uint16_t v) const {
	if (v >= GAMEPAD_JOYSTICK_MID) {
		return std::clamp(
			static_cast<float>(v - GAMEPAD_JOYSTICK_MID) /
			static_cast<float>(GAMEPAD_JOYSTICK_MAX - GAMEPAD_JOYSTICK_MID),
			0.0f,
			1.0f
		);
	}
	return std::clamp(
		-static_cast<float>(GAMEPAD_JOYSTICK_MID - v) /
		static_cast<float>(GAMEPAD_JOYSTICK_MID - GAMEPAD_JOYSTICK_MIN),
		-1.0f,
		0.0f
	);
}

uint16_t AxisTiltOverlayInput::denormalizeAxis(float v) const {
	v = std::clamp(v, -1.0f, 1.0f);
	if (v >= 0.0f) {
		const float raw = static_cast<float>(GAMEPAD_JOYSTICK_MID) +
			v * static_cast<float>(GAMEPAD_JOYSTICK_MAX - GAMEPAD_JOYSTICK_MID);
		return static_cast<uint16_t>(std::lround(std::clamp(
			raw,
			static_cast<float>(GAMEPAD_JOYSTICK_MID),
			static_cast<float>(GAMEPAD_JOYSTICK_MAX)
		)));
	}

	const float raw = static_cast<float>(GAMEPAD_JOYSTICK_MID) +
		v * static_cast<float>(GAMEPAD_JOYSTICK_MID - GAMEPAD_JOYSTICK_MIN);
	return static_cast<uint16_t>(std::lround(std::clamp(
		raw,
		static_cast<float>(GAMEPAD_JOYSTICK_MIN),
		static_cast<float>(GAMEPAD_JOYSTICK_MID)
	)));
}

float AxisTiltOverlayInput::getRightYOverlayPercent(const AxisTiltOverlayOptions& options) const {
	switch (options.rightYActivePreset) {
		case 0:
			return 0.0f;
		case 2:
			return options.rightYPercent2;
		case 3:
			return options.rightYPercent3;
		case 1:
		default:
			return options.rightYPercent1;
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

	const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
	const AxisTiltOverlayOptions& options = addonOptions.axisTiltOverlayOptions;
	if (!options.enabled) {
		return;
	}
	if (!addonOptions.ads8332Options.enabled || addonOptions.analogOptions.enabled) {
		return;
	}
	if (!options.pressEnabled && !options.rcGainEnabled) {
		return;
	}

	const bool anyPressPercent = (options.rightYPercent1 != 0.0f) || (options.rightYPercent2 != 0.0f) ||
		(options.rightYPercent3 != 0.0f);
	const bool pressFeatureEnabled = options.pressEnabled && anyPressPercent;
	const bool rcGainFeatureEnabled = options.rcGainEnabled;

	const uint32_t buttons = gamepad->state.buttons;

	if (pressFeatureEnabled) {
		const uint32_t rightMask = options.rightYTriggerButtonMask;
		if ((rightMask != 0) && ((buttons & rightMask) != 0)) {
			gamepad->state.ry = applyPercentDelta(gamepad->state.ry, getRightYOverlayPercent(options));
		}
	}

	if (rcGainFeatureEnabled) {
		const uint32_t rcMask = options.rcGainTriggerButtonMask;
		const bool rcTriggeredByButton = (rcMask != 0) && ((buttons & rcMask) != 0);
		const bool rcTriggered = options.rcGainAlwaysOn || rcTriggeredByButton;
		if (!rcTriggered) {
			// Reset motion history when trigger is released to avoid stale velocity/acceleration
			// bursts when RC is engaged again later.
			motionHistoryReady = false;
			prevCenter = Offset{0.0f, 0.0f};
			prevVelocity = Offset{0.0f, 0.0f};
			return;
		}

		loadRcOptionsIfDirty(options);
		const bool rcHasEffect = (rcJitterStrength > 0.0f) && ((rcDiamondA > 0.0f) || (rcDiamondB > 0.0f));
		if (!rcHasEffect) {
			return;
		}

		const float observedX = normalizeAxis(gamepad->state.rx);
		const float observedY = normalizeAxis(gamepad->state.ry);
		const Offset center = resolveCenter(observedX, observedY);

		Offset velocity{0.0f, 0.0f};
		Offset acceleration{0.0f, 0.0f};
		if (motionHistoryReady) {
			velocity = Offset{center.x - prevCenter.x, center.y - prevCenter.y};
			acceleration = Offset{velocity.x - prevVelocity.x, velocity.y - prevVelocity.y};
		} else {
			motionHistoryReady = true;
		}
		prevCenter = center;
		prevVelocity = velocity;

		Offset offset{0.0f, 0.0f};
		if (shouldApplyJitter()) {
			if (blockIndex >= RC_BLOCK_SIZE) {
				generateBlock();
			}
			offset = offsetBlock[blockIndex++];

			const auto applyAccelerationFeedforward = [](float offsetValue, float accelValue) -> float {
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
			};

			offset.x = applyAccelerationFeedforward(offset.x, acceleration.x);
			offset.y = applyAccelerationFeedforward(offset.y, acceleration.y);
		}

		const float outX = std::clamp(center.x + offset.x, -1.0f, 1.0f);
		const float outY = std::clamp(center.y + offset.y, -1.0f, 1.0f);
		gamepad->state.rx = denormalizeAxis(outX);
		gamepad->state.ry = denormalizeAxis(outY);
	}
}
