#pragma once
#include "rental.h"
#include <switch.h>

// Minimal HTTP server context.
typedef struct {
    int sock;
    int running;
    const rental_config_t *cfg;
    rental_state_t *state;
    // Callback to refresh/set lock state (so the web UI can immediately
    // enforce the new state after unlock).
    void (*on_state_changed)(rental_status_t new_status);
} miniserv_t;

int miniserv_start(miniserv_t *srv, const rental_config_t *cfg,
                   rental_state_t *state,
                   void (*on_state_changed)(rental_status_t));
void miniserv_stop(miniserv_t *srv);
// Run one iteration (accept and handle a single connection).
void miniserv_pump(miniserv_t *srv);
