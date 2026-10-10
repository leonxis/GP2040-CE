/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
 */

#include "storagemanager.h"

#include "BoardConfig.h"
#include "animationstorage.h"
#include "FlashPROM.h"
#include "drivermanager.h"
#include "eventmanager.h"
#include "peripheralmanager.h"
#include "config.pb.h"
#include "hardware/watchdog.h"
#include "CRC32.h"
#include "types.h"

// Check for saves
#include "ps4/PS4Driver.h"

#include "config_utils.h"

void Storage::init() {
	systemFlashSize = System::getPhysicalFlash(); // System Flash Size must be called once
	EEPROM.start();
	ConfigUtils::load(config);
}

/**
 * @brief Save the config, but only if it is safe to (as in USB host is not being used.)
 */
bool Storage::save()
{
	return save(false);
}

/**
 * @brief Save the config; if forcing a save is requested, or if USB host is not enabled, this will write to flash.
 */
bool Storage::save(const bool force) {
	// Conditions for saving:
	//   1. Force = True
	//   2. Input Mode NOT (PS4/PS5 with USB enabled)
	// Save will disconnect USB host, which is okay for gamepad and keyboard hosts
	if (!force &&
		PeripheralManager::getInstance().isUSBEnabled(0) &&
		(DriverManager::getInstance().getInputMode() == INPUT_MODE_PS4 ||
			DriverManager::getInstance().getInputMode() == INPUT_MODE_PS5) &&
		((PS4Driver*)DriverManager::getInstance().getDriver())->getDongleAuthRequired() == true ) {
		return false;
	}

	return ConfigUtils::save(config);
}

void Storage::ResetSettings()
{
	EEPROM.reset();
	watchdog_reboot(0, SRAM_END, 2000);
}

bool Storage::setProfile(const uint32_t profileNum)
{
	// 固定两个基础映射；基础映射2 恒可设（其内容可为全空）
	if (profileNum == 1 || profileNum == 2) {
		// Update the profile number - reinit will be triggered automatically in gp2040.cpp
		this->config.gamepadOptions.profileNumber = profileNum;
		return true;
	}
	// anything else is not a valid base profile
	return false;
}

void Storage::nextProfile()
{
	// 1↔2 循环
	this->config.gamepadOptions.profileNumber =
			(this->config.gamepadOptions.profileNumber == 1) ? 2 : 1;
}
void Storage::previousProfile()
{
	nextProfile();
}

const GpioMappings& Storage::getLayerPinMappings() const
{
	// 基础映射1 → sets[1]（映射层1）；基础映射2 → sets[2]（映射层2）
	const uint32_t idx = (config.gamepadOptions.profileNumber == 2) ? 2 : 1;
	return config.profileOptions.gpioMappingsSets[idx];
}

/**
 * @brief Return the current profile label.
 */
char* Storage::currentProfileLabel() {
	if (this->config.gamepadOptions.profileNumber == 1)
		return this->config.gpioMappings.profileLabel;
	else
		return this->config.profileOptions.gpioMappingsSets[config.gamepadOptions.profileNumber-2].profileLabel;
}

void Storage::setFunctionalPinMappings()
{
	// 基础映射2 → sets[0]；基础映射1 → 核心映射
	GpioMappingInfo* alts = nullptr;
	if (config.gamepadOptions.profileNumber == 2) {
		alts = config.profileOptions.gpioMappingsSets[0].pins;
	}

	for (Pin_t pin = 0; pin < (Pin_t)NUM_BANK0_GPIOS; pin++) {
		// assign the functional pin to the profile pin if:
		// 1: there was a profile to load
		// 2: the new action isn't RESERVED or ASSIGNED_TO_ADDON (profiles can't affect special addons)
		// 3: the old action isn't RESERVED or ASSIGNED_TO_ADDON (profiles can't affect special addons)
		// else use whatever is in the core mapping
		if (alts != nullptr &&
				alts[pin].action != GpioAction::RESERVED &&
				alts[pin].action != GpioAction::ASSIGNED_TO_ADDON &&
				this->config.gpioMappings.pins[pin].action != GpioAction::RESERVED &&
				this->config.gpioMappings.pins[pin].action != GpioAction::ASSIGNED_TO_ADDON) {
			functionalPinMappings[pin] = alts[pin];
		} else {
			functionalPinMappings[pin] = this->config.gpioMappings.pins[pin];
		}
	}
}

void Storage::SetGamepad(Gamepad * newpad)
{
	gamepad = newpad;
}

Gamepad * Storage::GetGamepad()
{
	return gamepad;
}

void Storage::SetProcessedGamepad(Gamepad * newpad)
{
	processedGamepad = newpad;
}

Gamepad * Storage::GetProcessedGamepad()
{
	return processedGamepad;
}

void Storage::prepareAmbientWebConfigOverride()
{
	ambientWebConfigOverrideActive.store(true, std::memory_order_release);
}

void Storage::dismissAmbientWebConfigOverride()
{
	ambientWebConfigOverrideActive.store(false, std::memory_order_release);
}

bool Storage::isAmbientWebConfigOverrideActive() const
{
	return ambientWebConfigOverrideActive.load(std::memory_order_acquire);
}

void Storage::setNrf24LinkUp(bool up)
{
	nrf24LinkUp.store(up, std::memory_order_release);
}

bool Storage::isNrf24LinkUp() const
{
	return nrf24LinkUp.load(std::memory_order_acquire);
}

void Storage::setUartLinkStatus(bool online, bool bleConnected)
{
	uartStatusOnline.store(online, std::memory_order_release);
	uartBleConnected.store(bleConnected, std::memory_order_release);
}

bool Storage::isUartStatusOnline() const
{
	return uartStatusOnline.load(std::memory_order_acquire);
}

bool Storage::isUartBleConnected() const
{
	return uartBleConnected.load(std::memory_order_acquire);
}

void Storage::pulseMacroHint()
{
	macroHintPulseSeqValue.fetch_add(1, std::memory_order_acq_rel);
}

uint32_t Storage::macroHintPulseSeq() const
{
	return macroHintPulseSeqValue.load(std::memory_order_acquire);
}

void Storage::pulseMacroRecFull()
{
	macroRecPulseSeqValue.fetch_add(1, std::memory_order_acq_rel);
}

uint32_t Storage::macroRecPulseSeq() const
{
	return macroRecPulseSeqValue.load(std::memory_order_acquire);
}

void Storage::pulseTurboToggle()
{
	turboPulseSeqValue.fetch_add(1, std::memory_order_acq_rel);
}

uint32_t Storage::turboPulseSeq() const
{
	return turboPulseSeqValue.load(std::memory_order_acquire);
}
