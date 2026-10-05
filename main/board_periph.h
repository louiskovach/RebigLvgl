#pragma once
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* Co-processor (backlight + buzzer) and PCF8563 RTC on the shared I2C bus. */
void      board_periph_init(i2c_master_bus_handle_t bus);
void      board_backlight_on(void);
void      board_set_brightness(uint8_t level);   /* 0 = dimmest, 255 = brightest */
void      board_buzzer(bool on);            /* non-blocking */
bool      board_rtc_read(time_t *utc);      /* false if RTC missing or lost power */
esp_err_t board_rtc_write(time_t utc);
