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

void initHmlBackMappingPresetScheme1FromBoardConfig(HmlBackMappingPreset& scheme1) {
    initHmlBackMappingPresetNone(scheme1);

    FnKeyMappingOptions& fn = scheme1.fnKey;
#if defined(HML_LEFT_FN_ACTION)
    fn.leftFnMapping.action = HML_LEFT_FN_ACTION;
    fn.leftFnMapping.has_action = true;
    fn.has_leftFnMapping = true;
#endif
#if defined(HML_RIGHT_FN_ACTION)
    fn.rightFnMapping.action = HML_RIGHT_FN_ACTION;
    fn.rightFnMapping.has_action = true;
    fn.has_rightFnMapping = true;
#endif
#if defined(HML_LEFT_MT_ACTION)
    fn.leftMtMapping.action = HML_LEFT_MT_ACTION;
    fn.leftMtMapping.has_action = true;
    fn.has_leftMtMapping = true;
#endif
#if defined(HML_RIGHT_MT_ACTION)
    fn.rightMtMapping.action = HML_RIGHT_MT_ACTION;
    fn.rightMtMapping.has_action = true;
    fn.has_rightMtMapping = true;
#endif
#if defined(HML_EXT_LEFT_ACTION)
    fn.leftExtTriggerMapping.action = HML_EXT_LEFT_ACTION;
    fn.leftExtTriggerMapping.has_action = true;
    fn.has_leftExtTriggerMapping = true;
#endif
#if defined(HML_EXT_RIGHT_ACTION)
    fn.rightExtTriggerMapping.action = HML_EXT_RIGHT_ACTION;
    fn.rightExtTriggerMapping.has_action = true;
    fn.has_rightExtTriggerMapping = true;
#endif

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

    BackButtonAddonOptions& bb = scheme1.backButton;
#if defined(HML_BACK_L1_ACTION)
    bb.leftBack1Mapping.action = HML_BACK_L1_ACTION;
    bb.leftBack1Mapping.has_action = true;
    bb.has_leftBack1Mapping = true;
#endif
#if defined(HML_BACK_R1_ACTION)
    bb.rightBack1Mapping.action = HML_BACK_R1_ACTION;
    bb.rightBack1Mapping.has_action = true;
    bb.has_rightBack1Mapping = true;
#endif
#if defined(HML_BACK_L2_ACTION)
    bb.leftBack2Mapping.action = HML_BACK_L2_ACTION;
    bb.leftBack2Mapping.has_action = true;
    bb.has_leftBack2Mapping = true;
#endif
#if defined(HML_BACK_R2_ACTION)
    bb.rightBack2Mapping.action = HML_BACK_R2_ACTION;
    bb.rightBack2Mapping.has_action = true;
    bb.has_rightBack2Mapping = true;
#endif
#if defined(HML_BACK_EL_ACTION)
    bb.leftElMapping.action = HML_BACK_EL_ACTION;
    bb.leftElMapping.has_action = true;
    bb.has_leftElMapping = true;
#endif
#if defined(HML_BACK_ER_ACTION)
    bb.rightErMapping.action = HML_BACK_ER_ACTION;
    bb.rightErMapping.has_action = true;
    bb.has_rightErMapping = true;
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
    if (presetIndex > 2) {
        presetIndex = 2;
    }
    HmlBackMappingPresetOptions& opts = getHmlBackMappingPresetOptions(addonOptions);
    ensureHmlBackMappingPresetSlots(opts, (pb_size_t)(presetIndex + 1));
    opts.presets[presetIndex].has_backButton = true;
    opts.presets[presetIndex].has_fnKey = true;
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
