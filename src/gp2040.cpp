// GP2040 includes
#include "gp2040.h"
#include "helper.h"
#include "system.h"
#include "enums.pb.h"

#include "build_info.h"
#include "peripheralmanager.h"
#include "storagemanager.h"
#include "hml_back_mapping_preset.h"
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
#include "addons/mcp3208_adc.h"
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
static bool composite_hid_enabled = false;
static const uint32_t CPU_FREQ_ENHANCED_KHZ = 144000;
static const uint32_t MAIN_LOOP_GATE_REPORT_RATE_HZ = 1000;
static const uint32_t MAIN_LOOP_GATE_WAIT_TIMEOUT_US = 1000;
static const uint32_t MAIN_LOOP_GATE_SUSPEND_SCAN_US = 4000;
static const uint32_t MAIN_LOOP_GATE_INTERVAL_2MS_US = 1500;
static const uint32_t MAIN_LOOP_GATE_USB_FRAME_US = 1000;
static const uint32_t MAIN_LOOP_GATE_PHASE_SPREAD_LIMIT_US = 125;
static const uint32_t MAIN_LOOP_GATE_COMPLETION_TO_TOKEN_GUARD_US = 64;
static const uint32_t MAIN_LOOP_GATE_ENDPOINT_READY_GUARD_US = 16;
static const uint32_t MAIN_LOOP_GATE_WCET_MARGIN_US = 16;
static const uint8_t MAIN_LOOP_GATE_WCET_WINDOW = 64;
static const uint16_t MAIN_LOOP_GATE_LOCK_COMPLETIONS = 128;
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

enum class MainLoopGateState {
	WAIT_MOUNT,
	BOOTSTRAP_BUILD,
	BOOTSTRAP_SUBMIT,
	WAIT_FIRST_IN,
	LEARNING,
	LOCKED,
	RECOVERY,
	SUSPENDED_ARMED,
	SUSPENDED_UNARMED,
};

enum class MainLoopGateAction {
	RunFrame,
	RetrySubmit,
	WaitUSB,
	ScanSuspended,
};

enum class MainLoopGateCompletionResult {
        Baseline,
        Normal,
        IntervalMutation,
        PhaseMutation,
};

struct MainLoopGateRollingMax {
        uint32_t samples[MAIN_LOOP_GATE_WCET_WINDOW] = {};
        uint8_t nextIndex = 0;
        uint8_t count = 0;
        uint32_t maximum = 0;

        void reset() {
                for (uint8_t i = 0; i < MAIN_LOOP_GATE_WCET_WINDOW; i++) {
                        samples[i] = 0;
                }
                nextIndex = 0;
                count = 0;
                maximum = 0;
        }

        void record(uint32_t value) {
                const bool full = count == MAIN_LOOP_GATE_WCET_WINDOW;
                const uint32_t replaced = full ? samples[nextIndex] : 0;
                samples[nextIndex] = value;
                nextIndex++;
                if (nextIndex == MAIN_LOOP_GATE_WCET_WINDOW) {
                        nextIndex = 0;
                }
                if (!full) {
                        count++;
                }
                if (value >= maximum) {
                        maximum = value;
                        return;
                }
                if (full && replaced == maximum) {
                        maximum = 0;
                        for (uint8_t i = 0; i < count; i++) {
                                if (samples[i] > maximum) {
                                        maximum = samples[i];
                                }
                        }
                }
        }
};

struct MainLoopGateFrameSchedule {
        bool valid = false;
        uint32_t nextTokenEarliestUs = 0;
        uint32_t armDeadlineUs = 0;
        uint32_t finalizeDeadlineUs = 0;
};

struct MainLoopGateLateSampleResult {
        uint8_t sampleSets = 0;
        bool busTouched = false;
        bool deadlineOverrun = false;
};

static MainLoopGateState main_loop_gate_state = MainLoopGateState::WAIT_MOUNT;
static uint32_t main_loop_gate_epoch = 0;
static uint32_t main_loop_gate_complete_seq = 0;
static uint32_t main_loop_gate_failed_seq = 0;
static uint32_t main_loop_gate_action_epoch = 0;
static bool main_loop_gate_first_in_seen = false;
static bool main_loop_gate_have_completion_timing = false;
static uint32_t main_loop_gate_last_complete_time_us = 0;
static uint32_t main_loop_gate_last_complete_sof_frame = 0;
static uint32_t main_loop_gate_phase_min_us = 0xFFFFFFFFu;
static uint32_t main_loop_gate_phase_max_us = 0;
static uint32_t main_loop_gate_interval_max_us = 0;
static uint16_t main_loop_gate_stable_completions = 0;
static GateLateAnalogSource main_loop_gate_analog_source =
        GateLateAnalogSource::None;
