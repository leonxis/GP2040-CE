#include "addons/axis_tilt_overlay.h"

#include "config.pb.h"
#include "storagemanager.h"

#include <algorithm>
#include <cmath>

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
		// RC gain feature placeholder (intentionally no-op for now).
	}
}
