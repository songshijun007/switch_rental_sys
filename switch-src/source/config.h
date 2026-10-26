#pragma once

// -----------------------------------------------------------------------------
// Build-time defaults for the rental lock system.
// These can be overridden at runtime by placing a rental.ini on the SD card.
// -----------------------------------------------------------------------------

// Default salt: CHANGE THIS BEFORE BUILDING FOR YOUR OWN RENTAL BUSINESS.
// Anyone who knows this salt can generate unlock keys for your machines.
#ifndef DEFAULT_SALT
#define DEFAULT_SALT "CHANGE-MY-RENTAL-SALT-PLEASE-2025"
#endif

// Default rental period length in days. Can be overridden via INI.
#ifndef DEFAULT_PERIOD_DAYS
#define DEFAULT_PERIOD_DAYS 7
#endif

// How many minutes of play time are allowed when the console is UNLOCKED.
// Set to 0xFFFF (UINT16 max) to represent "unlimited" (play timer disabled).
#define UNLOCKED_PLAY_MINUTES 0

// How many minutes of play time are allowed when the console is LOCKED.
// 0 = the Switch will not launch any games.
#define LOCKED_PLAY_MINUTES 0

// Grace period (hours) after period end before HARD locking. During grace
// period the screen shows the warning but play time remains unlimited. This
// gives renters time to pay / contact the owner.
#define GRACE_HOURS 12

// How many wrong key attempts before a cooldown (seconds).
#define WRONG_ATTEMPT_LIMIT 5
#define COOLDOWN_SECONDS 60

// Port for the web UI.
#define HTTP_PORT 8081

// Max characters the user can type for the unlock key on the console.
#define KEY_LEN 8

// SD card paths (absolute, using the sdmc: device).
#define RENTAL_DIR        "sdmc:/switch/pctltcp"
#define RENTAL_CONFIG     RENTAL_DIR "/rental.ini"
#define RENTAL_STATE      RENTAL_DIR "/rental_state.bin"

// Snapshot of the config / state read from SD.
typedef struct {
    char salt[128];
    int  period_days;
    int  grace_hours;
} rental_config_t;

typedef struct {
    // Highest period index the renter has paid for.
    int  unlocked_until_period;
    // Device id captured at first run (16 uppercase hex chars + NUL = 17 bytes).
    char device_id[17];
    // Whether first-run init has been done.
    int  initialized;
    // Wrong-attempt bookkeeping.
    int  wrong_attempts;
    uint64_t cooldown_until;
} rental_state_t;
