#pragma once
#include <switch.h>
#include <stdint.h>

// Fill `out` with the device id as a hex string (lower-case), e.g.
// "4000a1b2c3d4e5f6". out must have room for at least 33 chars (32 hex + NUL).
// Returns 0 on success.
int pctl_get_device_id_hex(char *out, size_t out_sz);

// Set the daily play-timer limit.
// minutes == 0 disallows launching any games (rental locked).
// minutes == 0xFFFF means "unset" (parental control play timer disabled, normal play).
// Values 1..1440 set a specific limit.
// Returns 0 on success.
int pctl_set_play_minutes(uint16_t minutes);
