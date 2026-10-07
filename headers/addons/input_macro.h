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
// Macro recording flash layout (macro 1 only)
//
// Region: MACRO_REC_FLASH_OFFSET..+MACRO_REC_FLASH_SIZE (320KB, 5 x 64KB,
// defined in FlashPROM.h). Erased as one range when recording starts; only
// 256B page programs happen while recording.
//
//   offset 0x000000 : header page (256B, written last -> power-loss safe)
//   offset 0x000100 : event track (variable length, 7936B)
//   offset 0x002000 : frame track (4B/frame, 312KB = 79,872 frames)
//
// Event wire format: [frameIndex uvarint][mask uvarint32][lt u8][rt u8]
// mask packs state.buttons plus dpad low nibble shifted to bits 16..19.
// ---------------------------------------------------------------------------
#define MACRO_REC_INDEX            0u                 // Only macro 1 supports recording
#define MACRO_REC_TICK_US          4000u              // 250Hz
#define MACRO_REC_HEADER_OFFSET    0x00000000u
#define MACRO_REC_EVENT_OFFSET     0x00000100u
#define MACRO_REC_EVENT_SIZE       (0x00002000u - MACRO_REC_EVENT_OFFSET)  // 7936B
#define MACRO_REC_FRAME_OFFSET     0x00002000u
#define MACRO_REC_FRAME_SIZE       (MACRO_REC_FLASH_SIZE - MACRO_REC_FRAME_OFFSET) // 312KB
#define MACRO_REC_FRAME_CAP        (MACRO_REC_FRAME_SIZE / 4u)           // 79,872 frames
#define MACRO_REC_CENTER_DEADZONE  2000
#define MACRO_REC_VERSION          1u
#define MACRO_REC_MAX_EVENT_BYTES  10u

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

    // --- Macro recording (macro 1 only) ---
    bool isRecordedMacroEnabled() const;
    void checkRecordHotkey();
    bool validateRecordingStream();
    void startRecording();
    void stopRecording(bool overflow);
    void recordSample(uint64_t now);
    bool appendRecordEvent(uint32_t frame, uint32_t mask, uint8_t lt, uint8_t rt);
    void recordWriteBytes(const uint8_t * data, uint8_t len);
    void flushEventPage();
    void flushFramePage();
    void beginRecordedPlayback();
    void runRecordedMacro(uint64_t now);

    bool isMacroRunning;
    bool isMacroTriggerHeld;
    int macroPosition;
    Mask_t macroButtonMask;
    Mask_t macroPinMasks[6];
    uint64_t macroStartTime;
    uint64_t currentMicros;
    int pressedMacro;
    int macroInputPosition;
    uint32_t macroInputHoldTime;
    bool prevMacroInputPressed;
    MacroOptions * inputMacroOptions;

    // Recording state
    bool recActive;           // Currently capturing input
    bool recStreamValid;      // Flash stream passes magic/version check
    uint32_t recHdrFrames;    // totalFrames read from flash header
    uint32_t recHdrEvents;    // eventCount read from flash header
    uint64_t recStartUs;
    uint32_t recFramesWritten;
    uint32_t recEventCount;
    uint32_t recEvtBytePos;   // Bytes used inside the event track
    uint16_t recEvtPagePos;
    uint16_t recFrmPagePos;
    uint8_t recEvtPage[256];
    uint8_t recFrmPage[256];
    uint32_t recLastMask;
    uint8_t recLastLt;
    uint8_t recLastRt;
    bool recHasLastEvent;
    bool recHotkeyPrev;       // Rising edge for the record hotkey

    // Recorded-stream playback
    bool recPlayActive;
    const uint8_t * recPlayEvtPtr;
    uint32_t recPlayBytesLeft;
    uint32_t recPlayEventsRead;
    uint32_t recPlayMask;
    uint8_t recPlayLt;
    uint8_t recPlayRt;
};

#endif  // _InputMacro_H_