static MainLoopGateRollingMax main_loop_gate_ads8332_burst_setup_wcet;
static MainLoopGateRollingMax main_loop_gate_ads8332_sample_wcet;
static MainLoopGateRollingMax main_loop_gate_mcp3208_burst_setup_wcet;
static MainLoopGateRollingMax main_loop_gate_mcp3208_sample_wcet;
static MainLoopGateRollingMax main_loop_gate_final_process_wcet;
static MainLoopGateRollingMax main_loop_gate_endpoint_arm_wcet;
static MainLoopGateFrameSchedule main_loop_gate_frame_schedule;
static uint32_t main_loop_gate_sample_age_last_us = 0;
static uint32_t main_loop_gate_sample_age_max_us = 0;
static uint32_t main_loop_gate_deadline_miss_count = 0;
static uint32_t main_loop_gate_phase_mutation_count = 0;
static uint32_t main_loop_gate_late_sample_set_count = 0;
static uint32_t main_loop_gate_repeated_sample_frame_count = 0;
static uint32_t main_loop_gate_frame_without_fresh_sample_count = 0;
static uint32_t main_loop_gate_max_sample_sets_per_frame = 0;
static bool main_loop_suspend_scan_initialized = false;
static uint32_t main_loop_suspend_last_gpio = 0;
static absolute_time_t main_loop_suspend_next_scan = nil_time;

extern void processCompositeHID(Gamepad *gamepad);

static inline bool mainLoopGateTimeReached(
        uint32_t nowUs,
        uint32_t deadlineUs) {
        return static_cast<int32_t>(nowUs - deadlineUs) >= 0;
}

static inline uint32_t mainLoopGateTimeRemaining(
        uint32_t nowUs,
        uint32_t deadlineUs) {
        const int32_t remaining =
                static_cast<int32_t>(deadlineUs - nowUs);
        return remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
}

static MainLoopGateRollingMax* mainLoopGateADCWcet(
        GateLateAnalogSource source) {
        switch (source) {
                case GateLateAnalogSource::ADS8332:
                        return &main_loop_gate_ads8332_sample_wcet;
                case GateLateAnalogSource::MCP3208:
                        return &main_loop_gate_mcp3208_sample_wcet;
                case GateLateAnalogSource::None:
                default:
                        return nullptr;
        }
}

static MainLoopGateRollingMax* mainLoopGateADCBurstSetupWcet(
        GateLateAnalogSource source) {
        switch (source) {
                case GateLateAnalogSource::ADS8332:
                        return &main_loop_gate_ads8332_burst_setup_wcet;
                case GateLateAnalogSource::MCP3208:
                        return &main_loop_gate_mcp3208_burst_setup_wcet;
                case GateLateAnalogSource::None:
                default:
                        return nullptr;
        }
}

static bool mainLoopGateSchedulingMeasurementsReady() {
        if (main_loop_gate_final_process_wcet.maximum == 0 ||
                main_loop_gate_endpoint_arm_wcet.maximum == 0) {
                return false;
        }
        MainLoopGateRollingMax* adcWcet =
                mainLoopGateADCWcet(main_loop_gate_analog_source);
        MainLoopGateRollingMax* burstSetupWcet =
                mainLoopGateADCBurstSetupWcet(main_loop_gate_analog_source);
        return adcWcet == nullptr ||
                (adcWcet->maximum != 0 &&
                 burstSetupWcet != nullptr &&
                 burstSetupWcet->maximum != 0);
}

static void resetMainLoopGateMeasurements() {
        main_loop_gate_ads8332_burst_setup_wcet.reset();
        main_loop_gate_ads8332_sample_wcet.reset();
        main_loop_gate_mcp3208_burst_setup_wcet.reset();
        main_loop_gate_mcp3208_sample_wcet.reset();
        main_loop_gate_final_process_wcet.reset();
        main_loop_gate_endpoint_arm_wcet.reset();
        main_loop_gate_frame_schedule = {};
        main_loop_gate_sample_age_last_us = 0;
        main_loop_gate_sample_age_max_us = 0;
        main_loop_gate_deadline_miss_count = 0;
        main_loop_gate_phase_mutation_count = 0;
        main_loop_gate_late_sample_set_count = 0;
        main_loop_gate_repeated_sample_frame_count = 0;
        main_loop_gate_frame_without_fresh_sample_count = 0;
        main_loop_gate_max_sample_sets_per_frame = 0;
}

