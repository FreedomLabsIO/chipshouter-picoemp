#include "serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"

#include "settings.h"

static char serial_buffer[256];
static char last_command[256];

#define PULSE_DELAY_CYCLES_DEFAULT PICOEMP_DEFAULT_PULSE_DELAY_CYCLES
#define PULSE_TIME_CYCLES_DEFAULT PICOEMP_DEFAULT_PULSE_TIME_CYCLES // 5us in 8ns cycles
#define PULSE_TIME_NS_DEFAULT PICOEMP_DEFAULT_PULSE_TIME_NS // 5us
#define PULSE_POWER_DEFAULT PICOEMP_DEFAULT_PULSE_POWER
static uint32_t pulse_time_ns;
static uint32_t pulse_delay_cycles;
static uint32_t pulse_time_cycles;
static union float_union {float f; uint32_t ui32;} pulse_power;

static bool save_settings_to_flash() {
    picoemp_settings_t settings = {
        .pulse_time_ns = pulse_time_ns,
        .pulse_power = pulse_power.f,
    };

    multicore_fifo_push_blocking(cmd_flash_lockout);
    if(multicore_fifo_pop_blocking() != return_ok) {
        return false;
    }

    bool saved = picoemp_settings_save(&settings);
    picoemp_settings_flash_lockout_release();
    while(picoemp_settings_flash_lockout_is_engaged()) {
        __asm volatile("nop");
    }

    return saved;
}

static void monitor_fast_trigger() {
    bool stop_requested = false;

    while(1) {
        if(multicore_fifo_rvalid()) {
            uint32_t result = multicore_fifo_pop_blocking();
            if(result == return_triggered) {
                printf("Triggered!\n");
            } else if(result == return_ok) {
                while(1) {
                    int c = getchar_timeout_us(0);
                    if(c == PICO_ERROR_TIMEOUT || c == EOF) {
                        break;
                    }
                }
                printf("Fast trigger stopped.\n");
                return;
            }
        }

        if(!stop_requested) {
            int c = getchar_timeout_us(1000);
            if(c != PICO_ERROR_TIMEOUT && c != EOF) {
                multicore_fifo_push_blocking(cmd_stop_fast_trigger);
                stop_requested = true;
            }
        } else {
            sleep_ms(1);
        }
    }
}

void read_line() {
    memset(serial_buffer, 0, sizeof(serial_buffer));
    while(1) {
        int c = getchar();
        if(c == EOF) {
            return;
        }

        putchar(c);

        if(c == '\r') {
            return;
        }
        if(c == '\n') {
            continue;
        }

        // buffer full, just return.
        if(strlen(serial_buffer) >= 255) {
            return;
        }

        serial_buffer[strlen(serial_buffer)] = (char)c;
    }
}

void print_status(uint32_t status) {
    bool armed = (status >> 0) & 1;
    bool charged = (status >> 1) & 1;
    bool timeout_active = (status >> 2) & 1;
    bool hvp_mode = (status >> 3) & 1;
    printf("Status:\n");
    if(armed) {
        printf("- Armed\n");
    } else {
        printf("- Disarmed\n");
    }
    if(charged) {
        printf("- Charged\n");
    } else {
        printf("- Not charged\n");
    }
    if(timeout_active) {
        printf("- Timeout active\n");
    } else {
        printf("- Timeout disabled\n");
    }
    if(hvp_mode) {
        printf("- HVP internal\n");
    } else {
        printf("- HVP external\n");
    }
}

