#pragma once
#include "config.h"
#include <stdint.h>
#include <stdbool.h>

// 2024-01-01T00:00:00Z (same as TypeScript key generator).
#define RENTAL_EPOCH 1704067200ULL

typedef enum {
    RENTAL_OK = 0,
    RENTAL_LOCKED,       // Current period is past unlock deadline
    RENTAL_IN_GRACE,     // Inside grace period, should warn but not lock
    RENTAL_WRONG_KEY,
    RENTAL_COOLDOWN,
} rental_status_t;

// One-time initialization: load config from SD, init state, capture device id.
// Must be called after initializing libnx (setsysHandle, timeGetSystemTime etc.).
int rental_init(rental_config_t *cfg, rental_state_t *state);

// Write current state to SD. Call after a successful unlock.
int rental_save_state(const rental_state_t *state);

// Get the current period index and period start/end (unix seconds, UTC).
int rental_current_period(const rental_config_t *cfg, uint64_t now_unix,
                          int *period_out, uint64_t *start_out, uint64_t *end_out);

// Check whether the console is currently locked, in grace, or unlocked.
rental_status_t rental_check(const rental_config_t *cfg,
                             const rental_state_t *state,
                             uint64_t now_unix,
                             int *current_period_out,
                             int *unlocked_until_out);

// Derive the 8-digit decimal unlock key for a specific period.
void rental_derive_key(const rental_config_t *cfg,
                       const rental_state_t *state,
                       int period,
                       char out[KEY_LEN + 1]);

// Try to apply an entered key. Returns RENTAL_OK on success (state updated and
// saved), RENTAL_WRONG_KEY or RENTAL_COOLDOWN otherwise.
rental_status_t rental_submit_key(rental_config_t *cfg,
                                  rental_state_t *state,
                                  const char *entered);

// Enforce the lock: set daily play time to LOCKED_PLAY_MINUTES.
int rental_enforce_lock(void);

// Remove/enable play: set daily play time to UNLOCKED_PLAY_MINUTES
// (0 = unlimited in the Nintendo protocol, effectively no restriction).
int rental_enforce_unlock(void);

// Return a pointer to a short English/French/log-friendly string.
const char *rental_status_str(rental_status_t s);

// Format the "machine code" as a printable hex string (from state->device_id).
const char *rental_machine_code(const rental_state_t *state);
