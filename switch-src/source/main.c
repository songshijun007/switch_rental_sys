// PCTL All-in-One Rental Edition
// Original: parental control timer with console UI + web UI.
// Extended: periodic rental-unlock key system (offline HMAC validation).
//
// Build with devkitA64/libnx:
//   make clean && make

#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "rental.h"
#include "miniserv.h"

static rental_config_t g_cfg;
static rental_state_t  g_state;
static miniserv_t      g_srv;
static PadState        g_pad;
static int             g_forced_lock = 0; // 1 if currently enforcing lock

static void apply_status(rental_status_t s) {
    if (s == RENTAL_OK) {
        rental_enforce_unlock();
        g_forced_lock = 0;
    } else {
        rental_enforce_lock();
        g_forced_lock = 1;
    }
}

static void clear_screen(void) {
    printf("\x1b[2J\x1b[H");
}

static void wait_any_key(void) {
    printf("\nPress A to continue...\n");
    consoleUpdate(NULL);
    while (appletMainLoop()) {
        padUpdate(&g_pad);
        u64 k = padGetButtonsDown(&g_pad);
        if (k & HidNpadButton_A) break;
        if (k & HidNpadButton_B) break;
        miniserv_pump(&g_srv);
        svcSleepThread(16 * 1000 * 1000ULL);
    }
}

// Prompt the user for an 8-digit key using the system software keyboard.
// Returns 1 if a key was entered (output written to out, NUL terminated).
static int prompt_key(char *out, size_t out_sz) {
    SwkbdConfig swkbd;
    static char tmp[16];
    swkbdCreate(&swkbd, 0);
    swkbdConfigMakePresetDefault(&swkbd);
    swkbdConfigSetType(&swkbd, SwkbdType_NumPad);
    swkbdConfigSetHeaderText(&swkbd, "Enter 8-digit unlock key");

    char subtext[96];
    snprintf(subtext, sizeof(subtext), "Machine code: %s", rental_machine_code(&g_state));
    swkbdConfigSetSubText(&swkbd, subtext);

    swkbdConfigSetStringLenMax(&swkbd, 8);
    swkbdConfigSetStringLenMin(&swkbd, 8);

    Result rc = swkbdShow(&swkbd, tmp, sizeof(tmp));
    swkbdClose(&swkbd);
    if (R_FAILED(rc)) return 0;
    // tmp is NUL-terminated if the user confirmed.
    if (tmp[0] == 0) return 0;
    strncpy(out, tmp, out_sz - 1);
    out[out_sz - 1] = 0;
    return 1;
}

static void print_header(uint64_t now) {
    int cur, paid;
    rental_status_t st = rental_check(&g_cfg, &g_state, now, &cur, &paid);
    const char *mc = rental_machine_code(&g_state);

    clear_screen();
    printf("=== PCTL All-in-One (Rental Edition) ===\n\n");
    printf(" Status      : %s\n", rental_status_str(st));
    printf(" Machine code: %s\n", mc);
    printf(" Period len  : %d days\n", g_cfg.period_days);
    printf(" Current per : #%d\n", cur);
    if (paid >= 0) {
        uint64_t period_len = (uint64_t)g_cfg.period_days * 86400ULL;
        uint64_t end = RENTAL_EPOCH + (uint64_t)(paid + 1) * period_len - 1ULL;
        time_t et = (time_t)end;
        struct tm *tm = gmtime(&et);
        char date[32];
        if (tm) strftime(date, sizeof(date), "%Y-%m-%d", tm);
        else    snprintf(date, sizeof(date), "?");
        printf(" Unlocked to : #%d (until %s UTC)\n", paid, date);
    } else {
        printf(" Unlocked to : (never unlocked)\n");
    }
    printf(" Web UI      : http://<Switch-IP>:%d\n", HTTP_PORT);
    printf(" Forced lock : %s\n", g_forced_lock ? "YES" : "no");
    printf("\n-----------------------------------------\n");
}

static void do_enter_key(void) {
    char key[32];
    printf("\nLaunching on-screen keyboard...\n");
    consoleUpdate(NULL);
    if (!prompt_key(key, sizeof(key))) {
        printf("Cancelled.\n");
        return;
    }
    rental_status_t rc = rental_submit_key(&g_cfg, &g_state, key);
    if (rc == RENTAL_OK) {
        printf("SUCCESS: key accepted for period.\n");
        uint64_t now = (uint64_t)time(NULL);
        rental_status_t ns = rental_check(&g_cfg, &g_state, now, NULL, NULL);
        apply_status(ns);
    } else if (rc == RENTAL_COOLDOWN) {
        int left = 0;
        uint64_t now = (uint64_t)time(NULL);
        if (now < g_state.cooldown_until) left = (int)(g_state.cooldown_until - now);
        printf("Too many wrong attempts. Wait %d seconds.\n", left);
    } else {
        printf("WRONG KEY. Attempts until cooldown: %d / %d\n",
               g_state.wrong_attempts, WRONG_ATTEMPT_LIMIT);
    }
}

