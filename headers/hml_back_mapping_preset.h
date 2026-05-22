#ifndef HML_BACK_MAPPING_PRESET_H_
#define HML_BACK_MAPPING_PRESET_H_

#include "config.pb.h"

HmlBackMappingPresetOptions& getHmlBackMappingPresetOptions(AddonOptions& addonOptions);

uint32_t getHmlBackMappingActivePresetIndex(const AddonOptions& addonOptions);
HmlBackMappingPreset& getActiveHmlBackPreset(AddonOptions& addonOptions);
const HmlBackMappingPreset& getActiveHmlBackPreset(const AddonOptions& addonOptions);

const BackButtonAddonOptions& getActiveBackButtonOptions(const AddonOptions& addonOptions);
const FnKeyMappingOptions& getActiveFnKeyMappingOptions(const AddonOptions& addonOptions);

HmlBackMappingPreset& getHmlBackPresetAt(AddonOptions& addonOptions, uint32_t presetIndex);
const HmlBackMappingPreset& getHmlBackPresetAt(const AddonOptions& addonOptions, uint32_t presetIndex);

void initHmlBackMappingPresetNone(HmlBackMappingPreset& preset);
void initHmlBackMappingPresetScheme1FromBoardConfig(HmlBackMappingPreset& preset);
void ensureHmlBackMappingPresetSlots(HmlBackMappingPresetOptions& opts, pb_size_t requiredCount);

#endif
