#ifndef _ACTION_MAPPING_COMMON_H_
#define _ACTION_MAPPING_COMMON_H_

#include <stdint.h>
#include "config.pb.h"

class Gamepad;

namespace ActionMappingCommon {

static constexpr uint8_t ACTION_MAPPING_TABLE_MAX_ENTRIES = 8;
static constexpr uint8_t ACTION_OUTPUT_SCOPE_MAX_COMPLEX_ACTIONS = 8;

struct ActionMappingEntry {
    uint32_t buttonMask = 0;
    uint8_t dpadMask = 0;
    uint16_t auxMask = 0;
    uint64_t keyboardMask = 0;
    uint8_t mouseButtonMask = 0;
    GpioAction complexAction = GpioAction::NONE;
    bool enabled = false;
};

struct ActionMappingTable {
    ActionMappingEntry entries[ACTION_MAPPING_TABLE_MAX_ENTRIES];
    uint8_t count = 0;

    void clear();
    void setCount(uint8_t newCount);
    ActionMappingEntry* at(uint8_t index);
    const ActionMappingEntry* at(uint8_t index) const;
};

struct DebounceBoolState {
    bool stable = false;
    bool pending = false;
    uint8_t count = 0;
};

struct DebounceLevelState {
    int8_t stable = -1;
    int8_t pending = -1;
    uint8_t count = 0;
};

void resetDebounceBool(DebounceBoolState& state, bool stableValue = false);
void updateDebounceBool(bool candidate, DebounceBoolState& state, uint8_t debounceFrames);

void resetDebounceLevel(DebounceLevelState& state, int8_t stableValue = -1);
void updateDebounceLevel(int8_t candidate, DebounceLevelState& state, uint8_t debounceFrames, bool immediateReleaseToNone = false);

void parseActionMapping(const GpioMappingInfo& mapping, ActionMappingEntry& entry);
void parseActionMapping(GpioAction action, uint32_t customButtonMask, uint32_t customDpadMask, ActionMappingEntry& entry);

void applyActionMappingEntry(Gamepad* gamepad, const ActionMappingEntry& entry);
void clearActionMappingEntry(Gamepad* gamepad, const ActionMappingEntry& entry);

class ActionOutputScope {
public:
    void reset();
    void beginFrame(Gamepad* gamepad);
    void apply(Gamepad* gamepad, const ActionMappingEntry& entry);
    void endFrame();

private:
    uint32_t lastButtons_ = 0;
    uint8_t lastDpad_ = 0;
    uint16_t lastAux_ = 0;
    uint64_t lastKeyboard_ = 0;
    uint8_t lastMouse_ = 0;
    GpioAction lastComplex_[ACTION_OUTPUT_SCOPE_MAX_COMPLEX_ACTIONS] = {};
    uint8_t lastComplexCount_ = 0;

    uint32_t frameButtons_ = 0;
    uint8_t frameDpad_ = 0;
    uint16_t frameAux_ = 0;
    uint64_t frameKeyboard_ = 0;
    uint8_t frameMouse_ = 0;
    GpioAction frameComplex_[ACTION_OUTPUT_SCOPE_MAX_COMPLEX_ACTIONS] = {};
    uint8_t frameComplexCount_ = 0;
};

} // namespace ActionMappingCommon

#endif // _ACTION_MAPPING_COMMON_H_
