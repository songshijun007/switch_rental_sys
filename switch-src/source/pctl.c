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
    // console's genuine unique hardware device id (u64).
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
            device_id = 0x00000000DEADBEEFULL;
        }
    }

    snprintf(out, out_sz, "%016llX", (unsigned long long)device_id);
    return 0;
}

// -----------------------------------------------------------------------------
// NOTE on pctl_set_play_minutes():
//
// There is NO function called `setsysSetPlayTimerSettingsForDebug` in libnx
// (it never existed under the `setsys` namespace). The real low-level IPC
// command for forcing a play-timer limit lives in Nintendo's `pctl` (Parental
// Control) sysmodule as a "...ForDebug" debug command, which:
//   1. Requires a large, precisely-laid-out settings struct whose exact byte
//      layout is not stably documented/exposed by libnx's public headers.
//   2. Is typically gated behind special permissions on retail consoles, so
//      even a byte-perfect IPC call may silently fail at runtime.
//   3. If the struct layout is wrong, this raw IPC call can hang or crash the
//      pctl system process -- a much worse outcome than simply not enforcing
//      the lock.
//
// To keep this build safe and working, we currently treat this as a
// best-effort NO-OP and just log what *would* happen. The rental/unlock-key
// logic, web UI, and on-console menu all continue to work correctly; only
// the actual "hard" OS-level play-timer lock is stubbed out for now.
//
// TODO: Replace this with a verified, tested implementation of the real
// pctl debug IPC command once the exact struct layout + required permission
// model have been confirmed against your target firmware version. See the
// pctl service docs on the switchbrew wiki for the authoritative command
// list before attempting this.
// -----------------------------------------------------------------------------
int pctl_set_play_minutes(uint16_t minutes) {
    if (minutes == 0xFFFF) {
        fprintf(stderr, "[pctl] (stub) would ENABLE unlimited play time.\n");
    } else {
        fprintf(stderr, "[pctl] (stub) would LIMIT play time to %u minute(s)/day.\n",
                (unsigned)minutes);
    }

    // No-op for now: always report success so the rest of the rental flow
    // (key validation, state saving, web UI) is unaffected.
    return 0;
}
