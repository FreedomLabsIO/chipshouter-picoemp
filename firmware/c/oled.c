#include "oled.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define OLED_I2C i2c1
#define OLED_I2C_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_PAGE_COUNT (OLED_HEIGHT / 8)
#define OLED_COM_PINS_CONFIG 0x02

#define OLED_PIN_SDA 2
#define OLED_PIN_SCL 3
#define OLED_PIN_VCC 4
#define OLED_PIN_GND 5

#define OLED_FONT_WIDTH 5
#define OLED_FONT_HEIGHT 7
#define OLED_CHAR_SPACING 1

static uint8_t oled_buffer[OLED_WIDTH * OLED_PAGE_COUNT];
static const uint8_t armed_row_repeats[OLED_FONT_HEIGHT] = {1, 1, 2, 2, 2, 1, 1};
static bool oled_show(void);

static bool oled_write_command(uint8_t command) {
    uint8_t buffer[2] = {0x00, command};
    return i2c_write_blocking(OLED_I2C, OLED_I2C_ADDRESS, buffer, sizeof(buffer), false) >= 0;
}

static bool oled_write_commands(const uint8_t *commands, size_t length) {
    for(size_t i = 0; i < length; ++i) {
        if(!oled_write_command(commands[i])) {
            return false;
        }
    }

    return true;
}

static void oled_clear(void) {
    memset(oled_buffer, 0, sizeof(oled_buffer));
}

static void oled_draw_pixel(uint8_t x, uint8_t y, bool on) {
    if(x >= OLED_WIDTH || y >= OLED_HEIGHT) {
        return;
    }

    uint16_t index = x + ((y / 8) * OLED_WIDTH);
    uint8_t mask = 1u << (y & 0x7);
    if(on) {
        oled_buffer[index] |= mask;
    } else {
        oled_buffer[index] &= (uint8_t)~mask;
    }
}

