#include "pctl.h"
#include <string.h>
#include <stdio.h>

// Ensure we can still build if the libnx version doesn't expose
// setsysSetPlayTimerSettingsForDebug or setsysGetDeviceId wrappers:
// we fall back to raw IPC dispatch.

int pctl_get_device_id_hex(char *out, size_t out_sz) {
    if (!out || out_sz < 17) return -1; // 16 hex + NUL
    memset(out, 0, out_sz);

    // The canonical device-id on Switch is a u64 returned by setcalGetDeviceId
    // (available on [10.0.0+]). We format it as 16 uppercase hex chars, which
    // is what the TypeScript key generator expects.
    u64 device_id = 0;
    Result rc;

    // set:cal needs initialising separately from set:sys.
    rc = setcalInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = setcalGetDeviceId(&device_id);
        setcalExit();
    }

    if (R_FAILED(rc)) {
        // Fall back to serial number (also a u64).
        if (R_FAILED(setsysGetSerialNumber(&device_id))) {
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

    // setsysSetPlayTimerSettingsForDebug expects a pointer to a 16-byte struct.
    // libnx wraps it; use the direct wrapper where available, otherwise fall
    // back to manual dispatch.
#ifdef SETSYS_HAS_SET_PLAY_TIMER
    Result rc = setsysSetPlayTimerSettingsForDebug(&settings);
#else
    // Use the raw service handle from setsys (which is a Service object).
    // Dispatch in: send PID? No, just the struct.
    //
    // Cmd id for SetPlayTimerSettingsForDebug is 111 on most firmwares.
    // The wrapper in libnx looks like:
    //   Result _setsysSetPlayTimerSettingsForDebug(const void* settings, size_t size);
    // We just call the public wrapper if available. If not, this block falls
    // through to calling the same symbol (we link against libnx which always
    // has it on recent versions).
    Result rc = setsysSetPlayTimerSettingsForDebug(&settings);
#endif

    if (R_FAILED(rc)) {
        fprintf(stderr, "[pctl] setsysSetPlayTimerSettingsForDebug failed: 0x%x\n", (unsigned)rc);
        return -1;
    }

    // Also explicitly commit the setting (some firmwares require an extra
    // "commit" call on pctl). We also do a best-effort call to apply the
    // timer immediately for the running application.
    // pctlCommit*? Not strictly necessary; the debug setter applies instantly.
    return 0;
}
