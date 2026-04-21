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

	if (options.leftYTriggerButtonMask == 0 && options.rightYTriggerButtonMask == 0) {
		return false;
	}

	return (options.leftYPercent1 != 0.0f) || (options.leftYPercent2 != 0.0f) ||
		(options.rightYPercent1 != 0.0f) || (options.rightYPercent2 != 0.0f);
}

float AxisTiltOverlayInput::getAxisPercent(const AxisTiltOverlayOptions& options, bool isLeft) const {
	const uint32_t activePreset = isLeft ? options.leftYActivePreset : options.rightYActivePreset;
	const bool usePreset2 = (activePreset == 2);

	if (isLeft) {
		return usePreset2 ? options.leftYPercent2 : options.leftYPercent1;
	}

	return usePreset2 ? options.rightYPercent2 : options.rightYPercent1;
}

uint32_t AxisTiltOverlayInput::getAxisTriggerMask(const AxisTiltOverlayOptions& options, bool isLeft) const {
	return isLeft ? options.leftYTriggerButtonMask : options.rightYTriggerButtonMask;
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
	// Intentionally not calling available(): that repeats full startup gating every frame.
	// Here we only need runtime gates; mask/percent checks are cheap in the branches below.
	const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
	const AxisTiltOverlayOptions& options = addonOptions.axisTiltOverlayOptions;
	if (!options.enabled) {
		return;
	}
	if (!addonOptions.ads8332Options.enabled || addonOptions.analogOptions.enabled) {
		return;
	}

	const uint32_t buttons = gamepad->state.buttons;
	const uint32_t leftMask = getAxisTriggerMask(options, true);
	const uint32_t rightMask = getAxisTriggerMask(options, false);

	if ((leftMask != 0) && ((buttons & leftMask) != 0)) {
		gamepad->state.ly = applyPercentDelta(gamepad->state.ly, getAxisPercent(options, true));
	}

	if ((rightMask != 0) && ((buttons & rightMask) != 0)) {
		gamepad->state.ry = applyPercentDelta(gamepad->state.ry, getAxisPercent(options, false));
	}
}
