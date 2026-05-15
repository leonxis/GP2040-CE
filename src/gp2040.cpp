// GP2040 includes
#include "gp2040.h"
#include "helper.h"
#include "system.h"
#include "enums.pb.h"

#include "build_info.h"
#include "peripheralmanager.h"
#include "storagemanager.h"
#include "addonmanager.h"
#include "config.pb.h"
#include "types.h"
#include "usbhostmanager.h"

// Inputs for Core0
#include "addons/analog.h"
#include "addons/unified_analog_processor.h"
#include "addons/unified_voltage_switch.h"
#include "addons/unified_joystick_travel_key.h"
#include "addons/ads8332_adc.h"
#include "addons/lsm6dsr_imu.h"
#include "addons/bootsel_button.h"
#include "addons/focus_mode.h"
#include "addons/dualdirectional.h"
#include "addons/axis_tilt_overlay.h"
#include "addons/tilt.h"
#include "addons/keyboard_host.h"
#include "addons/reverse.h"
#include "addons/turbo.h"
#include "addons/slider_socd.h"
#include "addons/wiiext.h"
#include "addons/input_macro.h"
#include "addons/snes_input.h"
#include "addons/rotaryencoder.h"
#include "addons/i2c_gpio_pcf8575.h"
#include "addons/gamepad_usb_host.h"
#include "addons/he_trigger.h"
#include "addons/linear_trigger.h"
#include "addons/two_key_touchpad.h"
#include "addons/back_button_divider.h"
#include "addons/tg16_input.h"

// Pico includes
#include "pico/bootrom.h"
#include "pico/time.h"
#include "hardware/adc.h"

#include "rndis.h"

// TinyUSB
#include "tusb.h"

// USB Input Class Drivers
#include "drivermanager.h"
#include "usbdriver.h"

static const uint32_t REBOOT_HOTKEY_ACTIVATION_TIME_MS = 50;
static const uint32_t REBOOT_HOTKEY_HOLD_TIME_MS = 4000;
static bool main_loop_gate_enabled = false;
static bool main_loop_gate_runtime_enabled = false;
static bool main_loop_gate_bootstrap_pending = false;
static bool composite_hid_enabled = false;
static const uint32_t CPU_FREQ_ENHANCED_KHZ = 144000;
static const uint32_t MAIN_LOOP_GATE_REPORT_RATE_HZ = 1000;
static uint16_t cached_joystick_mid = GAMEPAD_JOYSTICK_MID;
static float cached_dpad_deadzone = 0.1f;
static float cached_dpad_threshold = 0.1f;
static const uint8_t WEBCONFIG_BOOT_GPIO = 19; //修改为GPIO19
static const uint8_t RUNTIME_HOTKEY_SHARED_GPIO_A = 18;
static const uint8_t RUNTIME_HOTKEY_SHARED_GPIO_B = 19;
static const uint8_t RUNTIME_HOTKEY_WEBCONFIG_GPIO = 21;
static const uint8_t RUNTIME_HOTKEY_USB_BOOT_GPIO = 22;
static const uint8_t RUNTIME_HOTKEY_MODE_X_GPIO = 15;
static const uint8_t RUNTIME_HOTKEY_MODE_O_GPIO = 9;
static const uint8_t RUNTIME_HOTKEY_MODE_SQUARE_GPIO = 13;
static const uint8_t RUNTIME_HOTKEY_MODE_TRIANGLE_GPIO = 14;

enum class RuntimeHotkeyAction {
	NONE,
	INVALID,
	TOGGLE_WEBCONFIG,
	ENTER_USB_BOOTLOADER,
	SWITCH_MODE_X,
	SWITCH_MODE_O,
	SWITCH_MODE_SQUARE,
	SWITCH_MODE_TRIANGLE,
};

extern void processCompositeHID(Gamepad *gamepad);

static inline bool shouldUseMainLoopGate() {
	const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
	const InputMode inputMode = DriverManager::getInstance().getInputMode();
	const bool supportedMode =
		(inputMode == INPUT_MODE_PS4 || inputMode == INPUT_MODE_PS4B || inputMode == INPUT_MODE_SWITCH_PRO ||
		 inputMode == INPUT_MODE_XINPUT || inputMode == INPUT_MODE_XINPUTB);
	return (addonOptions.reportRate == MAIN_LOOP_GATE_REPORT_RATE_HZ) && supportedMode;
}

static inline bool shouldSkipMainLoopFrameForGate(bool configMode) {
	const bool gateEnabledNow = (!configMode && main_loop_gate_enabled);
	if (gateEnabledNow != main_loop_gate_runtime_enabled) {
		main_loop_gate_runtime_enabled = gateEnabledNow;
		// Clear pending marker on each mode transition to avoid stale edge after profile/mode changes.
		usb_reset_main_gamepad_poll_done_state();
		main_loop_gate_bootstrap_pending = gateEnabledNow;
	}

	if (!main_loop_gate_runtime_enabled || usb_main_loop_gate_usb_warmup_active()) {
		return false;
	}

	const bool pollEventPending = usb_consume_main_gamepad_poll_pending();
	const bool runFrame = main_loop_gate_bootstrap_pending || pollEventPending;
	if (runFrame) {
		main_loop_gate_bootstrap_pending = false;
	}
	return !runFrame;
}

