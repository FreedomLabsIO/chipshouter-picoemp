#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PICOEMP_DEFAULT_PULSE_DELAY_CYCLES 0u
#define PICOEMP_DEFAULT_PULSE_TIME_CYCLES 625u
#define PICOEMP_DEFAULT_PULSE_TIME_NS 5000u
#define PICOEMP_DEFAULT_PULSE_POWER 0.0122f

typedef struct {
    uint32_t pulse_time_ns;
    float pulse_power;
} picoemp_settings_t;

void picoemp_settings_load_defaults(picoemp_settings_t *settings);
bool picoemp_settings_load(picoemp_settings_t *settings);
bool picoemp_settings_save(const picoemp_settings_t *settings);

void picoemp_settings_flash_lockout_enter(void);
void picoemp_settings_flash_lockout_release(void);
bool picoemp_settings_flash_lockout_is_engaged(void);
