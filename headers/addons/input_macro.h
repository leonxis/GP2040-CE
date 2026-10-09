#ifndef _InputMacro_H
#define _InputMacro_H

#include "gpaddon.h"

#include "GamepadEnums.h"
#include "FlashPROM.h"

#ifndef INPUT_MACRO_ENABLED
#define INPUT_MACRO_ENABLED 0
#endif

#ifndef INPUT_MACRO_PIN
#define INPUT_MACRO_PIN -1
#endif

#define MAX_MACRO_INPUT_LIMIT 30
#define MAX_MACRO_LIMIT 6
#define INPUT_HOLD_US 16666

// ---------------------------------------------------------------------------
// Macro recording flash layout (two slots: macro 1 index 0, macro 2 index 1)
//
// Region: MACRO_REC_FLASH_OFFSET..+MACRO_REC_FLASH_SIZE (384KB, 6 x 64KB
// blocks, defined in FlashPROM.h). Each slot occupies MACRO_REC_SLOT_SIZE
// (192KB, 3 x 64KB blocks) and is erased as one range when recording starts;
// only 256B page programs happen while recording.
//
// Per slot:
//   offset 0x000000 : header page (256B, written last -> power-loss safe)
//   offset 0x000100 : event track (variable length, 7936B)
//   offset 0x002000 : frame track (4B/frame, 188,416B = 47,104 frames)
//
// Event wire format: [frameIndex uvarint][mask uvarint32][lt u8][rt u8]
// mask packs state.buttons plus dpad low nibble shifted to bits 16..19.
// ---------------------------------------------------------------------------
#define MACRO_REC_SLOT_COUNT       2u
#define MACRO_REC_SLOT_SIZE        0x00030000u        // 192KB per slot
#define MACRO_REC_TICK_US          4000u              // 250Hz
#define MACRO_REC_HEADER_OFFSET    0x00000000u
#define MACRO_REC_EVENT_OFFSET     0x00000100u
#define MACRO_REC_EVENT_SIZE       (0x00002000u - MACRO_REC_EVENT_OFFSET)  // 7936B
#define MACRO_REC_FRAME_OFFSET     0x00002000u
#define MACRO_REC_FRAME_SIZE       (MACRO_REC_SLOT_SIZE - MACRO_REC_FRAME_OFFSET) // 188,416B
#define MACRO_REC_FRAME_CAP        (MACRO_REC_FRAME_SIZE / 4u)           // 47,104 frames
#define MACRO_REC_CENTER_DEADZONE  2000
#define MACRO_REC_VERSION          1u
#define MACRO_REC_MAX_EVENT_BYTES  10u

static inline bool isRecordedMacroIndex(int i) { return i >= 0 && i < (int)MACRO_REC_SLOT_COUNT; }

#pragma pack(push, 1)
struct MacroRecHeader
{
    char magic[4];          // "GR01"
    uint8_t version;        // MACRO_REC_VERSION
    uint8_t reserved[3];
    uint32_t totalFrames;   // little-endian
    uint32_t eventCount;
};
#pragma pack(pop)

// Per-slot recording + playback state.
struct MacroRecSlot
{
    // Recording state
    bool active;            // Currently capturing input
    bool streamValid;       // Flash stream passes magic/version check
    uint32_t hdrFrames;     // totalFrames read from flash header
    uint32_t hdrEvents;     // eventCount read from flash header
    uint64_t startUs;
    uint32_t framesWritten;
    uint32_t eventCount;
    uint32_t evtBytePos;    // Bytes used inside the event track
    uint16_t evtPagePos;
    uint16_t frmPagePos;
    uint8_t evtPage[256];
    uint8_t frmPage[256];
    uint32_t lastMask;
    uint8_t lastLt;
    uint8_t lastRt;
    bool hasLastEvent;
    bool hotkeyPrev;        // Rising edge for this slot's record hotkey

