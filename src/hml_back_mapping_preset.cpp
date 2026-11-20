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
}

void initHmlBackMappingPresetScheme1FromBoardConfig(HmlBackMappingPreset& scheme1) {
    initHmlBackMappingPresetNone(scheme1);

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

HmlBackMappingPreset& getHmlBackPresetAt(AddonOptions& addonOptions, uint32_t presetIndex) {
    if (presetIndex > 2) {
        presetIndex = 2;
    }
    HmlBackMappingPresetOptions& opts = getHmlBackMappingPresetOptions(addonOptions);
    ensureHmlBackMappingPresetSlots(opts, (pb_size_t)(presetIndex + 1));
    opts.presets[presetIndex].has_backButton = true;
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