const static uint32_t rebootDelayMs = 500;
static absolute_time_t rebootDelayTimeout = nil_time;

static bool isGPIOHeldLow(uint8_t pin) {
	return gpio_get(pin) == 0;
}

static bool isWebConfigBootGPIOPressed() {
	return isGPIOHeldLow(WEBCONFIG_BOOT_GPIO);
}

static RuntimeHotkeyAction getRuntimeHotkeyAction() {
	if (!isGPIOHeldLow(RUNTIME_HOTKEY_SHARED_GPIO_A) || !isGPIOHeldLow(RUNTIME_HOTKEY_SHARED_GPIO_B)) {
		return RuntimeHotkeyAction::NONE;
	}

	const bool webConfigPressed = isGPIOHeldLow(RUNTIME_HOTKEY_WEBCONFIG_GPIO);
	const bool usbBootPressed = isGPIOHeldLow(RUNTIME_HOTKEY_USB_BOOT_GPIO);
	const bool modeXPressed = isGPIOHeldLow(RUNTIME_HOTKEY_MODE_X_GPIO);
	const bool modeOPressed = isGPIOHeldLow(RUNTIME_HOTKEY_MODE_O_GPIO);
	const bool modeSquarePressed = isGPIOHeldLow(RUNTIME_HOTKEY_MODE_SQUARE_GPIO);
	const bool modeTrianglePressed = isGPIOHeldLow(RUNTIME_HOTKEY_MODE_TRIANGLE_GPIO);

	const uint8_t thirdPinPressedCount =
		static_cast<uint8_t>(webConfigPressed) +
		static_cast<uint8_t>(usbBootPressed) +
		static_cast<uint8_t>(modeXPressed) +
		static_cast<uint8_t>(modeOPressed) +
		static_cast<uint8_t>(modeSquarePressed) +
		static_cast<uint8_t>(modeTrianglePressed);

	if (thirdPinPressedCount == 0) {
		return RuntimeHotkeyAction::NONE;
	}

	// Runtime hotkeys only trigger with one third-pin pressed to avoid collisions.
	if (thirdPinPressedCount > 1) {
		return RuntimeHotkeyAction::INVALID;
	}

	if (webConfigPressed) {
		return RuntimeHotkeyAction::TOGGLE_WEBCONFIG;
	}
	if (usbBootPressed) {
		return RuntimeHotkeyAction::ENTER_USB_BOOTLOADER;
	}
	if (modeXPressed) {
		return RuntimeHotkeyAction::SWITCH_MODE_X;
	}
	if (modeOPressed) {
		return RuntimeHotkeyAction::SWITCH_MODE_O;
	}
	if (modeSquarePressed) {
		return RuntimeHotkeyAction::SWITCH_MODE_SQUARE;
	}
	return RuntimeHotkeyAction::SWITCH_MODE_TRIANGLE;
}

static bool isRuntimeSwitchableInputMode(int32_t inputMode) {
	switch (inputMode) {
		case INPUT_MODE_XINPUT:
		case INPUT_MODE_XINPUTB:
		case INPUT_MODE_SWITCH:
		case INPUT_MODE_SWITCH_PRO:
		case INPUT_MODE_KEYBOARD:
		case INPUT_MODE_GENERIC:
		case INPUT_MODE_PS3:
		case INPUT_MODE_PS4:
		case INPUT_MODE_PS4B:
		case INPUT_MODE_PS5:
		case INPUT_MODE_P5GENERAL:
		case INPUT_MODE_NEOGEO:
		case INPUT_MODE_MDMINI:
		case INPUT_MODE_PCEMINI:
		case INPUT_MODE_EGRET:
		case INPUT_MODE_ASTRO:
		case INPUT_MODE_PSCLASSIC:
		case INPUT_MODE_XBOXORIGINAL:
		case INPUT_MODE_XBONE:
			return true;
		default:
			return false;
	}
}

static bool applyRuntimeInputModeSwitch(RuntimeHotkeyAction action) {
	GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();
	int32_t targetInputMode = -1;

	switch (action) {
		case RuntimeHotkeyAction::SWITCH_MODE_X:
			targetInputMode = gamepadOptions.runtimeModeHotkeyX;
			break;
		case RuntimeHotkeyAction::SWITCH_MODE_O:
			targetInputMode = gamepadOptions.runtimeModeHotkeyO;
			break;
		case RuntimeHotkeyAction::SWITCH_MODE_SQUARE:
			targetInputMode = gamepadOptions.runtimeModeHotkeySquare;
			break;
		case RuntimeHotkeyAction::SWITCH_MODE_TRIANGLE:
			targetInputMode = gamepadOptions.runtimeModeHotkeyTriangle;
			break;
		default:
			return false;
	}

	if (!isRuntimeSwitchableInputMode(targetInputMode)) {
		return false;
	}

	if (gamepadOptions.inputMode == targetInputMode) {
		return false;
	}

	// Reuse boot-time mode switch logic on next reboot.
	System::setPendingInputMode(targetInputMode);
	System::reboot(System::BootMode::DEFAULT);
	return true;
}

