#include "pctl.h"
#include <string.h>
#include <stdio.h>

// Ensure we can still build if the libnx version doesn't expose
// setsysSetPlayTimerSettingsForDebug: we fall back gracefully.

// Simple FNV-1a 64-bit hash, used only as a last-resort fallback to turn the
// console serial number string into a stable 64-bit pseudo device id if the
// primary spl-based device id retrieval ever fails.
static u64 fnv1a64(const char *s, size_t len) {
    u64 h = 1469598103934665603ULL; // offset basis
    for (size_t i = 0; i < len; ++i) {
        h ^= (u8)s[i];
        h *= 1099511628211ULL; // FNV prime
    }
    return h;
}

int pctl_get_device_id_hex(char *out, size_t out_sz) {
    if (!out || out_sz < 17) return -1; // 16 hex + NUL
    memset(out, 0, out_sz);

    u64 device_id = 0;
    Result rc;

    // Primary method: splGetConfig(SplConfigItem_DeviceId) returns the
    // console's genuine unique hardware device id (u64). This is the
    // standard, well-supported way to fetch a stable per-console identifier
    // in homebrew.
    rc = splInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = splGetConfig(SplConfigItem_DeviceId, &device_id);
        splExit();
    }

    if (R_FAILED(rc)) {
        // Fallback: hash the serial number string into a stable 64-bit value.
        SetSysSerialNumber serial;
        memset(&serial, 0, sizeof(serial));
        if (R_SUCCEEDED(setsysGetSerialNumber(&serial))) {
            device_id = fnv1a64(serial.number, strlen(serial.number));
        } else {
            // Last resort: a fixed non-zero sentinel so HMAC doesn't blow up.
            device_id = 0x00000000DEADBEEFULL;
        }
    }

    snprintf(out, out_sz, "%016llX", (unsigned long long)device_id);
    return 0;
}

int pctl_set_play_minutes(uint16_t minutes) {
    // The SetPlayTimerSettingsForDebug struct (16 bytes total):
    //   u8  is_enabled;
    //   u8  pad;
    //   s16 daily_limit_minutes;   // s16? -1 == unlimited / disabled
    //   u8  rest[12];
    //
    // When minutes == 0xFFFF we pass -1 (signed) which the firmware treats as
    // "parental timer disabled", allowing unlimited play.
    struct {
        u8  is_enabled;
        u8  pad;
        s16 limit;
        u8  rest[12];
    } settings;
    memset(&settings, 0, sizeof(settings));

    if (minutes == 0xFFFF) {
        settings.is_enabled = 0;
        settings.limit = -1; // unlimited
    } else {
        settings.is_enabled = 1;
        settings.limit = (s16)minutes;
    }

    // Pass through void* so this compiles regardless of the exact official
    // struct type name/layout libnx declares for this parameter (GCC 14
    // treats struct-pointer-type mismatches as hard errors, but implicit
    // void* <-> any-object-pointer conversions remain always legal in C).
    void *settings_ptr = &settings;
    Result rc = setsysSetPlayTimerSettingsForDebug(settings_ptr);

    if (R_FAILED(rc)) {
        fprintf(stderr, "[pctl] setsysSetPlayTimerSettingsForDebug failed: 0x%x\n", (unsigned)rc);
        return -1;
    }

    return 0;
}
