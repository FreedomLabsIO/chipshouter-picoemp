#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "picoemp.h"
#include "settings.h"
#include "serial.h"

#include "trigger_basic.pio.h"

static bool armed = false;
static bool timeout_active = true;
static bool hvp_internal = true;
static bool fast_trigger_active = false;
static bool fast_trigger_stop_requested = false;
static bool fast_trigger_led_active = false;
static bool fast_trigger_cycle_complete = false;
static absolute_time_t fast_trigger_led_off_time;
static absolute_time_t timeout_time;
static uint offset = 0xFFFFFFFF;

// defaults taken from original code
#define PULSE_DELAY_CYCLES_DEFAULT PICOEMP_DEFAULT_PULSE_DELAY_CYCLES
#define PULSE_TIME_CYCLES_DEFAULT PICOEMP_DEFAULT_PULSE_TIME_CYCLES // 5us in 8ns cycles
#define PULSE_TIME_US_DEFAULT PICOEMP_DEFAULT_PULSE_TIME_US // 5us
#define PULSE_POWER_DEFAULT PICOEMP_DEFAULT_PULSE_POWER
static uint32_t pulse_time;
static uint32_t pulse_delay_cycles;
static uint32_t pulse_time_cycles;
static union float_union {float f; uint32_t ui32;} pulse_power;

void arm() {
    gpio_put(PIN_LED_CHARGE_ON, true);
    armed = true;
    picoemp_set_armed_indicator(true);
}

void disarm() {
    gpio_put(PIN_LED_CHARGE_ON, false);
    armed = false;
    picoemp_disable_pwm();
    picoemp_set_armed_indicator(false);
}

uint32_t get_status() {
    uint32_t result = 0;
    if(armed) {
        result |= 0b1;
    }
    if(gpio_get(PIN_IN_CHARGED)) {
        result |= 0b10;
    }
    if(timeout_active) {
        result |= 0b100;
    }
    if(hvp_internal) {
        result |= 0b1000;
    }
    return result;
}

void update_timeout() {
    timeout_time = delayed_by_ms(get_absolute_time(), 60 * 1000);
}

void fast_trigger() {
    // Choose which PIO instance to use (there are two instances)
    PIO pio = pio0;

    // Our assembled program needs to be loaded into this PIO's instruction
    // memory. This SDK function will find a location (offset) in the
    // instruction memory where there is enough space for our program. We need
    // to remember this location!
    if (offset == 0xFFFFFFFF) { // Only load the program once
        offset = pio_add_program(pio, &trigger_basic_program);
    }
    
    // Find a free state machine on our chosen PIO (erroring if there are
    // none). Configure it to run our program, and start it, using the
    // helper function we included in our .pio file.
    uint sm = 0;
    trigger_basic_init(pio, sm, offset, PIN_IN_TRIGGER, PIN_OUT_HVPULSE);
    pio_sm_put_blocking(pio, sm, pulse_delay_cycles);
    pio_sm_put_blocking(pio, sm, pulse_time_cycles);

}

void stop_fast_trigger(bool wait_for_led) {
    if(!fast_trigger_active) {
        return;
    }

    pio_sm_set_enabled(pio0, 0, false);
    picoemp_configure_pulse_output();

    if(wait_for_led && fast_trigger_led_active) {
        fast_trigger_cycle_complete = true;
        return;
    }

    picoemp_set_pulse_indicator(false);
    fast_trigger_active = false;
    fast_trigger_stop_requested = false;
    fast_trigger_led_active = false;
    fast_trigger_cycle_complete = false;
    multicore_fifo_push_blocking(return_ok);
}