static const uint8_t *oled_get_glyph(char c) {
    static const uint8_t blank[OLED_FONT_WIDTH] = {0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t glyph_exclamation[OLED_FONT_WIDTH] = {0x00, 0x00, 0x5F, 0x00, 0x00};
    static const uint8_t glyph_hyphen[OLED_FONT_WIDTH] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t glyph_A[OLED_FONT_WIDTH] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
    static const uint8_t glyph_D[OLED_FONT_WIDTH] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
    static const uint8_t glyph_G[OLED_FONT_WIDTH] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
    static const uint8_t glyph_H[OLED_FONT_WIDTH] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
    static const uint8_t glyph_I[OLED_FONT_WIDTH] = {0x00, 0x41, 0x7F, 0x41, 0x00};
    static const uint8_t glyph_L[OLED_FONT_WIDTH] = {0x7F, 0x40, 0x40, 0x40, 0x40};
    static const uint8_t glyph_O[OLED_FONT_WIDTH] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
    static const uint8_t glyph_P[OLED_FONT_WIDTH] = {0x7F, 0x09, 0x09, 0x09, 0x06};
    static const uint8_t glyph_R[OLED_FONT_WIDTH] = {0x7F, 0x09, 0x19, 0x29, 0x46};
    static const uint8_t glyph_S[OLED_FONT_WIDTH] = {0x46, 0x49, 0x49, 0x49, 0x31};
    static const uint8_t glyph_T[OLED_FONT_WIDTH] = {0x01, 0x01, 0x7F, 0x01, 0x01};
    static const uint8_t glyph_U[OLED_FONT_WIDTH] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
    static const uint8_t glyph_V[OLED_FONT_WIDTH] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
    static const uint8_t glyph_i[OLED_FONT_WIDTH] = {0x00, 0x41, 0x7D, 0x40, 0x00};
    static const uint8_t glyph_c[OLED_FONT_WIDTH] = {0x38, 0x44, 0x44, 0x44, 0x28};
    static const uint8_t glyph_o[OLED_FONT_WIDTH] = {0x38, 0x44, 0x44, 0x44, 0x38};
    static const uint8_t glyph_E[OLED_FONT_WIDTH] = {0x7F, 0x49, 0x49, 0x49, 0x41};
    static const uint8_t glyph_M[OLED_FONT_WIDTH] = {0x7F, 0x02, 0x04, 0x02, 0x7F};

    switch(c) {
        case '!':
            return glyph_exclamation;
        case '-':
            return glyph_hyphen;
        case ' ':
            return blank;
        case 'A':
            return glyph_A;
        case 'D':
            return glyph_D;
        case 'G':
            return glyph_G;
        case 'H':
            return glyph_H;
        case 'I':
            return glyph_I;
        case 'L':
            return glyph_L;
        case 'O':
            return glyph_O;
        case 'P':
            return glyph_P;
        case 'R':
            return glyph_R;
        case 'S':
            return glyph_S;
        case 'T':
            return glyph_T;
        case 'U':
            return glyph_U;
        case 'V':
            return glyph_V;
        case 'i':
            return glyph_i;
        case 'c':
            return glyph_c;
        case 'o':
            return glyph_o;
        case 'E':
            return glyph_E;
        case 'M':
            return glyph_M;
        default:
            return blank;
    }
}

static void oled_draw_char_scaled_xy(uint8_t x, uint8_t y, char c, uint8_t scale_x, uint8_t scale_y) {
    const uint8_t *glyph = oled_get_glyph(c);

    for(uint8_t col = 0; col < OLED_FONT_WIDTH; ++col) {
        for(uint8_t row = 0; row < OLED_FONT_HEIGHT; ++row) {
            if((glyph[col] >> row) & 0x1) {
                for(uint8_t dx = 0; dx < scale_x; ++dx) {
                    for(uint8_t dy = 0; dy < scale_y; ++dy) {
                        oled_draw_pixel((uint8_t)(x + (col * scale_x) + dx), (uint8_t)(y + (row * scale_y) + dy), true);
                    }
                }
            }
        }
    }
}

static void oled_draw_char_scaled_x_row_repeats(uint8_t x, uint8_t y, char c, uint8_t scale_x, const uint8_t *row_repeats) {
    const uint8_t *glyph = oled_get_glyph(c);
    uint8_t y_offset = 0;

    for(uint8_t row = 0; row < OLED_FONT_HEIGHT; ++row) {
        uint8_t repeat_count = row_repeats[row];
        if(repeat_count == 0) {
            continue;
        }

        for(uint8_t col = 0; col < OLED_FONT_WIDTH; ++col) {
            if(((glyph[col] >> row) & 0x1) == 0) {
                continue;
            }

            for(uint8_t dx = 0; dx < scale_x; ++dx) {
                for(uint8_t dy = 0; dy < repeat_count; ++dy) {
                    oled_draw_pixel((uint8_t)(x + (col * scale_x) + dx), (uint8_t)(y + y_offset + dy), true);
                }
            }
        }

        y_offset = (uint8_t)(y_offset + repeat_count);
    }
}

static void oled_draw_char_scaled(uint8_t x, uint8_t y, char c, uint8_t scale) {
    oled_draw_char_scaled_xy(x, y, c, scale, scale);
}

static uint8_t oled_measure_text_width_xy(const char *text, uint8_t scale_x, uint8_t spacing) {
    size_t text_length = strlen(text);
    if(text_length == 0) {
        return 0;
    }

    return (uint8_t)((text_length * OLED_FONT_WIDTH * scale_x) + ((text_length - 1) * spacing * scale_x));
}

static uint8_t oled_measure_text_width(const char *text, uint8_t scale, uint8_t spacing) {
    return oled_measure_text_width_xy(text, scale, spacing);
}

static void oled_draw_text_xy(uint8_t x, uint8_t y, const char *text, uint8_t scale_x, uint8_t scale_y, uint8_t spacing) {
    uint8_t cursor_x = x;

    for(size_t i = 0; text[i] != '\0'; ++i) {
        oled_draw_char_scaled_xy(cursor_x, y, text[i], scale_x, scale_y);
        cursor_x = (uint8_t)(cursor_x + (OLED_FONT_WIDTH * scale_x) + (spacing * scale_x));
    }
}

static void oled_draw_text_x_row_repeats(uint8_t x, uint8_t y, const char *text, uint8_t scale_x, const uint8_t *row_repeats, uint8_t spacing) {
    uint8_t cursor_x = x;

    for(size_t i = 0; text[i] != '\0'; ++i) {
        oled_draw_char_scaled_x_row_repeats(cursor_x, y, text[i], scale_x, row_repeats);
        cursor_x = (uint8_t)(cursor_x + (OLED_FONT_WIDTH * scale_x) + (spacing * scale_x));
    }
}

static void oled_draw_text(uint8_t x, uint8_t y, const char *text, uint8_t scale, uint8_t spacing) {
    oled_draw_text_xy(x, y, text, scale, scale, spacing);
}

static void oled_draw_text_centered_x_row_repeats(uint8_t y, const char *text, uint8_t scale_x, const uint8_t *row_repeats, uint8_t spacing) {
    uint8_t text_width = oled_measure_text_width_xy(text, scale_x, spacing);
    uint8_t start_x = (uint8_t)((OLED_WIDTH - text_width) / 2);
    oled_draw_text_x_row_repeats(start_x, y, text, scale_x, row_repeats, spacing);
}

static void oled_draw_text_centered_xy(uint8_t y, const char *text, uint8_t scale_x, uint8_t scale_y, uint8_t spacing) {
    uint8_t text_width = oled_measure_text_width_xy(text, scale_x, spacing);
    uint8_t start_x = (uint8_t)((OLED_WIDTH - text_width) / 2);
    oled_draw_text_xy(start_x, y, text, scale_x, scale_y, spacing);
}

static void oled_draw_text_centered(uint8_t y, const char *text, uint8_t scale, uint8_t spacing) {
    uint8_t text_width = oled_measure_text_width(text, scale, spacing);
    uint8_t start_x = (uint8_t)((OLED_WIDTH - text_width) / 2);
    oled_draw_text(start_x, y, text, scale, spacing);
}

void picoemp_oled_show_idle() {
    oled_clear();
    oled_draw_text_centered(9, "PicoEMP", 2, OLED_CHAR_SPACING);
    oled_show();
}

void picoemp_oled_show_armed() {
    oled_clear();
    oled_draw_text_centered(0, "PicoEMP", 1, OLED_CHAR_SPACING);
    oled_draw_text_centered_x_row_repeats(10, "!ARMED!", 2, armed_row_repeats, 1);
    oled_draw_text_centered(25, "HIGH VOLTAGE", 1, 0);
    oled_show();
}

void picoemp_oled_show_pulse() {
    oled_clear();
    oled_draw_text_centered(9, "PULSE", 2, OLED_CHAR_SPACING);
    oled_show();
}

void picoemp_oled_init() {
    gpio_init(OLED_PIN_VCC);
    gpio_set_dir(OLED_PIN_VCC, GPIO_OUT);
    gpio_put(OLED_PIN_VCC, true);

    gpio_init(OLED_PIN_GND);
    gpio_set_dir(OLED_PIN_GND, GPIO_OUT);
    gpio_put(OLED_PIN_GND, false);

    sleep_ms(100);

    i2c_init(OLED_I2C, 400 * 1000);
    gpio_set_function(OLED_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(OLED_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_PIN_SDA);
    gpio_pull_up(OLED_PIN_SCL);

    static const uint8_t init_commands[] = {
        0xAE,
        0x20, 0x00,
        0xB0,
        0xC8,
        0x00,
        0x10,
        0x40,
        0x81, 0x7F,
        0xA1,
        0xA6,
        0xA8, OLED_HEIGHT - 1,
        0xD3, 0x00,
        0xD5, 0x80,
        0xD9, 0xF1,
        0xDA, OLED_COM_PINS_CONFIG,
        0xDB, 0x20,
        0x8D, 0x14,
        0xAF,
    };

    if(!oled_write_commands(init_commands, sizeof(init_commands))) {
        return;
    }

    picoemp_oled_show_idle();
}
static bool oled_show(void) {
    static const uint8_t setup_commands[] = {
        0x21, 0x00, OLED_WIDTH - 1,
        0x22, 0x00, OLED_PAGE_COUNT - 1,
    };

    if(!oled_write_commands(setup_commands, sizeof(setup_commands))) {
        return false;
    }

    uint8_t buffer[17];
    buffer[0] = 0x40;

    for(size_t i = 0; i < sizeof(oled_buffer); i += 16) {
        memcpy(&buffer[1], &oled_buffer[i], 16);
        if(i2c_write_blocking(OLED_I2C, OLED_I2C_ADDRESS, buffer, sizeof(buffer), false) < 0) {
            return false;
        }
    }

    return true;
}
