#pragma once
#include <stdint.h>
#include "esp_err.h"

/* Brings up I2C, RGB panel, backlight, LVGL (esp_lvgl_port) and GT911 touch. */
esp_err_t display_init(void);

/* Software contrast / saturation (0..255, 128 = normal). Call with the LVGL lock held. */
void display_set_color_adjust(uint8_t contrast, uint8_t saturation);

/* Wait (up to timeout_ms) until LVGL has finished drawing the next frame. Call WITHOUT the
 * LVGL lock held. Used to switch the backlight on only once the first screen is visible. */
void display_wait_next_frame(uint32_t timeout_ms);
