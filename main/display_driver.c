#include "display_driver.h"
#include "board_config.h"

#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "board_periph.h"
#include "params.h"

static const char *TAG = "display";

static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_panel_handle_t  s_panel;
static esp_lcd_touch_handle_t  s_touch;
static lv_display_t           *s_disp;
static lv_indev_t             *s_indev;

static esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c_bus);
}

static esp_err_t rgb_panel_init(void)
{
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = BOARD_LCD_PCLK_HZ,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_pulse_width = BOARD_LCD_HSYNC_PULSE,
            .hsync_back_porch = BOARD_LCD_HSYNC_BACK,
            .hsync_front_porch = BOARD_LCD_HSYNC_FRONT,
            .vsync_pulse_width = BOARD_LCD_VSYNC_PULSE,
            .vsync_back_porch = BOARD_LCD_VSYNC_BACK,
            .vsync_front_porch = BOARD_LCD_VSYNC_FRONT,
            .flags.pclk_active_neg = BOARD_LCD_PCLK_ACTIVE_NEG,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 1,                                   /* one FB in PSRAM, less PSRAM traffic */
        .bounce_buffer_size_px = BOARD_LCD_H_RES * 10,  /* SRAM bounce buffer for PSRAM FBs */
        .dma_burst_size = 64,
        .hsync_gpio_num = BOARD_LCD_PIN_HSYNC,
        .vsync_gpio_num = BOARD_LCD_PIN_VSYNC,
        .de_gpio_num = BOARD_LCD_PIN_DE,
        .pclk_gpio_num = BOARD_LCD_PIN_PCLK,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            BOARD_LCD_PIN_B0, BOARD_LCD_PIN_B1, BOARD_LCD_PIN_B2, BOARD_LCD_PIN_B3, BOARD_LCD_PIN_B4,
            BOARD_LCD_PIN_G0, BOARD_LCD_PIN_G1, BOARD_LCD_PIN_G2, BOARD_LCD_PIN_G3, BOARD_LCD_PIN_G4,
            BOARD_LCD_PIN_G5,
            BOARD_LCD_PIN_R0, BOARD_LCD_PIN_R1, BOARD_LCD_PIN_R2, BOARD_LCD_PIN_R3, BOARD_LCD_PIN_R4,
        },
        .flags.fb_in_psram = 1,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &s_panel), TAG, "rgb panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    return ESP_OK;
}

static esp_err_t lvgl_init(void)
{
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 8192;
    lvgl_cfg.task_affinity = 1;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl port");

    lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = s_panel,
        .buffer_size = BOARD_LCD_H_RES * 20,            /* 2 x 32 KB partial draw buffers in internal
                                                           RAM (40 lines = 128 KB left Wi-Fi short) */
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .swap_bytes = false,
            .direct_mode = false,
        },
    };
    lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = false,
        },
    };
    s_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    return s_disp ? ESP_OK : ESP_FAIL;
}

static esp_err_t touch_init(void)
{
    uint8_t addr;
    if (i2c_master_probe(s_i2c_bus, 0x5D, 50) == ESP_OK) {
        addr = 0x5D;
    } else if (i2c_master_probe(s_i2c_bus, 0x14, 50) == ESP_OK) {
        addr = 0x14;
    } else {
        ESP_LOGE(TAG, "GT911 not found at 0x5D or 0x14");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "GT911 at 0x%02X", addr);

    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = addr;
    io_cfg.scl_speed_hz = BOARD_I2C_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &tp_io), TAG, "touch io");

    esp_lcd_touch_io_gt911_config_t gt_cfg = { .dev_addr = addr };
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_TOUCH_RST,
        .int_gpio_num = BOARD_TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = {
            .swap_xy = BOARD_TOUCH_SWAP_XY,
            .mirror_x = BOARD_TOUCH_MIRROR_X,
            .mirror_y = BOARD_TOUCH_MIRROR_Y,
        },
        .driver_data = &gt_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch), TAG, "gt911");

    lvgl_port_touch_cfg_t touch_cfg = { .disp = s_disp, .handle = s_touch };
    s_indev = lvgl_port_add_touch(&touch_cfg);
    return s_indev ? ESP_OK : ESP_FAIL;
}

