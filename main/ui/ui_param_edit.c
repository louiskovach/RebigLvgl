/* Numeric entry for one parameter (like the original "Low Pressure Cutoff" screen). */
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "ui_keypad.h"
#include "params.h"

static ui_keypad_t s_kp;
static lv_obj_t   *s_title, *s_range;
static int         s_id;

static bool submit(const char *text, void *user)
{
    (void)user;
    const param_def_t *d = param_def(s_id);
    char *end = NULL;
    float v = strtof(text, &end);
    if (!text[0] || (end && *end)) {
        ui_keypad_message(&s_kp, "Enter a number", true);
        return false;
    }
    if (v < d->min || v > d->max) {
        ui_keypad_message(&s_kp, "Out of range", true);
        return false;
    }
    param_set(s_id, v);
    ui_go(UI_SCR_PARAMS);
    return true;
}

static void save_cb(lv_event_t *e) { (void)e; ui_keypad_submit(&s_kp); }

void ui_param_edit_open(int param_id)
{
    s_id = param_id;
    ui_go(UI_SCR_PARAM_EDIT);
}

void ui_param_edit_on_enter(void)
{
    const param_def_t *d = param_def(s_id);
    lv_label_set_text(s_title, d->name);
    ui_fit_label(s_title, UI_W - 20);

    char cur[24], lo[16], hi[16];
    float v = param_get(s_id);
    if (d->zero_is_off && v == 0) cur[0] = '\0';
    else                          param_format(s_id, cur, sizeof(cur), false);
    snprintf(lo, sizeof(lo), "%g", (double)d->min);
    snprintf(hi, sizeof(hi), "%g", (double)d->max);
    lv_label_set_text_fmt(s_range, "Range %s - %s%s", lo, hi, d->zero_is_off ? "  (0 = OFF)" : "");

    ui_keypad_set_allow_dot(&s_kp, d->decimals);
    ui_keypad_reset(&s_kp, cur, 7);
}

lv_obj_t *ui_param_edit_create(void)
{
    lv_obj_t *scr = ui_make_screen(true);
    s_title = ui_make_title(scr, "", lv_color_hex(0x2C2C48));

    ui_keypad_create(&s_kp, scr, 62, false, true);
    s_kp.submit_cb = submit;

    s_range = lv_label_create(scr);
    lv_obj_set_style_text_font(s_range, UI_FONT_20, 0);
    lv_obj_set_width(s_range, 220);
    lv_label_set_long_mode(s_range, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_range, UI_MARGIN, 150);

    lv_obj_t *cancel = ui_make_button(scr, "Cancel", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    ui_add_nav(cancel, UI_SCR_PARAMS);

    lv_obj_t *save = ui_make_button(scr, "Save", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(save, save_cb, LV_EVENT_CLICKED, NULL);
    return scr;
}
