/*
 * Analog Scaling (list) -> "<Input> Application Scaling" (calibrate one input)
 *   -> keypad for Low/High raw value (with Acquire) or Low/High engineering value.
 */
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "ui_keypad.h"
#include "sensors.h"

/* LVGL's own printf has no float support -> format with libc first. */
static void set_f(lv_obj_t *l, const char *fmt, double v, const char *unit)
{
    char buf[32];
    if (unit) snprintf(buf, sizeof(buf), fmt, v, unit);
    else      snprintf(buf, sizeof(buf), fmt, v);
    lv_label_set_text(l, buf);
}

/* ================================================================== list */
static lv_obj_t *s_list_scr, *s_in[SENSOR_COUNT], *s_out[SENSOR_COUNT];
static ui_screen_id_t s_list_return = UI_SCR_ADVANCED;
static int s_cal_idx;

static void open_cal_cb(lv_event_t *e)
{
    s_cal_idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_go(UI_SCR_CALIBRATE);
}

static void revert_cb(lv_event_t *e)
{
    (void)e;
    settings_default_scaling(g_settings.scaling);
    settings_save();
    ui_analog_refresh();
}

static void list_exit_cb(lv_event_t *e) { (void)e; ui_go(s_list_return); }

void ui_analog_on_enter(void)
{
    ui_screen_id_t from = ui_caller(UI_SCR_ANALOG);
    if (from == UI_SCR_ADVANCED || from == UI_SCR_DIAG) s_list_return = from;
}

static lv_obj_t *num_label(lv_obj_t *parent, int32_t right_x, int32_t y, int32_t w, const lv_font_t *f)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(l, right_x - w, y);
    return l;
}

#define COL_IN_R  440
#define COL_OUT_R 580
#define ROW_Y0    68
#define ROW_PITCH (UI_BTN_H + UI_GAP)

