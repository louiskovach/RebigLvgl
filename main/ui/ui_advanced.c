/* Advanced menu actions: Reboot, Save Parameters as Defaults, Adjust Display,
 * Select System Model, and the shared message screen. */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "settings.h"
#include "params.h"
#include "plc_link.h"
#include "board_periph.h"
#include "display_driver.h"
#include "esp_system.h"

/* ================================================================== message screen */
static lv_obj_t *s_msg_title, *s_msg_body, *s_msg_ok, *s_msg_cancel;
static ui_action_cb_t s_msg_on_ok;
static ui_screen_id_t s_msg_back;

static void msg_ok_cb(lv_event_t *e)
{
    (void)e;
    if (s_msg_on_ok) s_msg_on_ok();
    else             ui_go(s_msg_back);
}
static void msg_cancel_cb(lv_event_t *e) { (void)e; ui_go(s_msg_back); }

void ui_message_show(const char *title, const char *body, const char *ok_text, ui_action_cb_t on_ok,
                     const char *cancel_text, ui_screen_id_t back)
{
    s_msg_on_ok = on_ok;
    s_msg_back = back;
    lv_label_set_text(s_msg_title, title);
    ui_fit_label(s_msg_title, UI_W - 2 * UI_MARGIN);
    lv_label_set_text(s_msg_body, body ? body : "");

    if (ok_text) {
        lv_label_set_text(lv_obj_get_child(s_msg_ok, 0), ok_text);
        lv_obj_remove_flag(s_msg_ok, LV_OBJ_FLAG_HIDDEN);
        /* single button -> centred, with Cancel -> bottom right */
        if (cancel_text) lv_obj_align(s_msg_ok, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
        else             lv_obj_align(s_msg_ok, LV_ALIGN_BOTTOM_MID, 0, -UI_MARGIN);
    } else {
        lv_obj_add_flag(s_msg_ok, LV_OBJ_FLAG_HIDDEN);
    }
    if (cancel_text) {
        lv_label_set_text(lv_obj_get_child(s_msg_cancel, 0), cancel_text);
        lv_obj_remove_flag(s_msg_cancel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_msg_cancel, LV_OBJ_FLAG_HIDDEN);
    }
    ui_go(UI_SCR_MESSAGE);
}

lv_obj_t *ui_message_create(void)
{
    lv_obj_t *scr = ui_make_screen(false);
    s_msg_title = ui_make_title(scr, "", UI_COL_TEXT);

    s_msg_body = lv_label_create(scr);
    lv_obj_set_style_text_font(s_msg_body, UI_FONT_32, 0);
    lv_obj_set_width(s_msg_body, UI_W - 2 * UI_MARGIN);
    lv_obj_set_style_text_align(s_msg_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_msg_body, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_msg_body, LV_ALIGN_TOP_MID, 0, 110);

    s_msg_ok = ui_make_button(scr, "OK", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_add_event_cb(s_msg_ok, msg_ok_cb, LV_EVENT_CLICKED, NULL);
    s_msg_cancel = ui_make_button(scr, "Cancel", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(s_msg_cancel, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(s_msg_cancel, msg_cancel_cb, LV_EVENT_CLICKED, NULL);
    return scr;
}

/* ================================================================== reboot / defaults */
static void restart_cb(lv_timer_t *t) { (void)t; esp_restart(); }

static void do_reboot(void)
{
    ui_message_show("Rebooting", "Please wait...", NULL, NULL, NULL, UI_SCR_ADVANCED);
    lv_timer_t *t = lv_timer_create(restart_cb, 500, NULL);
    lv_timer_set_repeat_count(t, 1);
}

void ui_advanced_reboot(void)
{
    ui_message_show("Reboot System", "Restart the controller now?", "Reboot", do_reboot, "Cancel",
                    UI_SCR_ADVANCED);
}

void ui_advanced_defaults(void)
{
    params_defaults(g_settings.params);
    settings_save();
    plc_link_load_params();
    ui_message_show("Default Parameters Saved", "Press OK to Continue", "OK", NULL, NULL, UI_SCR_ADVANCED);
}

/* ================================================================== Adjust Display
 * Three cards (Brightness / Contrast / Saturation), each with a slider for big moves and
 * -/+ buttons for fine steps (hold to repeat). Changes preview live; Cancel undoes them. */
enum { D_BRIGHT, D_CONTRAST, D_SAT, D_COUNT };
static const char *const k_disp_names[D_COUNT] = { "Brightness", "Contrast", "Saturation" };
static const uint8_t      k_disp_default[D_COUNT] = { 255, 128, 128 };
static int       s_disp[D_COUNT];
static lv_obj_t *s_disp_val[D_COUNT], *s_disp_slider[D_COUNT];
static lv_style_t st_card, st_track, st_ind, st_knob;

static void disp_apply(void)
{
    board_set_brightness((uint8_t)s_disp[D_BRIGHT]);
    display_set_color_adjust((uint8_t)s_disp[D_CONTRAST], (uint8_t)s_disp[D_SAT]);
}

static void disp_show(int i, bool move_slider)
{
    lv_label_set_text_fmt(s_disp_val[i], "%d", s_disp[i]);
    if (move_slider) lv_slider_set_value(s_disp_slider[i], s_disp[i], LV_ANIM_OFF);
}

static void disp_set(int i, int v, bool move_slider)
{
    s_disp[i] = v < 0 ? 0 : (v > 255 ? 255 : v);
    disp_show(i, move_slider);
    disp_apply();
}

static void disp_step_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e);   /* row * 10 + (step + 1) */
    int row = code / 10, step = code % 10 - 1;
    disp_set(row, s_disp[row] + step, true);
}

static void disp_slider_cb(lv_event_t *e)
{
    int row = (int)(intptr_t)lv_event_get_user_data(e);
    disp_set(row, (int)lv_slider_get_value(lv_event_get_target(e)), false);
}

static void disp_reset_cb(lv_event_t *e)
{
    (void)e;
    for (int i = 0; i < D_COUNT; i++) { s_disp[i] = k_disp_default[i]; disp_show(i, true); }
    disp_apply();
}

static void disp_save_cb(lv_event_t *e)
{
    (void)e;
    g_settings.disp_brightness = (uint8_t)s_disp[D_BRIGHT];
    g_settings.disp_contrast   = (uint8_t)s_disp[D_CONTRAST];
    g_settings.disp_saturation = (uint8_t)s_disp[D_SAT];
    settings_save();
    ui_go(UI_SCR_ADVANCED);
}

static void disp_cancel_cb(lv_event_t *e)
{
    (void)e;
    board_set_brightness(g_settings.disp_brightness);        /* undo the preview */
    display_set_color_adjust(g_settings.disp_contrast, g_settings.disp_saturation);
    ui_go(UI_SCR_ADVANCED);
}

void ui_display_on_enter(void)
{
    s_disp[D_BRIGHT]   = g_settings.disp_brightness;
    s_disp[D_CONTRAST] = g_settings.disp_contrast;
    s_disp[D_SAT]      = g_settings.disp_saturation;
    for (int i = 0; i < D_COUNT; i++) disp_show(i, true);
}

static void disp_styles(void)
{
    lv_style_init(&st_card);
    lv_style_set_bg_opa(&st_card, LV_OPA_COVER);
    lv_style_set_bg_color(&st_card, lv_color_hex(0x161B8E));
    lv_style_set_border_width(&st_card, 2);
    lv_style_set_border_color(&st_card, lv_color_hex(0x8C94E0));
    lv_style_set_radius(&st_card, 14);

    lv_style_init(&st_track);
    lv_style_set_bg_opa(&st_track, LV_OPA_COVER);
    lv_style_set_bg_color(&st_track, lv_color_hex(0x0B0F66));
    lv_style_set_border_width(&st_track, 2);
    lv_style_set_border_color(&st_track, lv_color_hex(0x5A61C8));
    lv_style_set_radius(&st_track, LV_RADIUS_CIRCLE);

    lv_style_init(&st_ind);
    lv_style_set_bg_opa(&st_ind, LV_OPA_COVER);
    lv_style_set_bg_color(&st_ind, UI_COL_GREEN);
    lv_style_set_bg_grad_color(&st_ind, UI_COL_YELLOW);
    lv_style_set_bg_grad_dir(&st_ind, LV_GRAD_DIR_HOR);
    lv_style_set_radius(&st_ind, LV_RADIUS_CIRCLE);

    lv_style_init(&st_knob);
    lv_style_set_bg_opa(&st_knob, LV_OPA_COVER);
    lv_style_set_bg_color(&st_knob, UI_COL_YELLOW);
    lv_style_set_border_width(&st_knob, 3);
    lv_style_set_border_color(&st_knob, UI_COL_DARK);
    lv_style_set_outline_width(&st_knob, 3);
    lv_style_set_outline_color(&st_knob, lv_color_hex(0xD9DFF5));
    lv_style_set_radius(&st_knob, LV_RADIUS_CIRCLE);
    lv_style_set_pad_all(&st_knob, 10);
}

static void step_btn(lv_obj_t *card, const char *txt, int row, int step, int32_t x)
{
    lv_obj_t *b = ui_make_button(card, txt, UI_BTN_H, UI_BTN_H, UI_FONT_28);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, x, 0);
    void *code = (void *)(intptr_t)(row * 10 + step + 1);
    lv_obj_add_event_cb(b, disp_step_cb, LV_EVENT_CLICKED, code);
    lv_obj_add_event_cb(b, disp_step_cb, LV_EVENT_LONG_PRESSED_REPEAT, code);
}

lv_obj_t *ui_display_create(void)
{
    disp_styles();
    lv_obj_t *scr = ui_make_screen(false);
    ui_make_title(scr, "Adjust Display", UI_COL_TEXT);

    const int32_t card_w = UI_W - 2 * UI_MARGIN, card_h = 94;
    /* inside a card: name | - | slider | + | value */
    const int32_t name_w = 170, minus_x = 12 + name_w, val_w = 64;
    const int32_t val_x = card_w - 16 - val_w, plus_x = val_x - 12 - UI_BTN_H;
    const int32_t sl_x = minus_x + UI_BTN_H + 24, sl_w = plus_x - 24 - sl_x;

    for (int i = 0; i < D_COUNT; i++) {
        lv_obj_t *card = lv_obj_create(scr);
        lv_obj_remove_style_all(card);
        lv_obj_add_style(card, &st_card, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(card, card_w, card_h);
        lv_obj_set_pos(card, UI_MARGIN, 64 + i * (card_h + 12));

        lv_obj_t *name = lv_label_create(card);
        lv_label_set_text(name, k_disp_names[i]);
        lv_obj_set_style_text_font(name, UI_FONT_28, 0);
        lv_obj_set_style_text_color(name, UI_COL_TITLE_Y, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 16, 0);

        step_btn(card, LV_SYMBOL_MINUS, i, -1, minus_x);
        step_btn(card, LV_SYMBOL_PLUS, i, 1, plus_x);

        lv_obj_t *sl = lv_slider_create(card);
        lv_obj_remove_style_all(sl);
        lv_obj_add_style(sl, &st_track, LV_PART_MAIN);
        lv_obj_add_style(sl, &st_ind, LV_PART_INDICATOR);
        lv_obj_add_style(sl, &st_knob, LV_PART_KNOB);
        lv_slider_set_range(sl, 0, 255);
        lv_obj_set_size(sl, sl_w, 16);
        lv_obj_align(sl, LV_ALIGN_LEFT_MID, sl_x, 0);
        lv_obj_set_ext_click_area(sl, 24);
        lv_obj_add_event_cb(sl, disp_slider_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
        s_disp_slider[i] = sl;

        s_disp_val[i] = lv_label_create(card);
        lv_obj_set_style_text_font(s_disp_val[i], UI_FONT_32, 0);
        lv_obj_set_style_text_color(s_disp_val[i], UI_COL_TEXT, 0);
        lv_obj_set_width(s_disp_val[i], val_w);
        lv_obj_set_style_text_align(s_disp_val[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(s_disp_val[i], LV_ALIGN_LEFT_MID, val_x, 0);
    }

    lv_obj_t *cancel = ui_make_button(scr, "Cancel", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_add_event_cb(cancel, disp_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *reset = ui_make_button(scr, "Defaults", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_add_event_cb(reset, disp_reset_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save = ui_make_button(scr, "Save", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_add_event_cb(save, disp_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *const row[] = { cancel, reset, save };
    ui_spread_row(row, 3, UI_ROW_Y);

    ui_display_on_enter();
    return scr;
}

/* ================================================================== Select System Model */
/* min/max GpM = 0 means "not known yet" */
static const struct { const char *name; int min_gpm, max_gpm; } k_models[] = {
    { "BSG-20", 50, 260 },
    { "BSG-25", 0, 0 },
    { "BSG-30", 0, 0 },
    { "BSG-40", 0, 0 },
    { "BSG-50", 0, 0 },
    { "BSG-60", 0, 0 },
};
#define N_MODELS ((int)(sizeof(k_models) / sizeof(k_models[0])))

static lv_obj_t *s_model_desc, *s_model_rows[N_MODELS];
static int s_model_sel;

const char *ui_model_name(void)
{
    int m = g_settings.model < N_MODELS ? g_settings.model : 0;
    return k_models[m].name;
}

static void model_refresh(void)
{
    for (int i = 0; i < N_MODELS; i++) ui_check_set(s_model_rows[i], i == s_model_sel);
    if (k_models[s_model_sel].max_gpm)
        lv_label_set_text_fmt(s_model_desc, "The %s Model can handle a flow rate from %d - %d GpM.",
                              k_models[s_model_sel].name, k_models[s_model_sel].min_gpm,
                              k_models[s_model_sel].max_gpm);
    else
        lv_label_set_text_fmt(s_model_desc, "The %s Model.\n\n(Flow rate range not entered yet.)",
                              k_models[s_model_sel].name);
}

static void model_row_cb(lv_event_t *e)
{
    s_model_sel = (int)(intptr_t)lv_event_get_user_data(e);
    model_refresh();                   /* radio behaviour: re-check only the tapped one */
}

static void model_save_cb(lv_event_t *e)
{
    (void)e;
    g_settings.model = (uint8_t)s_model_sel;
    settings_save();
    ui_go(UI_SCR_ADVANCED);
}

void ui_model_on_enter(void)
{
    s_model_sel = g_settings.model < N_MODELS ? g_settings.model : 0;
    model_refresh();
}

static lv_obj_t *panel(lv_obj_t *parent, int32_t x, int32_t w)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(p, x, 64);
    lv_obj_set_size(p, w, 314);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(0xD8DAF0), 0);
    lv_obj_set_style_border_width(p, 2, 0);
    lv_obj_set_style_border_color(p, UI_COL_DARK, 0);
    lv_obj_set_style_pad_all(p, 12, 0);
    lv_obj_set_style_text_color(p, UI_COL_DARK, 0);
    return p;
}

lv_obj_t *ui_model_create(void)
{
    lv_obj_t *scr = ui_make_screen(true);
    ui_make_title(scr, "Select System Model", lv_color_hex(0x2C2C48));

    const int32_t w = (UI_W - 2 * UI_MARGIN - UI_GAP) / 2;
    lv_obj_t *left = panel(scr, UI_MARGIN, w);
    s_model_desc = lv_label_create(left);
    lv_obj_set_style_text_font(s_model_desc, UI_FONT_28, 0);
    lv_obj_set_width(s_model_desc, LV_PCT(100));
    lv_label_set_long_mode(s_model_desc, LV_LABEL_LONG_WRAP);

    lv_obj_t *right = panel(scr, UI_MARGIN + w + UI_GAP, w);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 4, 0);
    for (int i = 0; i < N_MODELS; i++) {
        lv_obj_t *c = ui_make_check(right, k_models[i].name);
        lv_obj_set_style_text_color(lv_obj_get_child(c, 1), UI_COL_DARK, 0);
        lv_obj_set_style_text_font(lv_obj_get_child(c, 1), UI_FONT_28, 0);
        lv_obj_set_style_border_color(lv_obj_get_child(c, 0), lv_color_hex(0x5A61C8), 0);
        lv_obj_add_event_cb(c, model_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_model_rows[i] = c;
    }

    lv_obj_t *cancel = ui_make_button(scr, "Cancel", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    ui_add_nav(cancel, UI_SCR_ADVANCED);
    lv_obj_t *save = ui_make_button(scr, "Save", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(save, model_save_cb, LV_EVENT_CLICKED, NULL);

    ui_model_on_enter();
    return scr;
}
