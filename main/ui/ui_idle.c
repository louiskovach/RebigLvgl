/* "Idle" live-status screen: analog inputs on the left, outputs on the right.
 * Values are placeholders until the PLC link feeds ui_idle_set_analog()/ui_idle_set_output(). */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "sensors.h"

static const struct { const char *name; const char *raw_unit; const char *unit; float raw, value; int decimals; }
k_analog[] = {
    { "1. Influent:",   "V",  "PSI", 0.5f, 0.0f,   1 },
    { "2. Effluent:",   "V",  "PSI", 0.5f, 0.0f,   1 },
    { "3. Air:",        "V",  "PSI", 0.5f, 0.0f,   1 },
    { "4. Flow:",       "mA", "GpM", 4.0f, 0.0f,   1 },
    { "5. Difference:", NULL, "PSI", 0,    0.0f,   2 },
    { "6. Health:",     NULL, "%",   0,    100.0f, 1 },
};
#define N_ANALOG ((int)(sizeof(k_analog) / sizeof(k_analog[0])))

static const struct { const char *name; bool on; } k_outputs[] = {
    { "A. Main Pump Input:", true  }, { "B. Revival Control:", true  },
    { "C. Revival Valve:",   false }, { "D. Effluent Valve:",  false },
    { "E. Pump Control:",    false }, { "F. Pump Enable:",     true  },
    { "G. Filter Mode:",     false }, { "H. Fireman Protect:", false },
};
#define N_OUT ((int)(sizeof(k_outputs) / sizeof(k_outputs[0])))

static lv_obj_t *s_title;
static lv_obj_t *s_raw[N_ANALOG], *s_val[N_ANALOG], *s_out[N_OUT];

#define ROW_Y0    70
#define ROW_PITCH 34

static lv_obj_t *label_at(lv_obj_t *parent, const char *txt, int32_t x, int32_t y, bool right)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, UI_FONT_24, 0);
    if (right) {
        lv_obj_set_width(l, 110);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(l, x - 110, y);
    } else {
        lv_obj_set_pos(l, x, y);
    }
    return l;
}

void ui_idle_set_analog(int idx, float raw, float value)
{
    if (idx < 0 || idx >= N_ANALOG) return;
    char buf[48];
    if (k_analog[idx].raw_unit) {
        snprintf(buf, sizeof(buf), "%.2f %s", (double)raw, k_analog[idx].raw_unit);
        lv_label_set_text(s_raw[idx], buf);
    }
    snprintf(buf, sizeof(buf), "%.*f", k_analog[idx].decimals, (double)value);
    lv_label_set_text(s_val[idx], buf);
    if (idx == 5) ui_filtermode_set_health(value);
}

void ui_idle_set_output(int idx, bool on)
{
    if (idx < 0 || idx >= N_OUT) return;
    lv_obj_t *lbl = lv_obj_get_child(s_out[idx], 0);
    lv_label_set_text(lbl, on ? "ON" : "OFF");
    lv_obj_set_style_bg_color(s_out[idx], on ? UI_COL_GREEN : lv_color_hex(0x0B0F66), 0);
    lv_obj_set_style_text_color(lbl, on ? UI_COL_DARK : lv_color_hex(0x8F95D8), 0);
}

#define PILL_W   64
#define OUT_X    440                              /* left edge of the outputs column */
#define OUT_NAME_W (UI_W - UI_MARGIN - PILL_W - 10 - OUT_X)

/* ON/OFF pill, right-aligned on the row */
static lv_obj_t *make_pill(lv_obj_t *parent, int32_t right_x, int32_t y)
{
    const int32_t w = PILL_W, h = 28;
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(p, w, h);
    lv_obj_set_pos(p, right_x - w, y);
    lv_obj_set_style_radius(p, h / 2, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, UI_FONT_20, 0);
    lv_obj_center(l);
    return p;
}

static void set_unknown(int idx)
{
    if (k_analog[idx].raw_unit) lv_label_set_text_fmt(s_raw[idx], "-- %s", k_analog[idx].raw_unit);
    lv_label_set_text(s_val[idx], "--");
}

/* Analog rows 1-6: real readings when the sensors exist, "--" until then */
void ui_idle_refresh_sensors(void)
{
    if (!s_title) return;
    for (int i = 0; i < SENSOR_COUNT; i++) {
        if (sensor_present(i)) ui_idle_set_analog(i, sensor_raw(i), sensor_value(i));
        else                   set_unknown(i);
    }
    if (sensor_present(0) && sensor_present(1)) {
        float diff = sensor_value(0) - sensor_value(1);
        ui_idle_set_analog(4, 0, diff < 0 ? -diff : diff);
    } else {
        set_unknown(4);
    }
    float h = sensor_health();
    if (h >= 0) ui_idle_set_analog(5, 0, h);
    else        { set_unknown(5); ui_filtermode_set_health(-1); }
}

/* called by ui_set_system_status() */
void ui_idle_set_title(const char *status)
{
    if (!s_title) return;
    lv_label_set_text(s_title, strcmp(status, "Ready") == 0 ? "Idle" : status);
}

static void filter_cb(lv_event_t *e) { (void)e; ui_filter_start(); }
static void revive_cb(lv_event_t *e) { (void)e; ui_revive_start(); }

lv_obj_t *ui_idle_create(void)
{
    lv_obj_t *scr = ui_make_screen(false);
    s_title = ui_make_title(scr, "Idle", UI_COL_TEXT);

    for (int i = 0; i < N_ANALOG; i++) {
        int32_t y = ROW_Y0 + i * ROW_PITCH;
        label_at(scr, k_analog[i].name, UI_MARGIN, y, false);
        s_raw[i] = label_at(scr, "", 275, y, true);
        s_val[i] = label_at(scr, "", 372, y, true);
        label_at(scr, k_analog[i].unit, 380, y, false);
        ui_idle_set_analog(i, k_analog[i].raw, k_analog[i].value);
    }
    for (int i = 0; i < N_OUT; i++) {
        int32_t y = ROW_Y0 + i * ROW_PITCH;
        /* name must stop before the pill: size 24, or 20 if it wouldn't fit */
        lv_obj_t *nm = label_at(scr, k_outputs[i].name, OUT_X, y, false);
        lv_point_t sz;
        lv_text_get_size(&sz, k_outputs[i].name, UI_FONT_24, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x > OUT_NAME_W) {
            lv_obj_set_style_text_font(nm, UI_FONT_20, 0);
            lv_obj_set_y(nm, y + 2);
        }
        s_out[i] = make_pill(scr, UI_W - UI_MARGIN, y);
        ui_idle_set_output(i, k_outputs[i].on);
    }

    lv_obj_t *filter = ui_make_button(scr, "Filter", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_add_event_cb(filter, filter_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *revive = ui_make_button(scr, "Revive\nMedia", UI_BTN_W, UI_BTN_H, UI_FONT_20);
    lv_obj_add_event_cb(revive, revive_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *menu = ui_make_button(scr, "Menu", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    ui_add_nav(menu, UI_SCR_MAINT);

    lv_obj_t *bell = ui_make_bell_button(scr);

    /* on the info screen itself the "i" is drawn as a thin ring (not filled) */
    lv_obj_t *info = ui_make_info_button_outline(scr);
    ui_add_nav(info, UI_SCR_HOME);           /* back to the main page (Filter Mode while filtering) */

    lv_obj_t *const row[] = { filter, revive, menu, bell, info };
    ui_spread_row(row, 5, UI_ROW_Y_BAR);

    ui_make_status_bar(scr);
    return scr;
}
