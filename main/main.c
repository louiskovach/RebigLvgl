#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_lvgl_port.h"
#include "display_driver.h"
#include "board_periph.h"
#include "settings.h"
#include "ui.h"
#include "net_time.h"
#include "plc_link.h"
#include "modbus_master.h"
#include "mb_poll.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "main";

/* Firmware build time (local time of the build PC) -> UTC, used when the RTC has no valid time. */
static time_t build_time_utc(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {0};
    int day = 1, year = 2026, hh = 0, mm = 0, ss = 0;
    sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);

    struct tm tm = {0};
    const char *p = strstr(months, mon);
    tm.tm_mon  = p ? (int)(p - months) / 3 : 0;
    tm.tm_mday = day;
    tm.tm_year = year - 1900;
    tm.tm_hour = hh;
    tm.tm_min  = mm;
    tm.tm_sec  = ss;
    return mktime(&tm) - settings_utc_offset_s();
}

static void clock_init(void)
{
    time_t t;
    if (!board_rtc_read(&t)) {
        ESP_LOGW(TAG, "RTC time invalid - using build time");
        t = build_time_utc();
        board_rtc_write(t);
    }
    struct timeval tv = { .tv_sec = t };
    settimeofday(&tv, NULL);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    settings_load();

    ESP_ERROR_CHECK(display_init());               /* backlight still off */
    clock_init();
    net_time_init();   /* Wi-Fi FIRST: it needs internal RAM, which the UI objects use up */

    if (lvgl_port_lock(0)) {
        display_set_color_adjust(g_settings.disp_contrast, g_settings.disp_saturation);
        ui_init();
        lvgl_port_unlock();
    }
    display_wait_next_frame(500);                  /* LVGL task draws the first screen...   */
    board_backlight_on();                          /* ...and only then light it up          */
    board_set_brightness(g_settings.disp_brightness);

    /* PLC LAST: its task stacks and the debug console's UART driver use internal RAM, which
     * the display and Wi-Fi need first (starting it earlier left the screen black). */
    plc_link_start();
#if CONFIG_MB_ENABLE
    if (mb_master_init())                          /* RS-485 Modbus master (UART1, IO4/5/6) */
        mb_poll_start();                           /* its own task: slaves <-> PLC memory (mb_map.c) */
#endif
    ESP_LOGI("main", "free internal RAM: %u bytes (largest block %u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}