static void configureWebConfigHotkeyGPIOs() {
	const uint8_t pins[] = {
		WEBCONFIG_BOOT_GPIO,
		RUNTIME_HOTKEY_SHARED_GPIO_A,
		RUNTIME_HOTKEY_SHARED_GPIO_B,
		RUNTIME_HOTKEY_WEBCONFIG_GPIO,
		RUNTIME_HOTKEY_USB_BOOT_GPIO,
		RUNTIME_HOTKEY_MODE_X_GPIO,
		RUNTIME_HOTKEY_MODE_O_GPIO,
		RUNTIME_HOTKEY_MODE_SQUARE_GPIO,
		RUNTIME_HOTKEY_MODE_TRIANGLE_GPIO,
	};
	for (uint8_t i = 0; i < count_of(pins); i++) {
		gpio_init(pins[i]);
		gpio_set_dir(pins[i], GPIO_IN);
		gpio_pull_up(pins[i]);
	}
}

void GP2040::setup() {
	Storage::getInstance().init();

	// Reduce CPU if USB host is enabled
	PeripheralManager::getInstance().initUSB();
	// HML performance mode is fixed on: always run at 144MHz.
	set_sys_clock_khz(CPU_FREQ_ENHANCED_KHZ, true);

	// I2C & SPI rely on the system clock
	PeripheralManager::getInstance().initSPI();
	PeripheralManager::getInstance().initI2C();

	Gamepad * gamepad = new Gamepad();
	Gamepad * processedGamepad = new Gamepad();
	Storage::getInstance().SetGamepad(gamepad);
	Storage::getInstance().SetProcessedGamepad(processedGamepad);

	// Set pin mappings for all GPIO functions
	Storage::getInstance().setFunctionalPinMappings();

	// power up...
	gamepad->auxState.power.pluggedIn = true;
	gamepad->auxState.power.charging = false;
	gamepad->auxState.power.level = GAMEPAD_AUX_MAX_POWER;

	// Setup Gamepad
	gamepad->setup();

	// Initialize last reinit profile to current so we don't reinit on first loop
	gamepad->lastReinitProfileNumber = Storage::getInstance().getGamepadOptions().profileNumber;

	// now we can load the latest configured profile, which will map the
	// new set of GPIOs to use...
	this->initializeStandardGpio();
	configureWebConfigHotkeyGPIOs();

	// Initialize our ADC (various add-ons)
	adc_init();

	// Setup Add-ons
	addons.LoadUSBAddon(new KeyboardHostAddon());
	addons.LoadUSBAddon(new GamepadUSBHostAddon());
	addons.LoadAddon(new AnalogInput());
	addons.LoadAddon(new ADS8332ADCAddon());
	addons.LoadAddon(new UnifiedAnalogProcessorAddon());
	addons.LoadAddon(new UnifiedVoltageSwitchAddon());
	addons.LoadAddon(new UnifiedJoystickTravelKeyAddon());
	addons.LoadAddon(new LSM6DSRIMUAddon());
	addons.LoadAddon(new HETriggerAddon());
	addons.LoadAddon(new LinearTriggerAddon());
	addons.LoadAddon(new TwoKeyTouchpadAddon());
	addons.LoadAddon(new BackButtonDividerAddon());
	addons.LoadAddon(new BootselButtonAddon());
	addons.LoadAddon(new DualDirectionalInput());
	addons.LoadAddon(new FocusModeAddon());
	addons.LoadAddon(new WiiExtensionInput());
	addons.LoadAddon(new SNESpadInput());
	addons.LoadAddon(new SliderSOCDInput());
	addons.LoadAddon(new TiltInput());
	addons.LoadAddon(new RotaryEncoderInput());
	addons.LoadAddon(new PCF8575Addon());
	addons.LoadAddon(new TG16padInput());

	// Input override addons
	addons.LoadAddon(new ReverseInput());
	addons.LoadAddon(new TurboInput()); // Turbo overrides button states and should be close to the end
	addons.LoadAddon(new AxisTiltOverlayInput()); // Must execute after all joystick processing
	addons.LoadAddon(new InputMacro());

	InputMode inputMode = gamepad->getOptions().inputMode;
	const BootAction bootAction = getBootAction();
	switch (bootAction) {
		case BootAction::ENTER_WEBCONFIG_MODE:
			inputMode = INPUT_MODE_CONFIG;
			break;
		case BootAction::ENTER_USB_MODE:
			reset_usb_boot(0, 0);
			return;
		case BootAction::SET_INPUT_MODE_SWITCH:
			inputMode = INPUT_MODE_SWITCH;
			break;
		case BootAction::SET_INPUT_MODE_KEYBOARD:
			inputMode = INPUT_MODE_KEYBOARD;
			break;
		case BootAction::SET_INPUT_MODE_GENERIC:
			inputMode = INPUT_MODE_GENERIC;
			break;
		case BootAction::SET_INPUT_MODE_NEOGEO:
			inputMode = INPUT_MODE_NEOGEO;
			break;
		case BootAction::SET_INPUT_MODE_MDMINI:
			inputMode = INPUT_MODE_MDMINI;
			break;
		case BootAction::SET_INPUT_MODE_PCEMINI:
			inputMode = INPUT_MODE_PCEMINI;
			break;
		case BootAction::SET_INPUT_MODE_EGRET:
			inputMode = INPUT_MODE_EGRET;
			break;
		case BootAction::SET_INPUT_MODE_ASTRO:
			inputMode = INPUT_MODE_ASTRO;
			break;
		case BootAction::SET_INPUT_MODE_PSCLASSIC:
			inputMode = INPUT_MODE_PSCLASSIC;
			break;
		case BootAction::SET_INPUT_MODE_XINPUT: // X-Input Driver
			inputMode = INPUT_MODE_XINPUT;
			break;
		case BootAction::SET_INPUT_MODE_XINPUTB: // X-Input + Composite HID
			inputMode = INPUT_MODE_XINPUTB;
			break;
		case BootAction::SET_INPUT_MODE_PS3: // PS3 (HID with quirks) driver
			inputMode = INPUT_MODE_PS3;
			break;
		case BootAction::SET_INPUT_MODE_PS4: // PS4 / PS5 Driver
			inputMode = INPUT_MODE_PS4;
			break;
		case BootAction::SET_INPUT_MODE_PS5: // PS4 / PS5 Driver
			inputMode = INPUT_MODE_PS5;
			break;
		case BootAction::SET_INPUT_MODE_PS4B: // PS4B Driver (Composite: Gamepad + Keyboard)
			inputMode = INPUT_MODE_PS4B;
			break;
		case BootAction::SET_INPUT_MODE_P5GENERAL:
			inputMode = INPUT_MODE_P5GENERAL;
			break;
		case BootAction::SET_INPUT_MODE_XBONE: // Xbox One Driver
			inputMode = INPUT_MODE_XBONE;
			break;
		case BootAction::SET_INPUT_MODE_XBOXORIGINAL: // Xbox OG Driver
			inputMode = INPUT_MODE_XBOXORIGINAL;
			break;
		case BootAction::SET_INPUT_MODE_SWITCH_PRO:
			inputMode = INPUT_MODE_SWITCH_PRO;
			break;
		case BootAction::NONE:
		default:
			break;
	}

	// Setup USB Driver
	DriverManager::getInstance().setup(inputMode);
	if (inputMode == INPUT_MODE_CONFIG) {
		const AnimationOptions& animationOptions = Storage::getInstance().getAnimationOptions();
		if (animationOptions.has_webConfigAmbientHintEnabled &&
				animationOptions.webConfigAmbientHintEnabled) {
			Storage::getInstance().prepareAmbientWebConfigOverride();
		}
	}
	main_loop_gate_enabled = shouldUseMainLoopGate();
	composite_hid_enabled = (inputMode == INPUT_MODE_XINPUTB || inputMode == INPUT_MODE_PS4B);
	if (DriverManager::getInstance().getDriver() != nullptr) {
		cached_joystick_mid = DriverManager::getInstance().getDriver()->GetJoystickMidValue();
	}
	{
		// Cache profile/config-derived analog swap parameters once at boot.
		const GamepadOptions& options = gamepad->getOptions();
		const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();

		cached_dpad_deadzone = 0.1f;
		if (options.dpadDeadzone > 0 && options.dpadDeadzone <= 90) {
			cached_dpad_deadzone = options.dpadDeadzone / 100.0f;
		} else if (addonOptions.analogOptions.enabled) {
			const float idzNorm =
				analogDeadzoneNormFromRaw(addonOptions.analogOptions.inner_deadzone);
			if (idzNorm > 0.0f) {
				cached_dpad_deadzone = idzNorm;
			}
		}

		cached_dpad_threshold = 0.1f;
		if (options.dpadTriggerThreshold > 0 && options.dpadTriggerThreshold <= 90) {
			cached_dpad_threshold = options.dpadTriggerThreshold / 100.0f;
		}
	}

	// save to match user expectations on choosing mode at boot, and this is
	// before USB host will be used so we can force it to ignore the check
	if (inputMode != INPUT_MODE_CONFIG && inputMode != gamepad->getOptions().inputMode) {
		gamepad->setInputMode(inputMode);
		Storage::getInstance().save(true);
	}

	// register system event handlers
	EventManager::getInstance().registerEventHandler(GP_EVENT_STORAGE_SAVE, GPEVENT_CALLBACK(this->handleStorageSave(event)));
	EventManager::getInstance().registerEventHandler(GP_EVENT_RESTART, GPEVENT_CALLBACK(this->handleSystemReboot(event)));
}

