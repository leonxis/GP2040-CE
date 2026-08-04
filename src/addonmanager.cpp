#include "addonmanager.h"
#include "usbhostmanager.h"

bool AddonManager::LoadAddon(GPAddon* addon) {
    if (addon->available()) {
        AddonBlock * block = new AddonBlock;
        addon->setup();
        block->ptr = addon;
        addons.push_back(block);
        if (addon->isGateLateAnalogProvider()) {
            gateLateAnalogProvider = addon;
        }
        return true;
    } else {
        delete addon; // Don't use the memory if we don't have to   
    }

    return false;
}

bool AddonManager::LoadUSBAddon(GPAddon* addon) {
    bool ret = LoadAddon(addon);
    if ( ret == true )
        USBHostManager::getInstance().pushListener(addon->getListener());
    return ret;
}

void AddonManager::ReinitializeAddons() {
    // Loop through all addons and process any that match our type
    for (std::vector<AddonBlock*>::iterator it = addons.begin(); it != addons.end(); it++) {
        (*it)->ptr->reinit();
    }
}

void AddonManager::PreprocessAddons() {
    // Loop through all addons and process any that match our type
    for (std::vector<AddonBlock*>::iterator it = addons.begin(); it != addons.end(); it++) {
        (*it)->ptr->preprocess();
    }
}

void AddonManager::PreprocessGateEarlyAddons() {
    for (std::vector<AddonBlock*>::iterator it = addons.begin(); it != addons.end(); it++) {
        (*it)->ptr->preprocessGateEarly();
    }
}

GateLateAnalogSource AddonManager::GetGateLateAnalogSource() const {
    return gateLateAnalogProvider != nullptr
        ? gateLateAnalogProvider->gateLateAnalogSource()
        : GateLateAnalogSource::None;
}

bool AddonManager::BeginGateLateAnalogBurst() {
    return gateLateAnalogProvider != nullptr &&
        gateLateAnalogProvider->beginGateLateAnalogBurst();
}

bool AddonManager::SampleGateLateAnalog(
    const GateLateAnalogSampleRequest& request
) {
    return gateLateAnalogProvider != nullptr &&
        gateLateAnalogProvider->sampleGateLateAnalog(request);
}

void AddonManager::EndGateLateAnalogBurst() {
    if (gateLateAnalogProvider != nullptr) {
        gateLateAnalogProvider->endGateLateAnalogBurst();
    }
}

uint32_t AddonManager::GetGateLateAnalogCompletedTimeUs() const {
    return gateLateAnalogProvider != nullptr
        ? gateLateAnalogProvider->gateLateAnalogCompletedTimeUs()
        : 0;
}

void AddonManager::ProcessAddons() {
    // Loop through all addons and process any that match our type
    for (std::vector<AddonBlock*>::iterator it = addons.begin(); it != addons.end(); it++) {
        (*it)->ptr->process();
    }
}

void AddonManager::PostprocessAddons(bool reportSent) {
    // Loop through all addons and process any that match our type
    for (std::vector<AddonBlock*>::iterator it = addons.begin(); it != addons.end(); it++) {
        (*it)->ptr->postprocess(reportSent);
    }
}

// HACK : change this for NeoPicoLED
GPAddon * AddonManager::GetAddon(std::string name) { // hack for NeoPicoLED
    for (std::vector<AddonBlock*>::iterator it = addons.begin(); it != addons.end(); it++) {
        if ( (*it)->ptr->name() == name )
            return (*it)->ptr;
    }
    return nullptr;
}