void getMainLoopGateStats(MainLoopGateStats* stats) {
        if (stats == nullptr) {
                return;
        }
        stats->deadlineSchedulingActive =
                main_loop_gate_state == MainLoopGateState::LOCKED &&
                main_loop_gate_frame_schedule.valid;
        stats->analogSource = main_loop_gate_analog_source;
        stats->stableCompletions = main_loop_gate_stable_completions;
        stats->phaseMinUs =
                main_loop_gate_phase_min_us == 0xFFFFFFFFu
                        ? 0
                        : main_loop_gate_phase_min_us;
        stats->phaseMaxUs = main_loop_gate_phase_max_us;
        stats->ads8332BurstSetupWcetUs =
                main_loop_gate_ads8332_burst_setup_wcet.maximum;
        stats->ads8332SampleWcetUs =
                main_loop_gate_ads8332_sample_wcet.maximum;
        stats->mcp3208BurstSetupWcetUs =
                main_loop_gate_mcp3208_burst_setup_wcet.maximum;
        stats->mcp3208SampleWcetUs =
                main_loop_gate_mcp3208_sample_wcet.maximum;
        stats->finalProcessWcetUs =
                main_loop_gate_final_process_wcet.maximum;
        stats->endpointArmGuardUs =
                main_loop_gate_endpoint_arm_wcet.maximum +
                MAIN_LOOP_GATE_WCET_MARGIN_US +
                MAIN_LOOP_GATE_ENDPOINT_READY_GUARD_US;
        stats->sampleAgeLastUs = main_loop_gate_sample_age_last_us;
        stats->sampleAgeMaxUs = main_loop_gate_sample_age_max_us;
        stats->deadlineMissCount = main_loop_gate_deadline_miss_count;
        stats->phaseMutationCount = main_loop_gate_phase_mutation_count;
        stats->lateSampleSetCount =
                main_loop_gate_late_sample_set_count;
        stats->repeatedSampleFrameCount =
                main_loop_gate_repeated_sample_frame_count;
        stats->frameWithoutFreshSampleCount =
                main_loop_gate_frame_without_fresh_sample_count;
        stats->maxSampleSetsPerFrame =
                main_loop_gate_max_sample_sets_per_frame;
        stats->nextTokenEarliestUs =
                main_loop_gate_frame_schedule.nextTokenEarliestUs;
        stats->finalizeDeadlineUs =
                main_loop_gate_frame_schedule.finalizeDeadlineUs;
}

static inline bool shouldUseMainLoopGate() {
	const AddonOptions& addonOptions = Storage::getInstance().getAddonOptions();
	const InputMode inputMode = DriverManager::getInstance().getInputMode();
	// Note: SWITCH_PRO (NS PRO) is excluded from main-loop gating because its
	// handshake/feature report phases do not consistently trigger HID IN
	// completions, causing the gate to stall the main loop and drop inputs.
	const bool supportedMode =
		(inputMode == INPUT_MODE_PS4 || inputMode == INPUT_MODE_PS4B ||
		 inputMode == INPUT_MODE_XINPUT || inputMode == INPUT_MODE_XINPUTB);
	return (addonOptions.reportRate == MAIN_LOOP_GATE_REPORT_RATE_HZ) && supportedMode;
}

static void resetMainLoopGateTiming() {
	main_loop_gate_have_completion_timing = false;
	main_loop_gate_last_complete_time_us = 0;
	main_loop_gate_last_complete_sof_frame = 0;
	main_loop_gate_phase_min_us = 0xFFFFFFFFu;
	main_loop_gate_phase_max_us = 0;
	main_loop_gate_interval_max_us = 0;
	main_loop_gate_stable_completions = 0;
        main_loop_gate_frame_schedule = {};
}

static MainLoopGateCompletionResult recordMainLoopGateCompletion(
	const USBMainGamepadGateSnapshot& snapshot) {
	const uint32_t phaseUs =
		snapshot.completeTimeUs - snapshot.completeSofTimeUs;
        const bool phaseValid =
                snapshot.completeSofTimeUs != 0 &&
                phaseUs < MAIN_LOOP_GATE_USB_FRAME_US;

	if (!main_loop_gate_have_completion_timing) {
		main_loop_gate_have_completion_timing = true;
		main_loop_gate_last_complete_time_us = snapshot.completeTimeUs;
		main_loop_gate_last_complete_sof_frame =
			snapshot.completeSofFrame;
                if (phaseValid) {
                        main_loop_gate_phase_min_us = phaseUs;
                        main_loop_gate_phase_max_us = phaseUs;
                }
                return MainLoopGateCompletionResult::Baseline;
	}

	const uint32_t intervalUs =
		snapshot.completeTimeUs - main_loop_gate_last_complete_time_us;
	const uint32_t sofFrameDelta =
		(snapshot.completeSofFrame -
		 main_loop_gate_last_complete_sof_frame) & 0x7FFu;
	const bool normal =
		(sofFrameDelta == 1u) &&
		(intervalUs < MAIN_LOOP_GATE_INTERVAL_2MS_US);

	if (intervalUs > main_loop_gate_interval_max_us) {
		main_loop_gate_interval_max_us = intervalUs;
	}
	main_loop_gate_last_complete_time_us = snapshot.completeTimeUs;
	main_loop_gate_last_complete_sof_frame =
		snapshot.completeSofFrame;

        if (!normal) {
                main_loop_gate_stable_completions = 0;
                return MainLoopGateCompletionResult::IntervalMutation;
        }
        if (!phaseValid) {
                main_loop_gate_stable_completions = 0;
                return MainLoopGateCompletionResult::PhaseMutation;
        }

        const uint32_t candidateMin =
                phaseUs < main_loop_gate_phase_min_us
                        ? phaseUs
                        : main_loop_gate_phase_min_us;
        const uint32_t candidateMax =
                phaseUs > main_loop_gate_phase_max_us
                        ? phaseUs
                        : main_loop_gate_phase_max_us;
        if (main_loop_gate_phase_min_us != 0xFFFFFFFFu &&
                candidateMax - candidateMin >
                        MAIN_LOOP_GATE_PHASE_SPREAD_LIMIT_US) {
                main_loop_gate_phase_min_us = phaseUs;
                main_loop_gate_phase_max_us = phaseUs;
                main_loop_gate_stable_completions = 0;
                return MainLoopGateCompletionResult::PhaseMutation;
        }

        main_loop_gate_phase_min_us = candidateMin;
        main_loop_gate_phase_max_us = candidateMax;
        if (main_loop_gate_stable_completions <
                MAIN_LOOP_GATE_LOCK_COMPLETIONS) {
                main_loop_gate_stable_completions++;
        }
        return MainLoopGateCompletionResult::Normal;
}