/**
 * @brief Initialize standard input button GPIOs that are present in the currently loaded profile.
 */
void GP2040::initializeStandardGpio() {
	GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
	buttonGpios = 0;
	for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
	{
		// (NONE=-10, RESERVED=-5, ASSIGNED_TO_ADDON=0, everything else is ours)
		if (pinMappings[pin].action > 0)
		{
			gpio_init(pin);             // Initialize pin
			gpio_set_dir(pin, GPIO_IN); // Set as INPUT
			gpio_pull_up(pin);          // Set as PULLUP
			buttonGpios |= 1 << pin;    // mark this pin as mattering for GPIO debouncing
		}
	}
}

/**
 * @brief Deinitialize standard input button GPIOs that are present in the currently loaded profile.
 */
void GP2040::deinitializeStandardGpio() {
	GpioMappingInfo* pinMappings = Storage::getInstance().getProfilePinMappings();
	for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++)
	{
		// (NONE=-10, RESERVED=-5, ASSIGNED_TO_ADDON=0, everything else is ours)
		if (pinMappings[pin].action > 0)
		{
			gpio_deinit(pin);
		}
	}
}

/**
 * @brief Populate a debounced version of gpio_get_all suitable for use for buttons.
 *
 * For GPIO that are assigned to buttons (based on GpioMappings, see GP2040::initializeStandardGpio),
 * we can centralize their debouncing here and provide access to it to button users.
 *
 * For ease of use this provides the mask bitwise NOTed so that callers don't have to. To avoid misuse
 * and to simplify this method, non-button GPIO IS NOT PRESENT in this result. Use gpio_get_all directly
 * instead, if you don't want debounced data.
 */
