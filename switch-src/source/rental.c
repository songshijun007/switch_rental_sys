#include "rental.h"
#include "sha256.h"
#include "pctl.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#include <switch.h>

static void make_dirs(void) {
    mkdir(RENTAL_DIR, 0777);
}

static void load_config_defaults(rental_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->salt, DEFAULT_SALT, sizeof(cfg->salt) - 1);
    cfg->period_days = DEFAULT_PERIOD_DAYS;
    cfg->grace_hours = GRACE_HOURS;
}

// Minimal INI parser: looks for "key = value" lines.
static void load_ini(rental_config_t *cfg) {
    FILE *f = fopen(RENTAL_CONFIG, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r' || *p == 0) continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = p;
        char *val = eq + 1;
        // trim
        char *ke = eq - 1;
        while (ke > key && (*ke == ' ' || *ke == '\t')) *ke-- = 0;
        while (*val == ' ' || *val == '\t') val++;
        char *ve = val + strlen(val) - 1;
        while (ve > val && (*ve == ' ' || *ve == '\t' || *ve == '\n' || *ve == '\r')) *ve-- = 0;
        if (strcmp(key, "salt") == 0) {
            strncpy(cfg->salt, val, sizeof(cfg->salt) - 1);
            cfg->salt[sizeof(cfg->salt)-1] = 0;
        } else if (strcmp(key, "period_days") == 0) {
            int d = atoi(val);
            if (d >= 1 && d <= 365) cfg->period_days = d;
        } else if (strcmp(key, "grace_hours") == 0) {
            int g = atoi(val);
            if (g >= 0 && g <= 24*30) cfg->grace_hours = g;
        }
    }
    fclose(f);
}

static void fetch_device_id(rental_state_t *state) {
    // pctl_get_device_id_hex() returns the canonical u64 DeviceId (from set:cal
    // with a setsys serial-number fallback) formatted as 16 uppercase hex chars
    // into the provided buffer.
    if (pctl_get_device_id_hex(state->device_id, sizeof(state->device_id)) != 0) {
        snprintf(state->device_id, sizeof(state->device_id), "%016llX",
                 (unsigned long long)0xDEADBEEFULL);
    }
    state->device_id[sizeof(state->device_id) - 1] = 0;
}

int rental_init(rental_config_t *cfg, rental_state_t *state) {
    load_config_defaults(cfg);
    make_dirs();
    load_ini(cfg);

    memset(state, 0, sizeof(*state));
    FILE *f = fopen(RENTAL_STATE, "rb");
    if (f) {
        size_t n = fread(state, 1, sizeof(*state), f);
        fclose(f);
        if (n != sizeof(*state)) {
            // Corrupt state: reset to locked-first-run.
            memset(state, 0, sizeof(*state));
        }
    }

    if (!state->initialized || state->device_id[0] == 0) {
        fetch_device_id(state);
        state->unlocked_until_period = -1; // not paid for any period yet
        state->wrong_attempts = 0;
        state->cooldown_until = 0;
        state->initialized = 1;
        rental_save_state(state);
    }
    return 0;
}

int rental_save_state(const rental_state_t *state) {
    make_dirs();
    FILE *f = fopen(RENTAL_STATE, "wb");
    if (!f) return -1;
    size_t n = fwrite(state, 1, sizeof(*state), f);
    fclose(f);
    return (n == sizeof(*state)) ? 0 : -1;
}

int rental_current_period(const rental_config_t *cfg, uint64_t now_unix,
                          int *period_out, uint64_t *start_out, uint64_t *end_out) {
    uint64_t period_len = (uint64_t)cfg->period_days * 86400ULL;
    if (now_unix < RENTAL_EPOCH) now_unix = RENTAL_EPOCH;
    int p = (int)((now_unix - RENTAL_EPOCH) / period_len);
    uint64_t s = RENTAL_EPOCH + (uint64_t)p * period_len;
    uint64_t e = s + period_len - 1ULL;
    if (period_out) *period_out = p;
    if (start_out) *start_out = s;
    if (end_out)   *end_out   = e;
    return p;
}

static void derive_raw(const rental_config_t *cfg, const char *machine_id,
                       int period, uint8_t out[32]) {
    char msg[96];
    int n = snprintf(msg, sizeof(msg), "%s:%d", machine_id, period);
    hmac_sha256(cfg->salt, strlen(cfg->salt), msg, (size_t)n, out);
}