bool handle_command(char *command) {
    if (command[0] == 0 && last_command[0] != 0) {
        printf("Repeat previous command (%s)\n", last_command);
        return handle_command(last_command);
    } else {
        strcpy(last_command, command);
    }

    if(strcmp(command, "h") == 0 || strcmp(command, "help") == 0)
        return false;

    if(strcmp(command, "a") == 0 || strcmp(command, "arm") == 0) {
        multicore_fifo_push_blocking(cmd_arm);
        uint32_t result = multicore_fifo_pop_blocking();
        if(result == return_ok) {
            printf("Device armed!\n");
        } else {
            printf("Arming failed!\n");
        }
        return true;
    }
    if(strcmp(command, "d") == 0 || strcmp(command, "disarm") == 0) {
        multicore_fifo_push_blocking(cmd_disarm);
        uint32_t result = multicore_fifo_pop_blocking();
        if(result == return_ok) {
            printf("Device disarmed!\n");
        } else {
            printf("Disarming failed!\n");
        }
        return true;
    }
    if(strcmp(command, "p") == 0 || strcmp(command, "pulse") == 0) {
        multicore_fifo_push_blocking(cmd_pulse);
        uint32_t result = multicore_fifo_pop_blocking();
        if(result == return_ok) {
            printf("Pulsed!\n");
        } else {
            printf("Pulse failed!\n");
        }
        return true;
    }
    if(strcmp(command, "s") == 0 || strcmp(command, "status") == 0) {
        multicore_fifo_push_blocking(cmd_status);
        uint32_t result = multicore_fifo_pop_blocking();
        if(result == return_ok) {
            print_status(multicore_fifo_pop_blocking());
        } else {
            printf("Getting status failed!\n");
        }
        return true;
    }
    if(strcmp(command, "en") == 0 || strcmp(command, "enable_timeout") == 0) {
        multicore_fifo_push_blocking(cmd_enable_timeout);
        uint32_t result = multicore_fifo_pop_blocking();
        if(result == return_ok) {
            printf("Timeout enabled!\n");
        } else {
            printf("Enabling timeout failed!\n");
        }
        return true;
    }
    if(strcmp(command, "di") == 0 || strcmp(command, "disable_timeout") == 0) {
        multicore_fifo_push_blocking(cmd_disable_timeout);
        uint32_t result = multicore_fifo_pop_blocking();
        if(result == return_ok) {
            printf("Timeout disabled!\n");
        } else {
            printf("Disabling timeout failed!\n");
        }
        return true;
    }
    if(strcmp(command, "f") == 0 || strcmp(command, "fast_trigger") == 0) {
        multicore_fifo_push_blocking(cmd_fast_trigger);
        uint32_t result = multicore_fifo_pop_blocking();
        if(result == return_ok) {
            printf("Fast trigger active. Send any byte to stop.\n");
            monitor_fast_trigger();
        } else {
            printf("Setting up fast trigger failed.");
        }
        return true;
    }
    if(strcmp(command, "fa") == 0 || strcmp(command, "fast_trigger_configure") == 0) {
        char **unused;
        printf(" configure in cycles\n");
        printf("  1 cycle = 8ns\n");
        printf("  1us = 125 cycles\n");
        printf("  1ms = 125000 cycles\n");
        printf("  max = MAX_UINT32 = 4294967295 cycles = 34359ms\n");

        printf(" pulse_delay_cycles (current: %d, default: %d)?\n> ", pulse_delay_cycles, PULSE_DELAY_CYCLES_DEFAULT);
        read_line();
        printf("\n");
        if (serial_buffer[0] == 0)
            printf("Using default\n");
        else
            pulse_delay_cycles = strtoul(serial_buffer, unused, 10);
        
        printf(" pulse_time_cycles (current: %d, default: %d)?\n> ", pulse_time_cycles, PULSE_TIME_CYCLES_DEFAULT);
        read_line();
        printf("\n");
        if (serial_buffer[0] == 0)
            printf("Using default\n");
        else
            pulse_time_cycles = strtoul(serial_buffer, unused, 10);

        multicore_fifo_push_blocking(cmd_config_pulse_delay_cycles);
        multicore_fifo_push_blocking(pulse_delay_cycles);
        uint32_t result = multicore_fifo_pop_blocking();
        if(result != return_ok) {
            printf("Config pulse_delay_cycles failed.");
        }

        multicore_fifo_push_blocking(cmd_config_pulse_time_cycles);
        multicore_fifo_push_blocking(pulse_time_cycles);
        result = multicore_fifo_pop_blocking();
        if(result != return_ok) {
            printf("Config pulse_time_cycles failed.");
        }

        printf("pulse_delay_cycles=%d, pulse_time_cycles=%d\n", pulse_delay_cycles, pulse_time_cycles);

        return true;
    }
    if(strcmp(command, "in") == 0 || strcmp(command, "internal_hvp") == 0) {
        multicore_fifo_push_blocking(cmd_internal_hvp);
        uint32_t result = multicore_fifo_pop_blocking();
        if(result == return_ok) {
            printf("Internal HVP mode active!\n");
        } else {
            printf("Setting up internal HVP mode failed.");
        }
        return true;
    }
    if(strcmp(command, "ex") == 0 || strcmp(command, "external_hvp") == 0) {
        multicore_fifo_push_blocking(cmd_external_hvp);
        uint32_t result = multicore_fifo_pop_blocking();
        if(result == return_ok) {
            printf("External HVP mode active!\n");
        } else {
            printf("Setting up external HVP mode failed.");
        }
        return true;
    }

    if(strcmp(command, "c") == 0 || strcmp(command, "configure") == 0) {
        char **unused;
        bool was_armed = false;
        bool config_ok = true;
        uint32_t status = 0;
        uint32_t result = 0;

        multicore_fifo_push_blocking(cmd_status);
        result = multicore_fifo_pop_blocking();
        if(result != return_ok) {
            printf("Getting status failed.\n");
            return true;
        }

        status = multicore_fifo_pop_blocking();
        was_armed = (status & 0x1u) != 0;
        if(was_armed) {
            multicore_fifo_push_blocking(cmd_disarm);
            result = multicore_fifo_pop_blocking();
            if(result != return_ok) {
                printf("Auto-disarm before configuration failed.\n");
                return true;
            }
            printf("Device disarmed for configuration.\n");
        }

        printf(" pulse_time_ns (current: %u, default: %u)?\n> ", pulse_time_ns, PULSE_TIME_NS_DEFAULT);
        read_line();
        printf("\n");
        if (serial_buffer[0] == 0)
            printf("Using default\n");
        else
            pulse_time_ns = strtoul(serial_buffer, unused, 10);

        printf(" pulse_power (current: %f, default: %f)?\n> ", pulse_power.f, PULSE_POWER_DEFAULT);
        read_line();
        printf("\n");
        if (serial_buffer[0] == 0)
            printf("Using default");
        else
            pulse_power.f = strtof(serial_buffer, unused);

        multicore_fifo_push_blocking(cmd_config_pulse_time);
        multicore_fifo_push_blocking(pulse_time_ns);
        result = multicore_fifo_pop_blocking();
        if(result != return_ok) {
            printf("Config pulse_time failed.");
            config_ok = false;
        }

        multicore_fifo_push_blocking(cmd_config_pulse_power);
        multicore_fifo_push_blocking(pulse_power.ui32);
        result = multicore_fifo_pop_blocking();
        if(result != return_ok) {
            printf("Config pulse_power failed.");
            config_ok = false;
        }

        if(config_ok) {
            if(save_settings_to_flash()) {
                printf("Settings saved.\n");
            } else {
                printf("Saving settings failed.\n");
            }
        }

        if(was_armed) {
            multicore_fifo_push_blocking(cmd_arm);
            result = multicore_fifo_pop_blocking();
            if(result == return_ok) {
                printf("Device re-armed after configuration.\n");
            } else {
                printf("Auto re-arm after configuration failed.\n");
            }
        }

        printf("pulse_time_ns=%u, pulse_power=%f\n", pulse_time_ns, pulse_power.f);

        return true;
    }

    if(strcmp(command, "t") == 0 || strcmp(command, "toggle_gp1") == 0) {
        multicore_fifo_push_blocking(cmd_toggle_gp1);
        
        uint32_t result = multicore_fifo_pop_blocking();
        if(result != return_ok) {
            printf("target_reset failed.");
        }

        return true;
    }

    if(strcmp(command, "r") == 0 || strcmp(command, "reset") == 0) {
        watchdog_enable(1, 1);
        while(1);
    }

    return false;
}