void GP2040::debounceGpioGetAll() {
	Mask_t raw_gpio = ~gpio_get_all();
	Gamepad* gamepad = Storage::getInstance().GetGamepad();
	// return if state isn't different than the actual
	if (gamepad->debouncedGpio == (raw_gpio & buttonGpios)) return;

	uint32_t debounceDelay = Storage::getInstance().getGamepadOptions().debounceDelay;
	// abort if no delay is configured
	if (debounceDelay == 0) {
		gamepad->debouncedGpio = raw_gpio;
		return;
	}

	uint32_t now = getMillis();
	// check each button use case GPIO for state
	for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
		Mask_t pin_mask = 1 << pin;
		if (buttonGpios & pin_mask) {
			// Allow debouncer to change state if button state changed and debounce delay threshold met
			if ((gamepad->debouncedGpio & pin_mask) != \
					(raw_gpio & pin_mask) && ((now - gpioDebounceTime[pin]) > debounceDelay)) {
				gamepad->debouncedGpio ^= pin_mask;
				gpioDebounceTime[pin] = now;
			}
		}
	}
}

void GP2040::run() {
	bool configMode = DriverManager::getInstance().isConfigMode();
	GPDriver * inputDriver = DriverManager::getInstance().getDriver();
	Gamepad * gamepad = Storage::getInstance().GetGamepad();
	Gamepad * processedGamepad = Storage::getInstance().GetProcessedGamepad();
	GamepadState prevState;

	// Start the TinyUSB Device functionality
	tud_init(TUD_OPT_RHPORT);

	// Initialize our USB manager
	USBHostManager::getInstance().start();

	if (configMode == true ) {
		rndis_init(WEB_CONFIG_HOSTNAME);
	}

	while (1) { // LOOP
		this->getReinitGamepad(gamepad);

		if (shouldSkipMainLoopFrameForGate(configMode)) {
			// Keep host side polling responsive even when main-loop gate skips this frame.
			USBHostManager::getInstance().process();
			tud_task();
			sleep_us(0);
			continue;
		}

		memcpy(&prevState, &gamepad->state, sizeof(GamepadState));

		// Debounce
		debounceGpioGetAll();
		// Read Gamepad
		gamepad->read();

		checkRawState(prevState, gamepad->state);

		// Process USB Host on Core0
		USBHostManager::getInstance().process();

		// Config Loop (Web-Config skips Core0 add-ons)
		if (configMode == true) {
			inputDriver->process(gamepad);
			rebootHotkeys.process(configMode);
			checkSaveRebootState();
			continue;
		}

		// Pre-Process add-ons for MPGS
		addons.PreprocessAddons();

		gamepad->process(); // process through MPGS

		// (Post) Process for add-ons
		addons.ProcessAddons();

		gamepad->hotkey(); 	// check for MPGS hotkeys
		rebootHotkeys.process(configMode);

		// Perform bidirectional swap for analog modes (after addons process)
		// This ensures we use physical joystick values updated by the unified analog processor.
		// Left/Right Analog modes now support bidirectional swap:
		// - Dpad input → Joystick output (already done in gamepad->process())
		// - Joystick input → Dpad output (done here)
		// Skip bidirectional swap if macro is running with stick direction (macro will handle stick values)
		InputMacro* inputMacro = (InputMacro*)addons.GetAddon(InputMacroName);
		bool macroHasStickDirection = (inputMacro != nullptr && inputMacro->hasStickDirection());
		
		DpadMode activeDpadMode = gamepad->getActiveDpadMode();
		if ((activeDpadMode == DpadMode::DPAD_MODE_LEFT_ANALOG || activeDpadMode == DpadMode::DPAD_MODE_RIGHT_ANALOG) && !macroHasStickDirection) {
			// Get the original dpad value before mode conversion
			// dpadOriginal contains the processed dpad value before mode-specific conversion
			uint8_t originalDpad = gamepad->state.dpadOriginal & 0x0F; // Get mode-specific dpad mask
			
			if (activeDpadMode == DpadMode::DPAD_MODE_LEFT_ANALOG) {
				// Save current physical joystick values (after addons processing)
				uint16_t savedLx = gamepad->state.lx;
				uint16_t savedLy = gamepad->state.ly;
				
				// Convert physical joystick (left stick) to dpad
				gamepad->state.dpad = analogToDpad(savedLx, savedLy, cached_joystick_mid, cached_dpad_deadzone, cached_dpad_threshold);
				
				// Convert original dpad input to joystick (left stick)
				// This overwrites the physical joystick value, completing the bidirectional swap
				gamepad->state.lx = dpadToAnalogX(originalDpad);
				gamepad->state.ly = dpadToAnalogY(originalDpad);
			} else if (activeDpadMode == DpadMode::DPAD_MODE_RIGHT_ANALOG) {
				// Save current physical joystick values (after addons processing)
				uint16_t savedRx = gamepad->state.rx;
				uint16_t savedRy = gamepad->state.ry;
				
				// Convert physical joystick (right stick) to dpad
				gamepad->state.dpad = analogToDpad(savedRx, savedRy, cached_joystick_mid, cached_dpad_deadzone, cached_dpad_threshold);
				
				// Convert original dpad input to joystick (right stick)
				// This overwrites the physical joystick value, completing the bidirectional swap
				gamepad->state.rx = dpadToAnalogX(originalDpad);
				gamepad->state.ry = dpadToAnalogY(originalDpad);
			}
		}

		// Apply Y-axis overlay after all joystick transforms are complete.
		AxisTiltOverlayInput* axisTiltOverlay = (AxisTiltOverlayInput*)addons.GetAddon(AxisTiltOverlayName);
		if (axisTiltOverlay != nullptr) {
			axisTiltOverlay->applyFinalProcess(gamepad);
		}

		checkProcessedState(processedGamepad->state, gamepad->state);

		// Copy Processed Gamepad for Core1 (race condition otherwise)
		memcpy(&processedGamepad->state, &gamepad->state, sizeof(GamepadState));

		// Process Input Driver
		bool processed = inputDriver->process(gamepad);
		if (composite_hid_enabled) {
			processCompositeHID(gamepad);
		}

		// TinyUSB Task update (run while awake so host IN poll can be serviced; do not sleep after this).
		tud_task();

		// Post-Process Add-ons with USB Report Processed Sent
		addons.PostprocessAddons(processed);

		// Check if we have a pending save
		checkSaveRebootState();
	}
}