static void prepareMainLoopGateFrameSchedule(
        const USBMainGamepadGateSnapshot& snapshot) {
        main_loop_gate_frame_schedule = {};
        if (main_loop_gate_state != MainLoopGateState::LOCKED ||
                !mainLoopGateSchedulingMeasurementsReady() ||
                snapshot.completeSofTimeUs == 0 ||
                main_loop_gate_phase_min_us == 0xFFFFFFFFu) {
                return;
        }

        const uint32_t finalProcessBoundUs =
                main_loop_gate_final_process_wcet.maximum +
                MAIN_LOOP_GATE_WCET_MARGIN_US;
        const uint32_t endpointArmBoundUs =
                main_loop_gate_endpoint_arm_wcet.maximum +
                MAIN_LOOP_GATE_WCET_MARGIN_US;
        const uint32_t reservedUs =
                finalProcessBoundUs +
                endpointArmBoundUs +
                MAIN_LOOP_GATE_ENDPOINT_READY_GUARD_US;
        if (reservedUs >= MAIN_LOOP_GATE_USB_FRAME_US) {
                return;
        }

        const uint32_t tokenPhaseUs =
                main_loop_gate_phase_min_us >
                        MAIN_LOOP_GATE_COMPLETION_TO_TOKEN_GUARD_US
                        ? main_loop_gate_phase_min_us -
                                MAIN_LOOP_GATE_COMPLETION_TO_TOKEN_GUARD_US
                        : 0;
        const uint32_t nextTokenEarliestUs =
                snapshot.completeSofTimeUs +
                MAIN_LOOP_GATE_USB_FRAME_US +
                tokenPhaseUs;
        const uint32_t armDeadlineUs =
                nextTokenEarliestUs -
                MAIN_LOOP_GATE_ENDPOINT_READY_GUARD_US;

        main_loop_gate_frame_schedule.valid = true;
        main_loop_gate_frame_schedule.nextTokenEarliestUs =
                nextTokenEarliestUs;
        main_loop_gate_frame_schedule.armDeadlineUs =
                armDeadlineUs;
        main_loop_gate_frame_schedule.finalizeDeadlineUs =
                armDeadlineUs -
                finalProcessBoundUs -
                endpointArmBoundUs;
}

static void resetMainLoopGateForSnapshot(
	const USBMainGamepadGateSnapshot& snapshot) {
	main_loop_gate_epoch = snapshot.epoch;
	main_loop_gate_complete_seq = snapshot.completeSeq;
	main_loop_gate_failed_seq = snapshot.failedSeq;
	main_loop_gate_action_epoch = snapshot.epoch;
	main_loop_gate_first_in_seen = false;
	main_loop_suspend_scan_initialized = false;
	resetMainLoopGateTiming();
        resetMainLoopGateMeasurements();
	main_loop_gate_state =
		(snapshot.mounted && !snapshot.suspended)
			? MainLoopGateState::BOOTSTRAP_BUILD
			: MainLoopGateState::WAIT_MOUNT;
}