lv_obj_t *ui_analog_create(void)
{
    s_list_scr = ui_make_screen(false);
    ui_make_title_bar(s_list_scr, "Analog Scaling");

    for (int i = 0; i < SENSOR_COUNT; i++) {
        int32_t y = ROW_Y0 + i * ROW_PITCH;
        lv_obj_t *n = lv_label_create(s_list_scr);
        lv_label_set_text_fmt(n, "%d.%s", i + 1, sensor_name(i));
        lv_obj_set_style_text_font(n, UI_FONT_32, 0);
        lv_obj_set_pos(n, UI_MARGIN, y + 14);
        s_in[i]  = num_label(s_list_scr, COL_IN_R,  y + 14, 140, UI_FONT_32);
        s_out[i] = num_label(s_list_scr, COL_OUT_R, y + 14, 130, UI_FONT_32);

        lv_obj_t *b = ui_make_button(s_list_scr, "Calibrate", UI_BTN_W, UI_BTN_H, UI_FONT_28);
        lv_obj_set_pos(b, UI_W - UI_MARGIN - UI_BTN_W, y);
        lv_obj_add_event_cb(b, open_cal_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    lv_obj_t *exit = ui_make_button(s_list_scr, "Exit", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(exit, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(exit, list_exit_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *in = num_label(s_list_scr, COL_IN_R, UI_H - UI_MARGIN - 50, 140, UI_FONT_32);
    lv_label_set_text(in, "In");
    lv_obj_t *out = num_label(s_list_scr, COL_OUT_R, UI_H - UI_MARGIN - 50, 130, UI_FONT_32);
    lv_label_set_text(out, "Out");

    lv_obj_t *revert = ui_make_button(s_list_scr, "Revert", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(revert, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(revert, revert_cb, LV_EVENT_CLICKED, NULL);
    return s_list_scr;
}

/* ================================================================== calibrate one input */
enum { F_LO_RAW, F_LO_VAL, F_HI_RAW, F_HI_VAL, F_COUNT };

static lv_obj_t  *s_cal_scr, *s_cal_title, *s_cur_raw, *s_cur_val, *s_cal_err;
static lv_obj_t  *s_fields[F_COUNT];
static scaling_t  s_work;              /* edited copy, saved on Save */
static int        s_edit_field;

static float *work_field(int f)
{
    switch (f) {
    case F_LO_RAW: return &s_work.lo_raw;
    case F_LO_VAL: return &s_work.lo_val;
    case F_HI_RAW: return &s_work.hi_raw;
    default:       return &s_work.hi_val;
    }
}

static void cal_fields_refresh(void)
{
    const char *ru = sensor_raw_unit(s_cal_idx);
    set_f(s_fields[F_LO_RAW], "%.2f %s", s_work.lo_raw, ru);
    set_f(s_fields[F_HI_RAW], "%.2f %s", s_work.hi_raw, ru);
    set_f(s_fields[F_LO_VAL], "%.1f", s_work.lo_val, NULL);
    set_f(s_fields[F_HI_VAL], "%.1f", s_work.hi_val, NULL);
}

void ui_analog_refresh(void)
{
    lv_obj_t *act = lv_screen_active();
    if (s_list_scr && act == s_list_scr) {
        for (int i = 0; i < SENSOR_COUNT; i++) {
            if (sensor_present(i)) {
                set_f(s_in[i],  "%.2f", sensor_raw(i), NULL);
                set_f(s_out[i], "%.1f", sensor_value(i), NULL);
            } else {
                lv_label_set_text(s_in[i], "--");
                lv_label_set_text(s_out[i], "--");
            }
        }
    } else if (s_cal_scr && act == s_cal_scr) {
        if (!sensor_present(s_cal_idx)) {
            lv_label_set_text_fmt(s_cur_raw, "-- %s", sensor_raw_unit(s_cal_idx));
            lv_label_set_text_fmt(s_cur_val, "-- %s", sensor_unit(s_cal_idx));
            return;
        }
        float raw = sensor_raw(s_cal_idx);
        set_f(s_cur_raw, "%.2f %s", raw, sensor_raw_unit(s_cal_idx));
        set_f(s_cur_val, "%.1f %s", sensor_scale(raw, &s_work), sensor_unit(s_cal_idx));
    }
}

static void edit_cb(lv_event_t *e)
{
    s_edit_field = (int)(intptr_t)lv_event_get_user_data(e);
    ui_go(UI_SCR_CAL_EDIT);
}

static void cal_save_cb(lv_event_t *e)
{
    (void)e;
    if (s_work.hi_raw == s_work.lo_raw) {
        lv_label_set_text(s_cal_err, "Low and High points must be different");
        return;
    }
    g_settings.scaling[s_cal_idx] = s_work;
    settings_save();
    ui_go(UI_SCR_ANALOG);
}

void ui_calibrate_on_enter(void)
{
    lv_label_set_text(s_cal_err, "");
    if (ui_caller(UI_SCR_CALIBRATE) != UI_SCR_CAL_EDIT) s_work = g_settings.scaling[s_cal_idx];
    lv_label_set_text_fmt(s_cal_title, "%s Application Scaling", sensor_name(s_cal_idx));
    cal_fields_refresh();
    ui_analog_refresh();
}

static lv_obj_t *side_label(lv_obj_t *scr, const char *txt, int32_t y)
{
    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, UI_FONT_32, 0);
    lv_obj_set_pos(l, UI_MARGIN, y);
    return l;
}

static void pencil(lv_obj_t *scr, int field, int32_t x, int32_t y)
{
    lv_obj_t *b = ui_make_button(scr, LV_SYMBOL_EDIT, UI_BTN_H, UI_BTN_H, UI_FONT_28);
    lv_obj_set_pos(b, x, y);
    lv_obj_add_event_cb(b, edit_cb, LV_EVENT_CLICKED, (void *)(intptr_t)field);
}

lv_obj_t *ui_calibrate_create(void)
{
    s_cal_scr = ui_make_screen(false);
    s_cal_title = ui_make_title_bar(s_cal_scr, "");

    /* columns: raw value right edge | pencil | eng value right edge | pencil */
    const int32_t pen2_x = UI_W - UI_MARGIN - UI_BTN_H;
    const int32_t val_r  = pen2_x - UI_GAP;
    const int32_t pen1_x = 470;
    const int32_t raw_r  = pen1_x - UI_GAP;
    const int32_t y_cur = 84, y_lo = 170, y_hi = 170 + UI_BTN_H + UI_GAP;

    side_label(s_cal_scr, "Current:", y_cur);
    s_cur_raw = num_label(s_cal_scr, raw_r, y_cur, 180, UI_FONT_32);
    s_cur_val = num_label(s_cal_scr, UI_W - UI_MARGIN, y_cur, 220, UI_FONT_32);

    side_label(s_cal_scr, "Low Point:", y_lo + 14);
    s_fields[F_LO_RAW] = num_label(s_cal_scr, raw_r, y_lo + 14, 180, UI_FONT_32);
    pencil(s_cal_scr, F_LO_RAW, pen1_x, y_lo);
    s_fields[F_LO_VAL] = num_label(s_cal_scr, val_r, y_lo + 14, 160, UI_FONT_32);
    pencil(s_cal_scr, F_LO_VAL, pen2_x, y_lo);

    side_label(s_cal_scr, "High Point:", y_hi + 14);
    s_fields[F_HI_RAW] = num_label(s_cal_scr, raw_r, y_hi + 14, 180, UI_FONT_32);
    pencil(s_cal_scr, F_HI_RAW, pen1_x, y_hi);
    s_fields[F_HI_VAL] = num_label(s_cal_scr, val_r, y_hi + 14, 160, UI_FONT_32);
    pencil(s_cal_scr, F_HI_VAL, pen2_x, y_hi);

    s_cal_err = lv_label_create(s_cal_scr);
    lv_obj_set_style_text_font(s_cal_err, UI_FONT_24, 0);
    lv_obj_set_style_text_color(s_cal_err, UI_COL_YELLOW, 0);
    lv_obj_align(s_cal_err, LV_ALIGN_TOP_MID, 0, y_hi + UI_BTN_H + 16);

    lv_obj_t *cancel = ui_make_button(s_cal_scr, "Cancel", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    ui_add_nav(cancel, UI_SCR_ANALOG);
    lv_obj_t *save = ui_make_button(s_cal_scr, "Save", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(save, cal_save_cb, LV_EVENT_CLICKED, NULL);
    return s_cal_scr;
}

/* ================================================================== keypad for one value */
static ui_keypad_t s_kp;
static lv_obj_t   *s_kp_title, *s_acquire;

static bool is_raw_field(int f) { return f == F_LO_RAW || f == F_HI_RAW; }

static bool kp_submit(const char *text, void *user)
{
    (void)user;
    char *end = NULL;
    float v = strtof(text, &end);
    if (!text[0] || (end && *end)) {
        ui_keypad_message(&s_kp, "Enter a number", true);
        return false;
    }
    *work_field(s_edit_field) = v;
    ui_go(UI_SCR_CALIBRATE);
    return true;
}

static void kp_save_cb(lv_event_t *e) { (void)e; ui_keypad_submit(&s_kp); }

static void acquire_cb(lv_event_t *e)
{
    (void)e;
    if (!sensor_present(s_cal_idx)) {
        ui_keypad_message(&s_kp, "No sensor reading", true);
        return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", (double)sensor_raw(s_cal_idx));
    ui_keypad_reset(&s_kp, buf, 7);
}

void ui_cal_edit_on_enter(void)
{
    bool raw = is_raw_field(s_edit_field);
    bool low = (s_edit_field == F_LO_RAW || s_edit_field == F_LO_VAL);
    if (raw) lv_label_set_text_fmt(s_kp_title, "%s %s Value", low ? "Low" : "High",
                                   s_cal_idx == 3 ? "Current (mA)" : "Voltage");
    else     lv_label_set_text_fmt(s_kp_title, "%s Point Value (%s)", low ? "Low" : "High", sensor_unit(s_cal_idx));
    ui_fit_label(s_kp_title, UI_W - 2 * UI_MARGIN);

    char buf[16];
    snprintf(buf, sizeof(buf), raw ? "%.2f" : "%.1f", (double)*work_field(s_edit_field));
    ui_keypad_reset(&s_kp, buf, 7);

    if (raw) lv_obj_remove_flag(s_acquire, LV_OBJ_FLAG_HIDDEN);
    else     lv_obj_add_flag(s_acquire, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *ui_cal_edit_create(void)
{
    lv_obj_t *scr = ui_make_screen(true);
    s_kp_title = ui_make_title(scr, "", lv_color_hex(0x2C2C48));
    ui_keypad_create(&s_kp, scr, 62, false, true);
    s_kp.submit_cb = kp_submit;

    lv_obj_t *cancel = ui_make_button(scr, "Cancel", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    ui_add_nav(cancel, UI_SCR_CALIBRATE);

    lv_obj_t *save = ui_make_button(scr, "Save", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(save, kp_save_cb, LV_EVENT_CLICKED, NULL);

    s_acquire = ui_make_button(scr, "Acquire", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(s_acquire, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -(UI_MARGIN + UI_BTN_H + UI_GAP));
    lv_obj_add_event_cb(s_acquire, acquire_cb, LV_EVENT_CLICKED, NULL);
    return scr;
}