void GP2040::getReinitGamepad(Gamepad * gamepad) {
	GamepadOptions& gamepadOptions = Storage::getInstance().getGamepadOptions();

	// Check if profile has changed since last reinit
	if (gamepad->lastReinitProfileNumber != gamepadOptions.profileNumber) {
		uint32_t previousProfile = gamepad->lastReinitProfileNumber;
		uint32_t currentProfile = gamepadOptions.profileNumber;

		// deinitialize the ordinary (non-reserved, non-addon) GPIO pins, since
		// we are moving off of them and onto potentially different pin assignments
		// we currently don't support ASSIGNED_TO_ADDON pins being reinitialized,
		// but if they were to be, that'd be the addon's duty, not ours
		this->deinitializeStandardGpio();

		// now we can load the latest configured profile, which will map the
		// new set of GPIOs to use...
		Storage::getInstance().setFunctionalPinMappings();

		// ...and initialize the pins again
		this->initializeStandardGpio();
		configureWebConfigHotkeyGPIOs();

		// now we can tell the gamepad that the new mappings are in place
		// and ready to use, and the pins are ready, so it should reinitialize itself
		gamepad->reinit();

		// ...and addons on this core, if they implemented reinit (just things
		// with simple GPIO pin usage, at time of writing)
		addons.ReinitializeAddons();

		// Update the last reinit profile
		gamepad->lastReinitProfileNumber = currentProfile;

		// Trigger the profile change event now that reinit is complete
		EventManager::getInstance().triggerEvent(new GPProfileChangeEvent(previousProfile, currentProfile));
	}
}