static MainLoopGateAction getMainLoopGateAction(bool configMode) {
	const bool gateEnabledNow = (!configMode && main_loop_gate_enabled);
	if (!gateEnabledNow) {
		main_loop_gate_runtime_enabled = false;
		main_loop_gate_state = MainLoopGateState::WAIT_MOUNT;
		return MainLoopGateAction::RunFrame;
	}

	USBMainGamepadGateSnapshot snapshot = {};
	usb_get_main_gamepad_gate_snapshot(&snapshot);

	if (!main_loop_gate_runtime_enabled) {
		main_loop_gate_runtime_enabled = true;
		resetMainLoopGateForSnapshot(snapshot);
	} else if (snapshot.epoch != main_loop_gate_epoch) {
		resetMainLoopGateForSnapshot(snapshot);
	}

	main_loop_gate_action_epoch = snapshot.epoch;

	if (!snapshot.mounted) {
		main_loop_gate_state = MainLoopGateState::WAIT_MOUNT;
		main_loop_suspend_scan_initialized = false;
		return MainLoopGateAction::WaitUSB;
	}

	if (snapshot.suspended) {
		if (main_loop_gate_state != MainLoopGateState::SUSPENDED_ARMED &&
			main_loop_gate_state != MainLoopGateState::SUSPENDED_UNARMED) {
			main_loop_suspend_scan_initialized = false;
		}
		main_loop_gate_state =
			snapshot.reportArmed
				? MainLoopGateState::SUSPENDED_ARMED
				: MainLoopGateState::SUSPENDED_UNARMED;
		return MainLoopGateAction::ScanSuspended;
	}

	if (main_loop_gate_state == MainLoopGateState::SUSPENDED_ARMED ||
		main_loop_gate_state == MainLoopGateState::SUSPENDED_UNARMED) {
		main_loop_gate_complete_seq = snapshot.completeSeq;
		main_loop_gate_failed_seq = snapshot.failedSeq;
		main_loop_gate_first_in_seen = false;
		main_loop_suspend_scan_initialized = false;
		resetMainLoopGateTiming();
		main_loop_gate_state =
			snapshot.reportArmed
				? MainLoopGateState::WAIT_FIRST_IN
				: MainLoopGateState::BOOTSTRAP_SUBMIT;
	}

	if (snapshot.failedSeq != main_loop_gate_failed_seq) {
		main_loop_gate_failed_seq = snapshot.failedSeq;
		main_loop_gate_first_in_seen = false;
		resetMainLoopGateTiming();
		main_loop_gate_state = MainLoopGateState::RECOVERY;
	}

	if (main_loop_gate_state == MainLoopGateState::WAIT_MOUNT) {
		main_loop_gate_state = MainLoopGateState::BOOTSTRAP_BUILD;
	}

	switch (main_loop_gate_state) {
		case MainLoopGateState::BOOTSTRAP_BUILD:
                        main_loop_gate_frame_schedule = {};
			return MainLoopGateAction::RunFrame;

		case MainLoopGateState::BOOTSTRAP_SUBMIT:
			return MainLoopGateAction::RetrySubmit;

		case MainLoopGateState::WAIT_FIRST_IN:
			if (snapshot.completeSeq != main_loop_gate_complete_seq) {
				main_loop_gate_complete_seq = snapshot.completeSeq;
				recordMainLoopGateCompletion(snapshot);
				main_loop_gate_first_in_seen = true;
				main_loop_gate_state = MainLoopGateState::LEARNING;
                                main_loop_gate_frame_schedule = {};
				return MainLoopGateAction::RunFrame;
			}
			if (!snapshot.reportArmed) {
				main_loop_gate_state = MainLoopGateState::BOOTSTRAP_SUBMIT;
				return MainLoopGateAction::RetrySubmit;
			}
			return MainLoopGateAction::WaitUSB;

		case MainLoopGateState::LEARNING:
		case MainLoopGateState::LOCKED:
			if (snapshot.completeSeq != main_loop_gate_complete_seq) {
				main_loop_gate_complete_seq = snapshot.completeSeq;
                                const MainLoopGateCompletionResult result =
					recordMainLoopGateCompletion(snapshot);
                                if (result ==
                                        MainLoopGateCompletionResult::PhaseMutation) {
                                        main_loop_gate_phase_mutation_count++;
                                        resetMainLoopGateTiming();
                                        main_loop_gate_state =
                                                MainLoopGateState::RECOVERY;
                                } else if (result ==
                                        MainLoopGateCompletionResult::IntervalMutation) {
                                        resetMainLoopGateTiming();
					main_loop_gate_state =
						MainLoopGateState::RECOVERY;
				} else if (main_loop_gate_stable_completions >=
                                                MAIN_LOOP_GATE_LOCK_COMPLETIONS &&
                                        mainLoopGateSchedulingMeasurementsReady()) {
					main_loop_gate_state =
						MainLoopGateState::LOCKED;
				} else {
					main_loop_gate_state =
						MainLoopGateState::LEARNING;
				}
                                prepareMainLoopGateFrameSchedule(snapshot);
				return MainLoopGateAction::RunFrame;
			}
			if (!snapshot.reportArmed) {
				main_loop_gate_first_in_seen = false;
				resetMainLoopGateTiming();
				main_loop_gate_state = MainLoopGateState::RECOVERY;
				return MainLoopGateAction::RetrySubmit;
			}
			return MainLoopGateAction::WaitUSB;

		case MainLoopGateState::RECOVERY:
			if (snapshot.completeSeq != main_loop_gate_complete_seq) {
				main_loop_gate_complete_seq = snapshot.completeSeq;
				recordMainLoopGateCompletion(snapshot);
				main_loop_gate_first_in_seen = true;
				main_loop_gate_state = MainLoopGateState::LEARNING;
                                main_loop_gate_frame_schedule = {};
				return MainLoopGateAction::RunFrame;
			}
			return snapshot.reportArmed
				? MainLoopGateAction::WaitUSB
				: MainLoopGateAction::RetrySubmit;

		case MainLoopGateState::SUSPENDED_ARMED:
		case MainLoopGateState::SUSPENDED_UNARMED:
		case MainLoopGateState::WAIT_MOUNT:
		default:
			return MainLoopGateAction::WaitUSB;
	}
}