int main() {
    picoemp_settings_t startup_settings;

    // Initialize USB-UART as STDIO
    stdio_init_all();

    picoemp_init();

    // Init for reset pin (move somewhere else)
    gpio_init(1);
    gpio_set_dir(1, GPIO_OUT);
    gpio_put(1, 1);

    // Run serial-console on second core
    multicore_launch_core1(serial_console);

    picoemp_settings_load_defaults(&startup_settings);
    picoemp_settings_load(&startup_settings);
    pulse_time = startup_settings.pulse_time;
    pulse_power.f = startup_settings.pulse_power;
    pulse_delay_cycles = PULSE_DELAY_CYCLES_DEFAULT;
    pulse_time_cycles = PULSE_TIME_CYCLES_DEFAULT;

    while(1) {
        gpio_put(PIN_LED_HV, gpio_get(PIN_IN_CHARGED));

        // Handle serial commands (if any)
        while(multicore_fifo_rvalid()) {
            uint32_t command = multicore_fifo_pop_blocking();
            switch(command) {
                case cmd_arm:
                    arm();
                    update_timeout();
                    multicore_fifo_push_blocking(return_ok);
                    break;
                case cmd_disarm:
                    disarm();
                    multicore_fifo_push_blocking(return_ok);
                    break;
                case cmd_pulse:
                    picoemp_pulse(pulse_time);
                    update_timeout();
                    multicore_fifo_push_blocking(return_ok);
                    break;
                case cmd_status:
                    multicore_fifo_push_blocking(return_ok);
                    multicore_fifo_push_blocking(get_status());
                    break;
                case cmd_enable_timeout:
                    timeout_active = true;
                    update_timeout();
                    multicore_fifo_push_blocking(return_ok);
                    break;
                case cmd_disable_timeout:
                    timeout_active = false;
                    multicore_fifo_push_blocking(return_ok);
                    break;
                case cmd_config_pulse_delay_cycles:
                    pulse_delay_cycles = multicore_fifo_pop_blocking();
                    multicore_fifo_push_blocking(return_ok);
                    break;
                case cmd_config_pulse_time_cycles:
                    pulse_time_cycles = multicore_fifo_pop_blocking();
                    multicore_fifo_push_blocking(return_ok);
                    break;
                case cmd_fast_trigger:
                    if(fast_trigger_active) {
                        multicore_fifo_push_blocking(return_failed);
                        break;
                    }
                    fast_trigger_stop_requested = false;
                    fast_trigger_led_active = false;
                    fast_trigger_cycle_complete = false;
                    picoemp_set_pulse_indicator(false);
                    fast_trigger();
                    fast_trigger_active = true;
                    multicore_fifo_push_blocking(return_ok);
                    break;
                case cmd_stop_fast_trigger:
                    fast_trigger_stop_requested = true;
                    break;
                case cmd_internal_hvp:
                    picoemp_configure_pulse_output();
                    hvp_internal = true;
                    multicore_fifo_push_blocking(return_ok);
                    break;
                case cmd_external_hvp:
                    picoemp_configure_pulse_external();
                    hvp_internal = false;
                    multicore_fifo_push_blocking(return_ok);
                    break;
                case cmd_config_pulse_time:
                    pulse_time = multicore_fifo_pop_blocking();
                    multicore_fifo_push_blocking(return_ok);
                    break;
                case cmd_config_pulse_power:
                    pulse_power.ui32 = multicore_fifo_pop_blocking();
                    multicore_fifo_push_blocking(return_ok);
                    break;
                case cmd_flash_lockout:
                    picoemp_settings_flash_lockout_enter();
                    break;
                case cmd_toggle_gp1:
                    gpio_xor_mask(1<<1);
                    multicore_fifo_push_blocking(return_ok);
                    break;
            }
        }

        if(fast_trigger_active) {
            if(fast_trigger_stop_requested && !fast_trigger_led_active && !fast_trigger_cycle_complete) {
                stop_fast_trigger(false);
            }

            if(!fast_trigger_led_active && pio_interrupt_get(pio0, 1)) {
                picoemp_set_pulse_indicator(true);
                fast_trigger_led_active = true;
            }

            if(!fast_trigger_cycle_complete && pio_interrupt_get(pio0, 0)) {
                pio_sm_set_enabled(pio0, 0, false);
                picoemp_configure_pulse_output();
                fast_trigger_cycle_complete = true;
                fast_trigger_led_off_time = delayed_by_ms(get_absolute_time(), 250);
                multicore_fifo_push_blocking(return_triggered);
            }

            if(fast_trigger_led_active && fast_trigger_cycle_complete && (get_absolute_time() > fast_trigger_led_off_time)) {
                picoemp_set_pulse_indicator(false);
                fast_trigger_led_active = false;
                fast_trigger_cycle_complete = false;

                if(fast_trigger_stop_requested) {
                    stop_fast_trigger(false);
                } else {
                    fast_trigger();
                }
            }
        }

        // Pulse
        if(!fast_trigger_active && gpio_get(PIN_BTN_PULSE)) {
            update_timeout();
            picoemp_pulse(pulse_time);
        }

        if(gpio_get(PIN_BTN_ARM)) {
            update_timeout();
            if(fast_trigger_active) {
                fast_trigger_stop_requested = true;
                if(armed) {
                    disarm();
                }
            } else {
                if(!armed) {
                    arm();
                } else {
                    disarm();
                }
            }
            // YOLO debouncing
            while(gpio_get(PIN_BTN_ARM));
            sleep_ms(100);
        }

        if(!gpio_get(PIN_IN_CHARGED) && armed) {
            picoemp_enable_pwm(pulse_power.f);
        }

        if(timeout_active && (get_absolute_time() > timeout_time) && armed) {
            disarm();
            if(fast_trigger_active) {
                fast_trigger_stop_requested = true;
            }
        }
    }
    
    return 0;
}