static void do_show_current_key(void) {
    uint64_t now = (uint64_t)time(NULL);
    int cur;
    rental_current_period(&g_cfg, now, &cur, NULL, NULL);
    char key[KEY_LEN + 1];
    rental_derive_key(&g_cfg, &g_state, cur, key);
    printf("\nCurrent period #%d unlock key (demo only): %s\n", cur, key);
    printf("(This key only works on THIS console and with the compiled-in salt.)\n");
}

static void do_toggle_force(void) {
    // Manual emergency override (e.g. to test lock/unlock).
    g_forced_lock = !g_forced_lock;
    if (g_forced_lock) rental_enforce_lock();
    else               rental_enforce_unlock();
    printf("Forced lock set to: %s\n", g_forced_lock ? "YES" : "no");
}

static void do_quit(void) {
    // When quitting the NRO, make sure the current state is applied.
    uint64_t now = (uint64_t)time(NULL);
    rental_status_t st = rental_check(&g_cfg, &g_state, now, NULL, NULL);
    apply_status(st);
    printf("Applying final state: %s\n", rental_status_str(st));
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // Initialize standard Switch services we need.
    Result rc;
    rc = setsysInitialize();
    if (R_FAILED(rc)) {
        fprintf(stderr, "setsysInitialize failed: 0x%x\n", (unsigned)rc);
        return 1;
    }
    rc = pmshellInitialize();
    if (R_FAILED(rc)) {
        // Not strictly fatal.
        fprintf(stderr, "pmshellInitialize failed: 0x%x\n", (unsigned)rc);
    }
    rc = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(rc)) {
        fprintf(stderr, "nifmInitialize failed (network unavailable): 0x%x\n", (unsigned)rc);
    }
    rc = timeInitialize();
    if (R_FAILED(rc)) {
        fprintf(stderr, "timeInitialize failed: 0x%x\n", (unsigned)rc);
    }
    socketInitializeDefault();
    fsInitialize();

    // Make sure the directory exists on SD.
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/pctltcp", 0777);

    if (rental_init(&g_cfg, &g_state) != 0) {
        fprintf(stderr, "rental_init failed\n");
    }

    // Initial enforcement based on current state.
    uint64_t now = (uint64_t)time(NULL);
    rental_status_t initial = rental_check(&g_cfg, &g_state, now, NULL, NULL);
    apply_status(initial);

    // Start HTTP server.
    if (miniserv_start(&g_srv, &g_cfg, &g_state, apply_status) != 0) {
        fprintf(stderr, "Failed to start HTTP server on port %d\n", HTTP_PORT);
    }

    // Initialize the console UI.
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);

    printf("\x1b[2J\x1b[H");
    printf("PCTL All-in-One Rental Edition booted.\n");
    printf("Initial status: %s\n", rental_status_str(initial));
    printf("Press A to open menu.\n");
    consoleUpdate(NULL);
    svcSleepThread(500 * 1000 * 1000ULL);

    bool running = true;
    while (running && appletMainLoop()) {
        now = (uint64_t)time(NULL);
        padUpdate(&g_pad);
        u64 k = padGetButtonsDown(&g_pad);

        // Always pump HTTP server.
        miniserv_pump(&g_srv);

        int cur, paid;
        rental_status_t st = rental_check(&g_cfg, &g_state, now, &cur, &paid);
        (void)st;

        if (k & HidNpadButton_Plus) {
            running = false;
            break;
        }

        if (k & HidNpadButton_A) {
            print_header(now);
            printf("\nMenu:\n");
            printf("  A - Enter unlock key\n");
            printf("  X - Show current period key (dev)\n");
            printf("  Y - Toggle force lock\n");
            printf("  B / + - Exit\n");
            consoleUpdate(NULL);

            bool in_menu = true;
            while (in_menu && appletMainLoop()) {
                padUpdate(&g_pad);
                u64 mk = padGetButtonsDown(&g_pad);
                miniserv_pump(&g_srv);
                if (mk & HidNpadButton_A) { do_enter_key(); in_menu = false; }
                else if (mk & HidNpadButton_X) { do_show_current_key(); wait_any_key(); in_menu = false; }
                else if (mk & HidNpadButton_Y) { do_toggle_force(); wait_any_key(); in_menu = false; }
                else if (mk & (HidNpadButton_B | HidNpadButton_Plus)) { in_menu = false; running = false; break; }
                svcSleepThread(16 * 1000 * 1000ULL);
            }
        }

        // Print a small live status banner.
        print_header(now);
        printf("\nPress A for menu | PLUS to exit\n");
        consoleUpdate(NULL);

        svcSleepThread(200 * 1000 * 1000ULL);
    }

    do_quit();
    consoleUpdate(NULL);
    svcSleepThread(300 * 1000 * 1000ULL);

    miniserv_stop(&g_srv);

    // Tear down.
    socketExit();
    timeExit();
    nifmExit();
    pmshellExit();
    setsysExit();
    fsExit();
    return 0;
}