GP2040::BootAction GP2040::getBootAction() {
	const System::BootMode bootMode = System::takeBootMode();
	if (bootMode == System::BootMode::DEFAULT) {
		const int32_t pendingInputMode = System::takePendingInputMode();
		if (pendingInputMode >= 0) {
			return bootActionFromInputMode(pendingInputMode);
		}
	}

	switch (bootMode) {
		case System::BootMode::GAMEPAD: return BootAction::NONE;
		case System::BootMode::WEBCONFIG: return BootAction::ENTER_WEBCONFIG_MODE;
		case System::BootMode::USB: return BootAction::ENTER_USB_MODE;
		case System::BootMode::DEFAULT:
			{
				// Determine boot action based on gamepad state during boot
				Gamepad * gamepad = Storage::getInstance().GetGamepad();
				Gamepad * processedGamepad = Storage::getInstance().GetProcessedGamepad();

				debounceGpioGetAll();
				gamepad->read();

				// Pre-Process add-ons for MPGS
				addons.PreprocessAddons();

				gamepad->process(); // process through MPGS

				// Process for add-ons
				addons.ProcessAddons();

				// Copy Processed Gamepad for Core1 (race condition otherwise)
				memcpy(&processedGamepad->state, &gamepad->state, sizeof(GamepadState));

				if (gamepad->pressedS1() && gamepad->pressedS2() && gamepad->pressedUp()) {
					return BootAction::ENTER_USB_MODE;
				} else if (isWebConfigBootGPIOPressed()) {
					return BootAction::ENTER_WEBCONFIG_MODE;
                }

				break;
			}
	}

	return BootAction::NONE;
}

GP2040::BootAction GP2040::bootActionFromInputMode(int32_t inputMode) {
	switch (inputMode) {
		case INPUT_MODE_XINPUT:
			return BootAction::SET_INPUT_MODE_XINPUT;
		case INPUT_MODE_XINPUTB:
			return BootAction::SET_INPUT_MODE_XINPUTB;
		case INPUT_MODE_SWITCH:
			return BootAction::SET_INPUT_MODE_SWITCH;
		case INPUT_MODE_KEYBOARD:
			return BootAction::SET_INPUT_MODE_KEYBOARD;
		case INPUT_MODE_GENERIC:
			return BootAction::SET_INPUT_MODE_GENERIC;
		case INPUT_MODE_PS3:
			return BootAction::SET_INPUT_MODE_PS3;
		case INPUT_MODE_PS4:
			return BootAction::SET_INPUT_MODE_PS4;
		case INPUT_MODE_PS4B:
			return BootAction::SET_INPUT_MODE_PS4B;
		case INPUT_MODE_PS5:
			return BootAction::SET_INPUT_MODE_PS5;
		case INPUT_MODE_P5GENERAL:
			return BootAction::SET_INPUT_MODE_P5GENERAL;
		case INPUT_MODE_NEOGEO:
			return BootAction::SET_INPUT_MODE_NEOGEO;
		case INPUT_MODE_MDMINI:
			return BootAction::SET_INPUT_MODE_MDMINI;
		case INPUT_MODE_PCEMINI:
			return BootAction::SET_INPUT_MODE_PCEMINI;
		case INPUT_MODE_EGRET:
			return BootAction::SET_INPUT_MODE_EGRET;
		case INPUT_MODE_ASTRO:
			return BootAction::SET_INPUT_MODE_ASTRO;
		case INPUT_MODE_PSCLASSIC:
			return BootAction::SET_INPUT_MODE_PSCLASSIC;
		case INPUT_MODE_XBOXORIGINAL:
			return BootAction::SET_INPUT_MODE_XBOXORIGINAL;
		case INPUT_MODE_XBONE:
			return BootAction::SET_INPUT_MODE_XBONE;
		case INPUT_MODE_SWITCH_PRO:
			return BootAction::SET_INPUT_MODE_SWITCH_PRO;
		default:
			return BootAction::NONE;
	}
}

GP2040::RebootHotkeys::RebootHotkeys() :
	active(false),
	waitForHotkeyRelease(false),
	noButtonsPressedTimeout(nil_time),
	rebootHotkeysHoldTimeout(nil_time) {
}