void serial_console() {
    picoemp_settings_t startup_settings;

    multicore_fifo_drain();

    memset(last_command, 0, sizeof(last_command));

    picoemp_settings_load_defaults(&startup_settings);
    picoemp_settings_load(&startup_settings);
    pulse_time_ns = startup_settings.pulse_time_ns;
    pulse_power.f = startup_settings.pulse_power;
    pulse_delay_cycles = PULSE_DELAY_CYCLES_DEFAULT;
    pulse_time_cycles = PULSE_TIME_CYCLES_DEFAULT;
    
    while(1) {
        read_line();
        printf("\n");
        if(!handle_command(serial_buffer)) {
            printf("PicoEMP Commands:\n");
            printf("- <empty to repeat last command>\n");
            printf("- [h]elp\n");
            printf("- [a]rm\n");
            printf("- [d]isarm\n");
            printf("- [p]ulse\n");
            printf("- [en]able_timeout\n");
            printf("- [di]sable_timeout\n");
            printf("- [f]ast_trigger\n");
            printf("- [fa]st_trigger_configure: delay_cycles=%d, time_cycles=%d\n", pulse_delay_cycles, pulse_time_cycles);
            printf("- [in]ternal_hvp\n");
            printf("- [ex]ternal_hvp\n");
            printf("- [c]onfigure: pulse_time_ns=%u, pulse_power=%f\n", pulse_time_ns, pulse_power.f);
            printf("- [t]oggle_gp1\n");
            printf("- [s]tatus\n");
            printf("- [r]eset\n");
        }
        printf("\n");
        
        if (last_command[0] != 0) {
            printf("[%s] > ", last_command);
        } else {
            printf(" > ");
        }
    }
}
