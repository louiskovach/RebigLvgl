/* System Information screen (View Information -> Information). */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "settings.h"
#include "net_time.h"

const char *ui_model_name(void);   /* ui_advanced.c */
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_flash.h"
#include "esp_psram.h"

static char s_serial[32] = "N/A";
static char s_model[32]  = "N/A";
static time_t s_start_time;

static lv_obj_t *s_val_serial, *s_val_model, *s_val_start, *s_val_ip, *s_val_link;

static lv_obj_t *make_panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p, UI_COL_PANEL, 0);
    lv_obj_set_style_border_width(p, 2, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(0x7A7FB8), 0);
    lv_obj_set_style_text_color(p, UI_COL_DARK, 0);
    return p;
}

static lv_obj_t *kv(lv_obj_t *panel, const char *key, const char *val, int32_t y, int32_t val_x,
                    const lv_font_t *font)
{
    lv_obj_t *k = lv_label_create(panel);
    lv_label_set_text(k, key);
    lv_obj_set_style_text_font(k, font, 0);
    lv_obj_set_pos(k, 12, y);
    lv_obj_t *v = lv_label_create(panel);
    lv_label_set_text(v, val);
    lv_obj_set_style_text_font(v, font, 0);
    lv_obj_set_pos(v, val_x, y);
    return v;
}

void ui_sysinfo_set(const char *serial, const char *model)
{
    if (serial) snprintf(s_serial, sizeof(s_serial), "%s", serial);
    if (model)  snprintf(s_model, sizeof(s_model), "%s", model);
    if (s_val_serial) {
        lv_label_set_text(s_val_serial, s_serial);
        lv_label_set_text(s_val_model, s_model);
    }
}

void ui_info_on_enter(void)
{
    char buf[72];
    ui_format_datetime(buf, sizeof(buf), s_start_time, true);
    lv_label_set_text(s_val_start, buf);
    lv_label_set_text(s_val_ip, net_link_up() ? net_ip() : (g_settings.dhcp ? "" : g_settings.ip));
    lv_label_set_text(s_val_link, net_link_up() ? "UP (Wi-Fi)" : "DOWN");
    if (s_model[0] == 'N') lv_label_set_text(s_val_model, ui_model_name());   /* unless the PLC set one */
}

lv_obj_t *ui_info_create(void)
{
    s_start_time = time(NULL);

    lv_obj_t *scr = ui_make_screen(false);
    ui_make_title(scr, "Information", UI_COL_TEXT);

    /* top panel */
    lv_obj_t *top = make_panel(scr, UI_MARGIN, 58, UI_W - 2 * UI_MARGIN, 204);
    const int32_t vx = 230, p = 28;
    const esp_app_desc_t *app = esp_app_get_description();
    char build[40];
    snprintf(build, sizeof(build), "%s %s", app->date, app->time);

    s_val_serial = kv(top, "Serial Number:", s_serial, 6 + 0 * p, vx, UI_FONT_24);
    s_val_model  = kv(top, "Model:",         s_model,  6 + 1 * p, vx, UI_FONT_24);
    kv(top, "Build:",       build,                     6 + 2 * p, vx, UI_FONT_24);
    s_val_start  = kv(top, "Start Time:",    "",       6 + 3 * p, vx, UI_FONT_24);
    kv(top, "Application:", app->version,              6 + 4 * p, vx, UI_FONT_24);
    kv(top, "Loader:",      esp_get_idf_version(),     6 + 5 * p, vx, UI_FONT_24);
    kv(top, "16: --",       "17: --",                  6 + 6 * p, vx, UI_FONT_24);

    /* network panel */
    const int32_t by = 270, bh = 114, bw = (UI_W - 2 * UI_MARGIN - UI_GAP) / 2;
    lv_obj_t *net = make_panel(scr, UI_MARGIN, by, bw, bh);
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macs[24];
    snprintf(macs, sizeof(macs), "%02x-%02x-%02x-%02x-%02x-%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    kv(net, "Network", "", 4, 100, UI_FONT_24);
    s_val_ip = kv(net, "IP:", "", 34, 80, UI_FONT_20);
    s_val_link = kv(net, "Link:", "DOWN", 60, 80, UI_FONT_20);
    kv(net, "MAC:",  macs,   86, 80, UI_FONT_20);

    /* storage panel */
    lv_obj_t *sto = make_panel(scr, UI_MARGIN + bw + UI_GAP, by, bw, bh);
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    char fl[24], ps[24];
    snprintf(fl, sizeof(fl), "%u MB", (unsigned)(flash / (1024 * 1024)));
    snprintf(ps, sizeof(ps), "%u MB", (unsigned)(esp_psram_get_size() / (1024 * 1024)));
    kv(sto, "Storage:", "", 4, 100, UI_FONT_24);
    kv(sto, "Flash:", fl,   34, 110, UI_FONT_20);
    kv(sto, "PSRAM:", ps,   60, 110, UI_FONT_20);
    kv(sto, "NVS:",   "OK", 86, 110, UI_FONT_20);

    lv_obj_t *exit = ui_make_button(scr, "Exit", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(exit, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    ui_add_nav(exit, UI_SCR_VIEWINFO);

    ui_make_clock_bar(scr);
    return scr;
}