static bool mainLoopGateReportAttempted(bool submitted) {
	if (!main_loop_gate_runtime_enabled) {
                return submitted;
	}

	if (submitted &&
		usb_mark_main_gamepad_report_submitted(main_loop_gate_action_epoch)) {
		if (!main_loop_gate_first_in_seen) {
			main_loop_gate_state = MainLoopGateState::WAIT_FIRST_IN;
		} else if (main_loop_gate_state != MainLoopGateState::LOCKED &&
			main_loop_gate_state != MainLoopGateState::RECOVERY) {
			main_loop_gate_state = MainLoopGateState::LEARNING;
		}
                return true;
	}

	if (!submitted) {
		usb_notify_main_gamepad_submit_failed(
			main_loop_gate_action_epoch);
	}
	main_loop_gate_first_in_seen = false;
	resetMainLoopGateTiming();

	USBMainGamepadGateSnapshot snapshot = {};
	usb_get_main_gamepad_gate_snapshot(&snapshot);
	if (snapshot.epoch != main_loop_gate_epoch ||
		!snapshot.mounted ||
		snapshot.suspended) {
		resetMainLoopGateForSnapshot(snapshot);
	} else {
		main_loop_gate_state = MainLoopGateState::RECOVERY;
	}
        return false;
}

static MainLoopGateLateSampleResult sampleMainLoopGateLateAnalog(
        AddonManager& addons) {
        MainLoopGateLateSampleResult result;
        MainLoopGateRollingMax* adcWcet =
                mainLoopGateADCWcet(main_loop_gate_analog_source);
        MainLoopGateRollingMax* burstSetupWcet =
                mainLoopGateADCBurstSetupWcet(main_loop_gate_analog_source);
        if (adcWcet == nullptr || burstSetupWcet == nullptr) {
                return result;
        }

        const bool deadlineMode =
                main_loop_gate_state == MainLoopGateState::LOCKED &&
                main_loop_gate_frame_schedule.valid;
        if (deadlineMode) {
                const uint32_t burstSetupBoundUs =
                        burstSetupWcet->maximum +
                        MAIN_LOOP_GATE_WCET_MARGIN_US;
                const uint32_t sampleBoundUs =
                        adcWcet->maximum +
                        MAIN_LOOP_GATE_WCET_MARGIN_US;
                if (mainLoopGateTimeRemaining(
                                time_us_32(),
                                main_loop_gate_frame_schedule.finalizeDeadlineUs) <
                        burstSetupBoundUs + sampleBoundUs) {
                        main_loop_gate_frame_without_fresh_sample_count++;
                        return result;
                }
        }

        const uint32_t burstSetupStartUs = time_us_32();
        const bool burstStarted = addons.BeginGateLateAnalogBurst();
        const uint32_t burstSetupEndUs = time_us_32();
        if (!burstStarted) {
                main_loop_gate_frame_without_fresh_sample_count++;
                return result;
        }
        uint32_t burstSetupDurationUs =
                burstSetupEndUs - burstSetupStartUs;
        if (burstSetupDurationUs == 0) {
                burstSetupDurationUs = 1;
        }
        burstSetupWcet->record(burstSetupDurationUs);
        result.busTouched = true;

        GateLateAnalogSampleRequest request;
        request.enforceDeadline = deadlineMode;
        request.deadlineUs =
                main_loop_gate_frame_schedule.finalizeDeadlineUs;
        while (true) {
                if (deadlineMode) {
                        const uint32_t sampleBoundUs =
                                adcWcet->maximum +
                                MAIN_LOOP_GATE_WCET_MARGIN_US;
                        const uint32_t nowUs = time_us_32();
                        if (mainLoopGateTimeRemaining(
                                        nowUs,
                                        request.deadlineUs) <
                                sampleBoundUs) {
                                result.deadlineOverrun =
                                        mainLoopGateTimeReached(
                                                nowUs,
                                                request.deadlineUs);
                                break;
                        }
                }

                const uint32_t sampleStartUs = time_us_32();
                const bool sampled =
                        addons.SampleGateLateAnalog(request);
                const uint32_t sampleEndUs = time_us_32();
                uint32_t sampleDurationUs =
                        sampleEndUs - sampleStartUs;
                if (sampleDurationUs == 0) {
                        sampleDurationUs = 1;
                }
                adcWcet->record(sampleDurationUs);

                if (!sampled) {
                        result.deadlineOverrun =
                                deadlineMode &&
                                mainLoopGateTimeReached(
                                        sampleEndUs,
                                        request.deadlineUs);
                        break;
                }

                result.sampleSets++;
                if (!deadlineMode) {
                        break;
                }
        }
        addons.EndGateLateAnalogBurst();

        main_loop_gate_late_sample_set_count += result.sampleSets;
        if (result.sampleSets > 1) {
                main_loop_gate_repeated_sample_frame_count++;
        }
        if (result.sampleSets >
                main_loop_gate_max_sample_sets_per_frame) {
                main_loop_gate_max_sample_sets_per_frame =
                        result.sampleSets;
        }
        if (result.sampleSets == 0) {
                main_loop_gate_frame_without_fresh_sample_count++;
        }
        return result;
}