/* ---- "a frame has been drawn" flag (set from the LVGL task) ---- */
static volatile bool s_frame_done;

static void refr_ready_cb(lv_event_t *e)
{
    (void)e;
    s_frame_done = true;
}

void display_wait_next_frame(uint32_t timeout_ms)
{
    if (lvgl_port_lock(0)) {
        s_frame_done = false;
        lv_obj_invalidate(lv_screen_active());          /* make sure there is something to draw */
        lvgl_port_unlock();
    }
    for (uint32_t t = 0; t < timeout_ms && !s_frame_done; t += 10) vTaskDelay(pdMS_TO_TICKS(10));
}

/* ---- Buzzer sounds for as long as the screen is touched (parameter 24) ---- */
static void buzz_poll(lv_timer_t *t)
{
    static bool last;
    (void)t;
    bool on = s_indev && lv_indev_get_state(s_indev) == LV_INDEV_STATE_PRESSED && param_get(P_BEEP) != 0;
    if (on != last) {
        board_buzzer(on);
        last = on;
    }
}

/* ---- Software contrast / saturation, applied to each rendered area just before it is sent
 *      to the panel. 128 = unchanged for both. ---- */
static uint8_t s_con_lut[256];
static int32_t s_sat_k = 256;              /* 256 = 1.0 */
static bool    s_color_active;

void display_set_color_adjust(uint8_t contrast, uint8_t saturation)
{
    int32_t k = (int32_t)contrast * 256 / 128;          /* 0 .. ~2.0 */
    for (int v = 0; v < 256; v++) {
        int32_t o = ((v - 128) * k) / 256 + 128;
        s_con_lut[v] = (uint8_t)(o < 0 ? 0 : (o > 255 ? 255 : o));
    }
    s_sat_k = (int32_t)saturation * 256 / 128;
    s_color_active = (contrast != 128 || saturation != 128);
    lv_obj_invalidate(lv_screen_active());               /* redraw with the new look */
}

static inline int32_t clamp255(int32_t v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static void color_filter_cb(lv_event_t *e)
{
    if (!s_color_active) return;
    lv_display_t *disp = lv_event_get_target(e);
    const lv_area_t *area = lv_event_get_param(e);
    lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
    if (!area || !buf) return;

    uint16_t *px = (uint16_t *)buf->data;
    uint32_t n = lv_area_get_size(area);
    for (uint32_t i = 0; i < n; i++) {
        uint16_t c = px[i];
        int32_t r = ((c >> 11) & 0x1F), g = ((c >> 5) & 0x3F), b = (c & 0x1F);
        r = s_con_lut[(r << 3) | (r >> 2)];
        g = s_con_lut[(g << 2) | (g >> 4)];
        b = s_con_lut[(b << 3) | (b >> 2)];
        int32_t y = (r * 77 + g * 150 + b * 29) >> 8;
        r = clamp255(y + (((r - y) * s_sat_k) >> 8));
        g = clamp255(y + (((g - y) * s_sat_k) >> 8));
        b = clamp255(y + (((b - y) * s_sat_k) >> 8));
        px[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
}

esp_err_t display_init(void)
{
    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "i2c");
    vTaskDelay(pdMS_TO_TICKS(100));  /* let the board co-processor come up */
    board_periph_init(s_i2c_bus);

    ESP_RETURN_ON_ERROR(rgb_panel_init(), TAG, "panel");
    ESP_RETURN_ON_ERROR(lvgl_init(), TAG, "lvgl");
    /* Backlight stays OFF here; main.c turns it on once the first real screen is drawn.
     * Until then LVGL's empty default screen is black instead of the theme's white. */
    if (lvgl_port_lock(0)) {
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
        lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
        lvgl_port_unlock();
    }

    if (touch_init() != ESP_OK) {
        ESP_LOGW(TAG, "Continuing without touch");
    }
    if (lvgl_port_lock(0)) {
        lv_timer_create(buzz_poll, 20, NULL);
        lv_display_add_event_cb(s_disp, color_filter_cb, LV_EVENT_FLUSH_START, NULL);
        lv_display_add_event_cb(s_disp, refr_ready_cb, LV_EVENT_REFR_READY, NULL);
        lvgl_port_unlock();
    }
    return ESP_OK;
}
