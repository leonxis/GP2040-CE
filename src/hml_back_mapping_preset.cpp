#include "hml_back_mapping_preset.h"

#include "BoardConfig.h"

static uint32_t clampActivePresetIndex(uint32_t activePreset, pb_size_t presetsCount) {
    if (presetsCount == 0) {
        return 0;
    }
    if (activePreset >= presetsCount) {
        return presetsCount - 1;
    }
    return activePreset;
}

static void initGpioMappingNone(GpioMappingInfo& mapping) {
    mapping.action = GpioAction::NONE;
    mapping.customButtonMask = 0;
    mapping.customDpadMask = 0;
    mapping.has_action = true;
}

void initHmlBackMappingPresetNone(HmlBackMappingPreset& preset) {
    initGpioMappingNone(preset.leftKeyMapping);
    preset.has_leftKeyMapping = true;
    initGpioMappingNone(preset.rightKeyMapping);
    preset.has_rightKeyMapping = true;
    initGpioMappingNone(preset.leftBack1Mapping);
    preset.has_leftBack1Mapping = true;
    initGpioMappingNone(preset.rightBack1Mapping);
    preset.has_rightBack1Mapping = true;
    initGpioMappingNone(preset.leftBack2Mapping);
    preset.has_leftBack2Mapping = true;
    initGpioMappingNone(preset.rightBack2Mapping);
    preset.has_rightBack2Mapping = true;
    initGpioMappingNone(preset.leftBack3Mapping);
    preset.has_leftBack3Mapping = true;
    initGpioMappingNone(preset.rightBack3Mapping);
    preset.has_rightBack3Mapping = true;
    initGpioMappingNone(preset.leftFnMapping);
    preset.has_leftFnMapping = true;
    initGpioMappingNone(preset.rightFnMapping);
    preset.has_rightFnMapping = true;
    initGpioMappingNone(preset.leftMtMapping);
    preset.has_leftMtMapping = true;
    initGpioMappingNone(preset.rightMtMapping);
    preset.has_rightMtMapping = true;
}

void initHmlBackMappingPresetScheme1FromBoardConfig(HmlBackMappingPreset& scheme1) {
    initHmlBackMappingPresetNone(scheme1);

#if defined(HML_TWOKEY_LEFT_ACTION)
    scheme1.leftKeyMapping.action = HML_TWOKEY_LEFT_ACTION;
    scheme1.leftKeyMapping.has_action = true;
    scheme1.has_leftKeyMapping = true;
#endif
#if defined(HML_TWOKEY_RIGHT_ACTION)
    scheme1.rightKeyMapping.action = HML_TWOKEY_RIGHT_ACTION;
    scheme1.rightKeyMapping.has_action = true;
    scheme1.has_rightKeyMapping = true;
#endif
}

void ensureHmlBackMappingPresetSlots(HmlBackMappingPresetOptions& opts, pb_size_t requiredCount) {
    if (requiredCount > 3) {
        requiredCount = 3;
    }
    if (requiredCount == 0) {
        return;
    }
    const pb_size_t oldCount = opts.presets_count;
    if (opts.presets_count < requiredCount) {
        opts.presets_count = requiredCount;
        for (pb_size_t i = oldCount; i < opts.presets_count; i++) {
            initHmlBackMappingPresetNone(opts.presets[i]);
        }
    }
}

HmlBackMappingPresetOptions& getHmlBackMappingPresetOptions(AddonOptions& addonOptions) {
    addonOptions.has_hmlBackMappingPresetOptions = true;
    return addonOptions.hmlBackMappingPresetOptions;
}

uint32_t getHmlBackMappingActivePresetIndex(const AddonOptions& addonOptions) {
    if (!addonOptions.has_hmlBackMappingPresetOptions) {
        return 0;
    }
    const HmlBackMappingPresetOptions& opts = addonOptions.hmlBackMappingPresetOptions;
    return clampActivePresetIndex(opts.activePreset, opts.presets_count);
}

HmlBackMappingPreset& getActiveHmlBackPreset(AddonOptions& addonOptions) {
    HmlBackMappingPresetOptions& opts = getHmlBackMappingPresetOptions(addonOptions);
    const uint32_t index = clampActivePresetIndex(opts.activePreset, opts.presets_count);
    ensureHmlBackMappingPresetSlots(opts, (pb_size_t)(index + 1));
    return opts.presets[index];
}

const HmlBackMappingPreset& getActiveHmlBackPreset(const AddonOptions& addonOptions) {
    static HmlBackMappingPreset empty = HmlBackMappingPreset_init_zero;
    if (!addonOptions.has_hmlBackMappingPresetOptions || addonOptions.hmlBackMappingPresetOptions.presets_count == 0) {
        return empty;
    }
    const HmlBackMappingPresetOptions& opts = addonOptions.hmlBackMappingPresetOptions;
    const uint32_t index = clampActivePresetIndex(opts.activePreset, opts.presets_count);
    return opts.presets[index];
}

HmlBackMappingPreset& getHmlBackPresetAt(AddonOptions& addonOptions, uint32_t presetIndex) {
    if (presetIndex > 2) {
        presetIndex = 2;
    }
    HmlBackMappingPresetOptions& opts = getHmlBackMappingPresetOptions(addonOptions);
    ensureHmlBackMappingPresetSlots(opts, (pb_size_t)(presetIndex + 1));
    return opts.presets[presetIndex];
}

const HmlBackMappingPreset& getHmlBackPresetAt(const AddonOptions& addonOptions, uint32_t presetIndex) {
    static HmlBackMappingPreset empty = HmlBackMappingPreset_init_zero;
    if (!addonOptions.has_hmlBackMappingPresetOptions) {
        return empty;
    }
    if (presetIndex > 2) {
        presetIndex = 2;
    }
    if (presetIndex >= addonOptions.hmlBackMappingPresetOptions.presets_count) {
        return empty;
    }
    return addonOptions.hmlBackMappingPresetOptions.presets[presetIndex];
}