void rental_derive_key(const rental_config_t *cfg, const rental_state_t *state,
                       int period, char out[KEY_LEN + 1]) {
    uint8_t mac[32];
    derive_raw(cfg, state->device_id, period, mac);
    // Take first 4 bytes (32 bits) and convert to 8-digit decimal mod 1e8.
    uint32_t v = ((uint32_t)mac[0] << 24) | ((uint32_t)mac[1] << 16) |
                 ((uint32_t)mac[2] << 8)  | ((uint32_t)mac[3]);
    uint32_t code = v % 100000000U;
    snprintf(out, KEY_LEN + 1, "%08u", code);
}

rental_status_t rental_check(const rental_config_t *cfg,
                             const rental_state_t *state,
                             uint64_t now_unix,
                             int *current_period_out,
                             int *unlocked_until_out) {
    int cur;
    rental_current_period(cfg, now_unix, &cur, NULL, NULL);
    if (current_period_out)  *current_period_out  = cur;
    if (unlocked_until_out)  *unlocked_until_out  = state->unlocked_until_period;

    if (state->unlocked_until_period < 0) {
        return RENTAL_LOCKED;
    }
    if (cur <= state->unlocked_until_period) {
        return RENTAL_OK;
    }
    // Check grace.
    int grace = cfg->grace_hours;
    if (grace > 0) {
        uint64_t period_len = (uint64_t)cfg->period_days * 86400ULL;
        uint64_t end_of_paid = RENTAL_EPOCH +
            (uint64_t)(state->unlocked_until_period + 1) * period_len - 1ULL;
        uint64_t grace_end = end_of_paid + (uint64_t)grace * 3600ULL;
        if (now_unix <= grace_end) return RENTAL_IN_GRACE;
    }
    return RENTAL_LOCKED;
}

rental_status_t rental_submit_key(rental_config_t *cfg,
                                  rental_state_t *state,
                                  const char *entered) {
    uint64_t now = (uint64_t)time(NULL);
    if (state->cooldown_until && now < state->cooldown_until) {
        return RENTAL_COOLDOWN;
    }

    char cleaned[KEY_LEN + 2];
    int ci = 0;
    for (const char *p = entered; *p && ci < KEY_LEN; ++p) {
        if (*p >= '0' && *p <= '9') cleaned[ci++] = *p;
    }
    cleaned[ci] = 0;
    if (ci != KEY_LEN) {
        return RENTAL_WRONG_KEY;
    }

    int cur;
    rental_current_period(cfg, now, &cur, NULL, NULL);
    // Accept keys for the current period up to 208 periods in the future
    // (~4 years if weekly).
    int best = -1;
    char expected[KEY_LEN + 1];
    for (int i = 0; i < 208; ++i) {
        int p = cur + i;
        rental_derive_key(cfg, state, p, expected);
        if (strcmp(expected, cleaned) == 0) {
            best = p;
            break;
        }
    }

    if (best < 0) {
        state->wrong_attempts++;
        if (state->wrong_attempts >= WRONG_ATTEMPT_LIMIT) {
            state->cooldown_until = now + COOLDOWN_SECONDS;
            state->wrong_attempts = 0;
            rental_save_state(state);
            return RENTAL_COOLDOWN;
        }
        rental_save_state(state);
        return RENTAL_WRONG_KEY;
    }

    if (best > state->unlocked_until_period) {
        state->unlocked_until_period = best;
    }
    state->wrong_attempts = 0;
    state->cooldown_until = 0;
    rental_save_state(state);
    return RENTAL_OK;
}

int rental_enforce_lock(void) {
    return pctl_set_play_minutes(LOCKED_PLAY_MINUTES);
}

int rental_enforce_unlock(void) {
    return pctl_set_play_minutes(UNLOCKED_PLAY_MINUTES);
}

const char *rental_status_str(rental_status_t s) {
    switch (s) {
        case RENTAL_OK: return "OK (unlocked)";
        case RENTAL_IN_GRACE: return "IN GRACE PERIOD";
        case RENTAL_LOCKED: return "LOCKED";
        case RENTAL_WRONG_KEY: return "WRONG KEY";
        case RENTAL_COOLDOWN: return "COOLDOWN";
    }
    return "?";
}

const char *rental_machine_code(const rental_state_t *state) {
    return state->device_id;
}
