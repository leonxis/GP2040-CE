#include "hml_back_mapping_preset.h"

static uint32_t clampActivePresetIndex(uint32_t activePreset, pb_size_t presetsCount) {
    if (presetsCount == 0) {
        return 0;
    }
    if (activePreset >= presetsCount) {
        return presetsCount - 1;
    }
    return activePreset;
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
    if (opts.presets_count <= index) {
        opts.presets_count = index + 1;
    }
    opts.presets[index].has_backButton = true;
    opts.presets[index].has_fnKey = true;
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

const BackButtonAddonOptions& getActiveBackButtonOptions(const AddonOptions& addonOptions) {
    return getActiveHmlBackPreset(addonOptions).backButton;
}

const FnKeyMappingOptions& getActiveFnKeyMappingOptions(const AddonOptions& addonOptions) {
    return getActiveHmlBackPreset(addonOptions).fnKey;
}

HmlBackMappingPreset& getHmlBackPresetAt(AddonOptions& addonOptions, uint32_t presetIndex) {
    HmlBackMappingPresetOptions& opts = getHmlBackMappingPresetOptions(addonOptions);
    if (presetIndex > 2) {
        presetIndex = 2;
    }
    if (opts.presets_count <= presetIndex) {
        opts.presets_count = presetIndex + 1;
    }
    opts.presets[presetIndex].has_backButton = true;
    opts.presets[presetIndex].has_fnKey = true;
    return opts.presets[presetIndex];
}

const HmlBackMappingPreset& getHmlBackPresetAt(const AddonOptions& addonOptions, uint32_t presetIndex) {
    static HmlBackMappingPreset empty = HmlBackMappingPreset_init_zero;
    if (!addonOptions.has_hmlBackMappingPresetOptions || presetIndex >= addonOptions.hmlBackMappingPresetOptions.presets_count) {
        return empty;
    }
    if (presetIndex > 2) {
        presetIndex = 2;
    }
    return addonOptions.hmlBackMappingPresetOptions.presets[presetIndex];
}

static void initGpioMappingNone(GpioMappingInfo& mapping) {
    mapping.action = GpioAction::NONE;
    mapping.customButtonMask = 0;
    mapping.customDpadMask = 0;
    mapping.has_action = true;
}

void initHmlBackMappingPresetNone(HmlBackMappingPreset& preset) {
    preset.has_backButton = true;
    BackButtonAddonOptions& bb = preset.backButton;
    initGpioMappingNone(bb.leftBack1Mapping);
    bb.has_leftBack1Mapping = true;
    initGpioMappingNone(bb.rightBack1Mapping);
    bb.has_rightBack1Mapping = true;
    initGpioMappingNone(bb.leftBack2Mapping);
    bb.has_leftBack2Mapping = true;
    initGpioMappingNone(bb.rightBack2Mapping);
    bb.has_rightBack2Mapping = true;
    initGpioMappingNone(bb.leftElMapping);
    bb.has_leftElMapping = true;
    initGpioMappingNone(bb.rightErMapping);
    bb.has_rightErMapping = true;

    preset.has_fnKey = true;
    FnKeyMappingOptions& fn = preset.fnKey;
    initGpioMappingNone(fn.leftFnMapping);
    fn.has_leftFnMapping = true;
    initGpioMappingNone(fn.rightFnMapping);
    fn.has_rightFnMapping = true;
    initGpioMappingNone(fn.leftMtMapping);
    fn.has_leftMtMapping = true;
    initGpioMappingNone(fn.rightMtMapping);
    fn.has_rightMtMapping = true;
    initGpioMappingNone(fn.leftExtTriggerMapping);
    fn.has_leftExtTriggerMapping = true;
    initGpioMappingNone(fn.rightExtTriggerMapping);
    fn.has_rightExtTriggerMapping = true;

    initGpioMappingNone(preset.leftKeyMapping);
    preset.has_leftKeyMapping = true;
    initGpioMappingNone(preset.rightKeyMapping);
    preset.has_rightKeyMapping = true;
}