void GP2040::RebootHotkeys::process(bool configMode) {
	// We only allow the hotkey to trigger after we observed no buttons pressed for a certain period of time.
	// We do this to avoid detecting buttons that are held during the boot process. In particular we want to avoid
	// oscillating between webconfig and default mode when the user keeps holding the hotkey buttons.
	const RuntimeHotkeyAction runtimeHotkeyAction = getRuntimeHotkeyAction();
	const bool runtimeHotkeyPressed = (runtimeHotkeyAction != RuntimeHotkeyAction::NONE);

	if (!active) {
		if (!runtimeHotkeyPressed) {
			if (is_nil_time(noButtonsPressedTimeout)) {
				noButtonsPressedTimeout = make_timeout_time_us(REBOOT_HOTKEY_ACTIVATION_TIME_MS);
			}

			if (time_reached(noButtonsPressedTimeout)) {
				active = true;
			}
		} else {
			noButtonsPressedTimeout = nil_time;
		}
	} else {
		// Do not retrigger until the full combo is released.
		if (waitForHotkeyRelease) {
			if (!runtimeHotkeyPressed) {
				waitForHotkeyRelease = false;
			}
			rebootHotkeysHoldTimeout = nil_time;
			return;
		}

		if (runtimeHotkeyAction == RuntimeHotkeyAction::TOGGLE_WEBCONFIG ||
			runtimeHotkeyAction == RuntimeHotkeyAction::ENTER_USB_BOOTLOADER ||
			runtimeHotkeyAction == RuntimeHotkeyAction::SWITCH_MODE_X ||
			runtimeHotkeyAction == RuntimeHotkeyAction::SWITCH_MODE_O ||
			runtimeHotkeyAction == RuntimeHotkeyAction::SWITCH_MODE_SQUARE ||
			runtimeHotkeyAction == RuntimeHotkeyAction::SWITCH_MODE_TRIANGLE) {
			if (is_nil_time(rebootHotkeysHoldTimeout)) {
				rebootHotkeysHoldTimeout = make_timeout_time_ms(REBOOT_HOTKEY_HOLD_TIME_MS);
			}

			if (time_reached(rebootHotkeysHoldTimeout)) {
				waitForHotkeyRelease = true;
				if (runtimeHotkeyAction == RuntimeHotkeyAction::TOGGLE_WEBCONFIG) {
					// If we are in webconfig mode we go to gamepad mode and vice versa
					System::reboot(configMode ? System::BootMode::GAMEPAD : System::BootMode::WEBCONFIG);
				} else if (runtimeHotkeyAction == RuntimeHotkeyAction::ENTER_USB_BOOTLOADER) {
					System::reboot(System::BootMode::USB);
				} else if (!applyRuntimeInputModeSwitch(runtimeHotkeyAction)) {
					// Unsupported/disabled input mode target. Require release before retry.
					rebootHotkeysHoldTimeout = nil_time;
				}
			}
		} else {
			// Either no runtime hotkey combo is held or the combo is invalid (multiple third-pins).
			rebootHotkeysHoldTimeout = nil_time;
		}
	}
}

void GP2040::checkRawState(const GamepadState& prevState, const GamepadState& currState) {
    // buttons pressed
    if (
        ((currState.aux & ~prevState.aux) != 0) ||
        ((currState.dpad & ~prevState.dpad) != 0) ||
        ((currState.buttons & ~prevState.buttons) != 0)
    ) {
        EventManager::getInstance().triggerEvent(new GPButtonDownEvent((currState.dpad & ~prevState.dpad), (currState.buttons & ~prevState.buttons), (currState.aux & ~prevState.aux)));
    }

    // buttons released
    if (
        ((prevState.aux & ~currState.aux) != 0) ||
        ((prevState.dpad & ~currState.dpad) != 0) ||
        ((prevState.buttons & ~currState.buttons) != 0)
    ) {
        EventManager::getInstance().triggerEvent(new GPButtonUpEvent((prevState.dpad & ~currState.dpad), (prevState.buttons & ~currState.buttons), (prevState.aux & ~currState.aux)));
    }
}

void GP2040::checkProcessedState(const GamepadState& prevState, const GamepadState& currState) {
    // buttons pressed
    if (
        ((currState.aux & ~prevState.aux) != 0) ||
        ((currState.dpad & ~prevState.dpad) != 0) ||
        ((currState.buttons & ~prevState.buttons) != 0)
    ) {
        EventManager::getInstance().triggerEvent(new GPButtonProcessedDownEvent((currState.dpad & ~prevState.dpad), (currState.buttons & ~prevState.buttons), (currState.aux & ~prevState.aux)));
    }

    // buttons released
    if (
        ((prevState.aux & ~currState.aux) != 0) ||
        ((prevState.dpad & ~currState.dpad) != 0) ||
        ((prevState.buttons & ~currState.buttons) != 0)
    ) {
        EventManager::getInstance().triggerEvent(new GPButtonProcessedUpEvent((prevState.dpad & ~currState.dpad), (prevState.buttons & ~currState.buttons), (prevState.aux & ~currState.aux)));
    }

    if (
        (currState.lx != prevState.lx) ||
        (currState.ly != prevState.ly) ||
        (currState.rx != prevState.rx) ||
        (currState.ry != prevState.ry) ||
        (currState.lt != prevState.lt) ||
        (currState.rt != prevState.rt)
    ) {
        EventManager::getInstance().triggerEvent(new GPAnalogProcessedMoveEvent(currState.lx, currState.ly, currState.rx, currState.ry, currState.lt, currState.rt));
    }
}

void GP2040::checkSaveRebootState() {
	if (saveRequested) {
		saveRequested = false;
		Storage::getInstance().save(forceSave);
	}

	if (rebootRequested) {
		rebootRequested = false;
		rebootDelayTimeout = make_timeout_time_ms(rebootDelayMs);
	}

	if (!is_nil_time(rebootDelayTimeout) && time_reached(rebootDelayTimeout)) {
		System::reboot(rebootMode);
	}
}

void GP2040::handleStorageSave(GPEvent* e) {
	saveRequested = true;
	forceSave = ((GPStorageSaveEvent*)e)->forceSave;
	rebootRequested = ((GPStorageSaveEvent*)e)->restartAfterSave;
	rebootMode = System::BootMode::DEFAULT;
}

void GP2040::handleSystemReboot(GPEvent* e) {
	rebootRequested = true;
	rebootMode = ((GPRestartEvent*)e)->bootMode;
}
