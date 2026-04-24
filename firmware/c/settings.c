#include "settings.h"

#include <assert.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "serial.h"

#ifndef PICO_FLASH_SIZE_BYTES
#error "PICO_FLASH_SIZE_BYTES must be defined"
#endif

#define SETTINGS_MAGIC 0x50454d50u
#define SETTINGS_VERSION 1u
#define SETTINGS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef union {
    float f;
    uint32_t u32;
} float_u32_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t pulse_time;
    uint32_t pulse_power_bits;
    uint32_t checksum;
} settings_storage_t;

extern uint8_t __flash_binary_end;

static volatile bool settings_flash_lockout_release_requested = false;
static volatile bool settings_flash_lockout_engaged = false;

static_assert((SETTINGS_FLASH_OFFSET % FLASH_SECTOR_SIZE) == 0, "settings sector must be flash-sector aligned");

static const settings_storage_t *settings_storage_get(void) {
    return (const settings_storage_t *)(XIP_BASE + SETTINGS_FLASH_OFFSET);
}

static uint32_t settings_checksum(uint32_t pulse_time, uint32_t pulse_power_bits) {
    return SETTINGS_MAGIC ^ SETTINGS_VERSION ^ pulse_time ^ pulse_power_bits ^ 0xC0DEC0DEu;
}

static bool settings_validate(const settings_storage_t *storage) {
    float_u32_t pulse_power = {.u32 = storage->pulse_power_bits};

    if(storage->magic != SETTINGS_MAGIC || storage->version != SETTINGS_VERSION) {
        return false;
    }

    if(storage->checksum != settings_checksum(storage->pulse_time, storage->pulse_power_bits)) {
        return false;
    }

    if(!(pulse_power.f >= 0.0f && pulse_power.f <= 1.0f)) {
        return false;
    }

    return true;
}

static void __not_in_flash_func(settings_write_page)(const uint8_t page[FLASH_PAGE_SIZE]) {
    uint32_t saved_interrupts = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts_from_disabled(saved_interrupts);
}

void picoemp_settings_load_defaults(picoemp_settings_t *settings) {
    if(settings == NULL) {
        return;
    }

    settings->pulse_time = PICOEMP_DEFAULT_PULSE_TIME_US;
    settings->pulse_power = PICOEMP_DEFAULT_PULSE_POWER;
}

bool picoemp_settings_load(picoemp_settings_t *settings) {
    const settings_storage_t *storage = settings_storage_get();
    float_u32_t pulse_power;

    if(settings == NULL || !settings_validate(storage)) {
        return false;
    }

    pulse_power.u32 = storage->pulse_power_bits;
    settings->pulse_time = storage->pulse_time;
    settings->pulse_power = pulse_power.f;
    return true;
}

bool picoemp_settings_save(const picoemp_settings_t *settings) {
    const settings_storage_t *current = settings_storage_get();
    uint8_t page[FLASH_PAGE_SIZE];
    settings_storage_t *storage = (settings_storage_t *)page;
    float_u32_t pulse_power;

    if(settings == NULL) {
        return false;
    }

    pulse_power.f = settings->pulse_power;
    if(!(pulse_power.f >= 0.0f && pulse_power.f <= 1.0f)) {
        return false;
    }

    assert((((uintptr_t)&__flash_binary_end) - XIP_BASE) <= SETTINGS_FLASH_OFFSET);

    if(settings_validate(current) &&
       current->pulse_time == settings->pulse_time &&
       current->pulse_power_bits == pulse_power.u32) {
        return true;
    }

    memset(page, 0xFF, sizeof(page));
    storage->magic = SETTINGS_MAGIC;
    storage->version = SETTINGS_VERSION;
    storage->pulse_time = settings->pulse_time;
    storage->pulse_power_bits = pulse_power.u32;
    storage->checksum = settings_checksum(storage->pulse_time, storage->pulse_power_bits);

    settings_write_page(page);

    current = settings_storage_get();
    return settings_validate(current) &&
           current->pulse_time == settings->pulse_time &&
           current->pulse_power_bits == pulse_power.u32;
}

void __not_in_flash_func(picoemp_settings_flash_lockout_enter)(void) {
    uint32_t saved_interrupts;

    settings_flash_lockout_release_requested = false;
    settings_flash_lockout_engaged = true;

    saved_interrupts = save_and_disable_interrupts();
    multicore_fifo_push_blocking(return_ok);
    while(!settings_flash_lockout_release_requested) {
        __asm volatile("nop");
    }
    restore_interrupts_from_disabled(saved_interrupts);

    settings_flash_lockout_engaged = false;
}

void picoemp_settings_flash_lockout_release(void) {
    settings_flash_lockout_release_requested = true;
}

bool picoemp_settings_flash_lockout_is_engaged(void) {
    return settings_flash_lockout_engaged;
}
