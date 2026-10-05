#include "board_periph.h"
#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "periph";

static i2c_master_dev_handle_t s_coproc;
static i2c_master_dev_handle_t s_rtc;
static TaskHandle_t            s_buzz_task;
static volatile bool           s_buzz_want;

static i2c_master_dev_handle_t add_dev(i2c_master_bus_handle_t bus, uint8_t addr, const char *name)
{
    if (i2c_master_probe(bus, addr, 50) != ESP_OK) {
        ESP_LOGW(TAG, "%s not found at 0x%02X", name, addr);
        return NULL;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t h = NULL;
    if (i2c_master_bus_add_device(bus, &cfg, &h) != ESP_OK) return NULL;
    return h;
}

static void coproc_send(uint8_t cmd)
{
    if (s_coproc) i2c_master_transmit(s_coproc, &cmd, 1, 50);
}

/* Applies the requested buzzer state (I2C is kept out of the LVGL task). */
static void buzz_task(void *arg)
{
    (void)arg;
    bool is_on = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bool want = s_buzz_want;
        if (want != is_on) {
            coproc_send(want ? BOARD_BUZZER_ON_CMD : BOARD_BUZZER_OFF_CMD);
            is_on = want;
        }
    }
}

void board_periph_init(i2c_master_bus_handle_t bus)
{
    s_coproc = add_dev(bus, BOARD_COPROC_ADDR, "co-processor");
    s_rtc    = add_dev(bus, BOARD_RTC_ADDR, "RTC");
    coproc_send(BOARD_BUZZER_OFF_CMD);
    xTaskCreate(buzz_task, "buzzer", 2048, NULL, 5, &s_buzz_task);
}

void board_backlight_on(void) { coproc_send(BOARD_BL_ON_CMD); }

/* Co-processor scale: 0 = brightest ... 244 = dimmest (245 = off, never sent here). */
void board_set_brightness(uint8_t level)
{
    coproc_send((uint8_t)(244 - ((uint32_t)level * 244 + 127) / 255));
}

void board_buzzer(bool on)
{
    s_buzz_want = on;
    if (s_buzz_task) xTaskNotifyGive(s_buzz_task);
}

/* ---- PCF8563: time is stored as UTC. (TZ is never set, so mktime() works in UTC.) ---- */
static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

bool board_rtc_read(time_t *utc)
{
    if (!s_rtc) return false;
    uint8_t reg = 0x02, b[7];
    if (i2c_master_transmit_receive(s_rtc, &reg, 1, b, sizeof(b), 100) != ESP_OK) return false;
    if (b[0] & 0x80) return false;  /* VL flag: clock integrity lost */

    struct tm tm = {0};
    tm.tm_sec  = bcd2bin(b[0] & 0x7F);
    tm.tm_min  = bcd2bin(b[1] & 0x7F);
    tm.tm_hour = bcd2bin(b[2] & 0x3F);
    tm.tm_mday = bcd2bin(b[3] & 0x3F);
    tm.tm_mon  = bcd2bin(b[5] & 0x1F) - 1;
    tm.tm_year = bcd2bin(b[6]) + 100;
    if (tm.tm_mon < 0 || tm.tm_mon > 11 || tm.tm_mday < 1) return false;
    *utc = mktime(&tm);
    return true;
}

esp_err_t board_rtc_write(time_t utc)
{
    if (!s_rtc) return ESP_ERR_INVALID_STATE;
    struct tm tm;
    gmtime_r(&utc, &tm);
    uint8_t b[8] = {
        0x02,
        bin2bcd(tm.tm_sec), bin2bcd(tm.tm_min), bin2bcd(tm.tm_hour),
        bin2bcd(tm.tm_mday), (uint8_t)tm.tm_wday,
        bin2bcd(tm.tm_mon + 1), bin2bcd(tm.tm_year % 100),
    };
    return i2c_master_transmit(s_rtc, b, sizeof(b), 100);
}