static void recordMainLoopGateFrameTiming(
        AddonManager& addons,
        const MainLoopGateLateSampleResult& lateSample,
        uint32_t finalProcessStartUs,
        uint32_t endpointArmStartUs,
        uint32_t endpointArmEndUs,
        bool reportArmed) {
        uint32_t finalProcessDurationUs =
                endpointArmStartUs - finalProcessStartUs;
        if (finalProcessDurationUs == 0) {
                finalProcessDurationUs = 1;
        }
        main_loop_gate_final_process_wcet.record(
                finalProcessDurationUs);

        if (!reportArmed) {
                return;
        }

        uint32_t endpointArmDurationUs =
                endpointArmEndUs - endpointArmStartUs;
        if (endpointArmDurationUs == 0) {
                endpointArmDurationUs = 1;
        }
        main_loop_gate_endpoint_arm_wcet.record(
                endpointArmDurationUs);

        const uint32_t sampleCompletedTimeUs =
                addons.GetGateLateAnalogCompletedTimeUs();
        if (sampleCompletedTimeUs != 0) {
                main_loop_gate_sample_age_last_us =
                        endpointArmEndUs - sampleCompletedTimeUs;
                if (main_loop_gate_sample_age_last_us >
                        main_loop_gate_sample_age_max_us) {
                        main_loop_gate_sample_age_max_us =
                                main_loop_gate_sample_age_last_us;
                }
        }

        const bool missedDeadline =
                main_loop_gate_frame_schedule.valid &&
                (lateSample.deadlineOverrun ||
                 mainLoopGateTimeReached(
                         finalProcessStartUs,
                         main_loop_gate_frame_schedule.finalizeDeadlineUs) ||
                 mainLoopGateTimeReached(
                         endpointArmEndUs,
                         main_loop_gate_frame_schedule.armDeadlineUs));
        if (missedDeadline) {
                main_loop_gate_deadline_miss_count++;
                resetMainLoopGateTiming();
                main_loop_gate_state = MainLoopGateState::RECOVERY;
        }
}

