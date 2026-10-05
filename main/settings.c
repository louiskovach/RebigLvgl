#include <string.h>
#include "settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <stdlib.h>

/* Never change the magic: new firmware copies whatever part of the saved blob it understands.
 * RULE: only ever ADD new fields at the END of settings_t (see settings.h). */
#define SETTINGS_MAGIC    0x46494C35u   /* v2: params[PARAM_SLOTS]          */
#define SETTINGS_MAGIC_V1 0x46494C34u   /* v1: params[33] (older firmware)  */

/* Layout written by older firmware (33 parameters), used only to migrate it. */
typedef struct {
    uint32_t  magic;
    char      passcode[9];
    int8_t    tz_hours;
    bool      dst, auto_net_time;
    uint8_t   date_fmt;
    bool      dhcp;
    char      ip[16], gateway[16], mask[16], dns[16];
    bool      web_enable;
    uint16_t  web_port;
    float     params[33];
    uint8_t   model;
    uint8_t   disp_brightness, disp_contrast, disp_saturation;
    scaling_t scaling[SENSOR_COUNT];
    uint8_t   smtp_detail;
} settings_v1_t;

static void migrate_v1(const uint8_t *buf, size_t len)
{
    settings_v1_t o;
    /* start from the current defaults so a short (even older) blob still ends up sane */
    memcpy(o.passcode, g_settings.passcode, sizeof(o.passcode));
    o.tz_hours = g_settings.tz_hours; o.dst = g_settings.dst; o.auto_net_time = g_settings.auto_net_time;
    o.date_fmt = g_settings.date_fmt; o.dhcp = g_settings.dhcp;
    memcpy(o.ip, g_settings.ip, 16); memcpy(o.gateway, g_settings.gateway, 16);
    memcpy(o.mask, g_settings.mask, 16); memcpy(o.dns, g_settings.dns, 16);
    o.web_enable = g_settings.web_enable; o.web_port = g_settings.web_port;
    memcpy(o.params, g_settings.params, sizeof(o.params));
    o.model = g_settings.model;
    o.disp_brightness = g_settings.disp_brightness; o.disp_contrast = g_settings.disp_contrast;
    o.disp_saturation = g_settings.disp_saturation;
    memcpy(o.scaling, g_settings.scaling, sizeof(o.scaling));
    o.smtp_detail = g_settings.smtp_detail;

    memcpy(&o, buf, len < sizeof(o) ? len : sizeof(o));

    memcpy(g_settings.passcode, o.passcode, sizeof(o.passcode));
    g_settings.tz_hours = o.tz_hours; g_settings.dst = o.dst; g_settings.auto_net_time = o.auto_net_time;
    g_settings.date_fmt = o.date_fmt; g_settings.dhcp = o.dhcp;
    memcpy(g_settings.ip, o.ip, 16); memcpy(g_settings.gateway, o.gateway, 16);
    memcpy(g_settings.mask, o.mask, 16); memcpy(g_settings.dns, o.dns, 16);
    g_settings.web_enable = o.web_enable; g_settings.web_port = o.web_port;
    memcpy(g_settings.params, o.params, sizeof(o.params));        /* new params keep defaults */
    g_settings.model = o.model;
    g_settings.disp_brightness = o.disp_brightness; g_settings.disp_contrast = o.disp_contrast;
    g_settings.disp_saturation = o.disp_saturation;
    memcpy(g_settings.scaling, o.scaling, sizeof(o.scaling));
    g_settings.smtp_detail = o.smtp_detail;
}

static const char *TAG = "settings";
settings_t g_settings;

static void set_defaults(void)
{
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.magic = SETTINGS_MAGIC;
    strcpy(g_settings.passcode, "14789");
    g_settings.tz_hours = -5;
    g_settings.dst = true;
    g_settings.auto_net_time = true;
    g_settings.date_fmt = DATEFMT_MDY24;
    g_settings.dhcp = true;
    g_settings.web_enable = true;
    g_settings.web_port = 80;
    params_defaults(g_settings.params);
    g_settings.rev = 1;
    g_settings.model = 0;
    g_settings.disp_brightness = 255;
    g_settings.disp_contrast = 128;
    g_settings.disp_saturation = 128;
    settings_default_scaling(g_settings.scaling);
}

void settings_default_scaling(scaling_t *sc)
{
    for (int i = 0; i < 3; i++) sc[i] = (scaling_t){ 0.5f, 0.0f, 4.5f, 100.0f };  /* pressure: 0.5-4.5 V -> 0-100 PSI */
    sc[3] = (scaling_t){ 4.0f, 0.0f, 20.0f, 100.0f };                             /* flow: 4-20 mA -> 0-100 GpM */
}

void settings_load(void)
{
    set_defaults();                                   /* anything not in flash keeps its default */

    nvs_handle_t h;
    if (nvs_open("hmi", NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No saved settings - using defaults");
        return;
    }
    size_t len = 0;
    if (nvs_get_blob(h, "cfg", NULL, &len) == ESP_OK && len >= sizeof(uint32_t)) {
        uint8_t *buf = malloc(len);
        uint32_t magic = 0;
        if (buf && nvs_get_blob(h, "cfg", buf, &len) == ESP_OK) memcpy(&magic, buf, sizeof(magic));
        if (magic == SETTINGS_MAGIC) {
            memcpy(&g_settings, buf, len < sizeof(g_settings) ? len : sizeof(g_settings));
            ESP_LOGI(TAG, "Settings loaded (%u bytes)", (unsigned)len);
            if (g_settings.rev < 1) {                     /* param 34 was "Skip" (inverted) */
                g_settings.params[P_SHOW_STARTUP] = 1;
                g_settings.rev = 1;
                free(buf);
                nvs_close(h);
                settings_save();
                return;
            }
        } else if (magic == SETTINGS_MAGIC_V1) {
            migrate_v1(buf, len);
            g_settings.magic = SETTINGS_MAGIC;
            g_settings.rev = 1;
            ESP_LOGI(TAG, "Settings migrated from older firmware");
            free(buf);
            nvs_close(h);
            settings_save();
            return;
        } else {
            ESP_LOGW(TAG, "Saved settings not recognised - using defaults");
        }
        free(buf);
    }
    nvs_close(h);
}

void settings_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("hmi", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        g_settings.magic = SETTINGS_MAGIC;
        err = nvs_set_blob(h, "cfg", &g_settings, sizeof(g_settings));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "Saving settings failed: %s", esp_err_to_name(err));
}

int32_t settings_utc_offset_s(void)
{
    return (int32_t)g_settings.tz_hours * 3600 + (g_settings.dst ? 3600 : 0);
}

const char *settings_date_fmt_name(uint8_t fmt)
{
    static const char *names[DATEFMT_COUNT] = {
        "M-D-Y (24)", "M-D-Y (12)", "D-M-Y (24)", "D-M-Y (12)", "Y-M-D (24)",
    };
    return fmt < DATEFMT_COUNT ? names[fmt] : names[0];
}