    // Recorded-stream playback
    bool playActive;
    const uint8_t * playEvtPtr;
    uint32_t playBytesLeft;
    uint32_t playEventsRead;
    uint32_t playMask;
    uint8_t playLt;
    uint8_t playRt;
};

static inline uint8_t macroRecEncodeUVarint(uint32_t value, uint8_t * out)
{
    uint8_t n = 0;
    while (value >= 0x80) {
        out[n++] = static_cast<uint8_t>(value | 0x80);
        value >>= 7;
    }
    out[n++] = static_cast<uint8_t>(value);
    return n;
}

// Returns bytes consumed (0 on truncated buffer).
static inline uint8_t macroRecDecodeUVarint(const uint8_t * in, uint32_t avail, uint32_t & value)
{
    value = 0;
    uint8_t shift = 0;
    for (uint8_t i = 0; i < 5 && i < avail; ++i) {
        value |= static_cast<uint32_t>(in[i] & 0x7F) << shift;
        if (!(in[i] & 0x80))
            return i + 1;
        shift += 7;
    }
    return 0;
}

// 16bit axis -> 8bit. Center snap first, then truncate so center (32767 -> 127)
// and extremes (0, 65535) map exactly.
static inline uint8_t macroRecQuantAxis(uint16_t v)
{
    int32_t d = static_cast<int32_t>(v) - GAMEPAD_JOYSTICK_MID;
    if (d < 0) d = -d;
    if (d <= MACRO_REC_CENTER_DEADZONE)
        v = GAMEPAD_JOYSTICK_MID;
    return static_cast<uint8_t>(v >> 8);
}

static inline uint16_t macroRecRestoreAxis(uint8_t q)
{
    uint32_t v = static_cast<uint32_t>(q) * 257u + 128u;
    return static_cast<uint16_t>(v > 65535u ? 65535u : v);
}

// InputMacro Module Name
#define InputMacroName "Input Macro"

class InputMacro : public GPAddon {
public:
    virtual bool available();   // GPAddon available
    virtual void setup();       // Analog Setup
    virtual void process();     // Analog Process - reapply stick values after other addons
    virtual void preprocess();
    virtual void postprocess(bool sent);
    virtual void reinit();
    virtual std::string name() { return InputMacroName; }
    bool isRunning() const { return isMacroRunning; }
    bool hasStickDirection() const;
private:
    void checkMacroPress();
    void checkMacroAction();
    void runCurrentMacro();
    void reset();
    void restart(Macro& macro);

    // --- Macro recording (macro 1 / macro 2, one slot each) ---
    bool isRecordedMacroEnabled(uint8_t slot) const;
    void checkRecordHotkey();
    bool validateRecordingStream(uint8_t slot);
    void startRecording(uint8_t slot);
    void stopRecording(uint8_t slot, bool overflow);
    void recordSample(uint8_t slot, uint64_t now);
    bool appendRecordEvent(uint8_t slot, uint32_t frame, uint32_t mask, uint8_t lt, uint8_t rt);
    void recordWriteBytes(uint8_t slot, const uint8_t * data, uint8_t len);
    void flushEventPage(uint8_t slot);
    void flushFramePage(uint8_t slot);
    void beginRecordedPlayback(uint8_t slot);
    void runRecordedMacro(uint8_t slot, uint64_t now);
    void restartRecorded(uint8_t slot);

    bool isMacroRunning = false;
    bool isMacroTriggerHeld = false;
    int macroPosition = -1;
    Mask_t macroButtonMask;
    Mask_t macroPinMasks[6];
    uint64_t macroStartTime;
    uint64_t currentMicros;
    int pressedMacro = -1;
    int macroInputPosition;
    uint32_t macroInputHoldTime;
    bool prevMacroInputPressed;
    MacroOptions * inputMacroOptions;

    MacroRecSlot recs[MACRO_REC_SLOT_COUNT];
};

#endif  // _InputMacro_H