static bool mainLoopGateEventPending() {
	USBMainGamepadGateSnapshot snapshot = {};
	usb_get_main_gamepad_gate_snapshot(&snapshot);
	if (snapshot.epoch != main_loop_gate_epoch) {
		return true;
	}
	if (main_loop_gate_state == MainLoopGateState::WAIT_MOUNT) {
		return snapshot.mounted;
	}
	return !snapshot.mounted ||
		snapshot.suspended ||
		snapshot.completeSeq != main_loop_gate_complete_seq ||
		snapshot.failedSeq != main_loop_gate_failed_seq ||
		!snapshot.reportArmed;
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
	gamepad->lastReinitHmlBackPreset = getHmlBackMappingActivePresetIndex(Storage::getInstance().getAddonOptions());

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
	addons.LoadAddon(new MCP3208ADCAddon());
	addons.LoadAddon(new UnifiedAnalogProcessorAddon());
	addons.LoadAddon(new UnifiedVoltageSwitchAddon());
	addons.LoadAddon(new UnifiedJoystickTravelKeyAddon());
	addons.LoadAddon(new LSM6DSRIMUAddon());
	addons.LoadAddon(new HETriggerAddon());
	addons.LoadAddon(new TwoKeyTouchpadAddon());
	addons.LoadAddon(new BackButtonDividerAddon());
	// 须在背键/触摸板/FN 电压映射之后：preprocess 内合并 ADC 与映射的 L2/R2（含 lt/rt）
	addons.LoadAddon(new LinearTriggerAddon());
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
	main_loop_gate_analog_source =
			addons.GetGateLateAnalogSource();

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
			gpio_init(pin);                    // Initialize pin
			gpio_set_dir(pin, GPIO_IN);        // Set as INPUT
			gpio_pull_up(pin);                 // Set as PULLUP
			gpio_set_input_enabled(pin, true); // Ensure digital input buffer is enabled (may be disabled by adc_gpio_init)
			buttonGpios |= 1 << pin;           // mark this pin as mattering for GPIO debouncing
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
	if (main_loop_gate_enabled) {
		tud_sof_isr_set(usb_notify_main_gamepad_sof);
	}

	// Initialize our USB manager
	USBHostManager::getInstance().start();

	if (configMode == true ) {
		rndis_init(WEB_CONFIG_HOSTNAME);
	}

	while (1) { // LOOP
		this->getReinitGamepad(gamepad);

		const MainLoopGateAction gateAction =
			getMainLoopGateAction(configMode);
		if (gateAction == MainLoopGateAction::WaitUSB) {
			USBHostManager::getInstance().process();
			tud_task();
			if (!mainLoopGateEventPending()) {
				best_effort_wfe_or_timeout(
					make_timeout_time_us(
						MAIN_LOOP_GATE_WAIT_TIMEOUT_US));
			}
			continue;
		}
		if (gateAction == MainLoopGateAction::ScanSuspended) {
			if (!main_loop_suspend_scan_initialized) {
				main_loop_suspend_last_gpio =
					gamepad->debouncedGpio & buttonGpios;
				main_loop_suspend_next_scan =
					make_timeout_time_us(
						MAIN_LOOP_GATE_SUSPEND_SCAN_US);
				main_loop_suspend_scan_initialized = true;
			} else if (time_reached(main_loop_suspend_next_scan)) {
				debounceGpioGetAll();
				const uint32_t currentGpio =
					gamepad->debouncedGpio & buttonGpios;
				if (currentGpio != main_loop_suspend_last_gpio) {
					main_loop_suspend_last_gpio = currentGpio;
					tud_remote_wakeup();
				}
				main_loop_suspend_next_scan =
					make_timeout_time_us(
						MAIN_LOOP_GATE_SUSPEND_SCAN_US);
			}

			USBHostManager::getInstance().process();
			tud_task();
			if (get_usb_suspended()) {
				best_effort_wfe_or_timeout(
					main_loop_suspend_next_scan);
			}
			continue;
		}
		if (gateAction == MainLoopGateAction::RetrySubmit) {
			USBHostManager::getInstance().process();
			const bool submitted = inputDriver->process(gamepad);
			// Arm the software epoch before tud_task can dispatch the completion.
			(void)mainLoopGateReportAttempted(submitted);
			tud_task();
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

                // Gated frames run ordinary input work once.
                const bool splitGateFrame =
                        main_loop_gate_runtime_enabled;
                if (splitGateFrame) {
                        addons.PreprocessGateEarlyAddons();
                } else {
                        addons.PreprocessAddons();
                }

		gamepad->process(); // process through MPGS

		MainLoopGateLateSampleResult lateSample;
                if (splitGateFrame) {
                        // A failed sample leaves the previous complete snapshot published.
			lateSample = sampleMainLoopGateLateAnalog(addons);
                }
		const uint32_t finalProcessStartUs =
				splitGateFrame ? time_us_32() : 0;

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
		const uint32_t endpointArmStartUs =
				splitGateFrame ? time_us_32() : 0;
		bool processed = inputDriver->process(gamepad);
		// A built frame is not a bootstrap until its main report was queued.
		const bool reportArmed =
				mainLoopGateReportAttempted(processed);
		const uint32_t endpointArmEndUs =
				splitGateFrame ? time_us_32() : 0;
		if (splitGateFrame) {
			recordMainLoopGateFrameTiming(
					addons,
					lateSample,
					finalProcessStartUs,
					endpointArmStartUs,
					endpointArmEndUs,
					reportArmed);
			if (lateSample.busTouched) {
				LSM6DSRIMUAddon::restoreGateSPIProfile();
			}
		}
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

	const uint32_t activeHmlBackPreset = getHmlBackMappingActivePresetIndex(Storage::getInstance().getAddonOptions());
	if (gamepad->lastReinitHmlBackPreset != activeHmlBackPreset) {
		addons.ReinitializeAddons();
		gamepad->lastReinitHmlBackPreset = activeHmlBackPreset;
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
