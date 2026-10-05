/* Setup login (Enter Passcode) and Change Passcode (old -> new). Passcode is stored in NVS. */
#include <string.h>
#include "ui.h"
#include "ui_keypad.h"
#include "settings.h"

#define PASS_MAX 8

/* ------------------------------------------------------------------ login */
static ui_keypad_t s_login;

static bool login_submit(const char *text, void *user)
{
    (void)user;
    if (strcmp(text, g_settings.passcode) == 0) {
        ui_go(UI_SCR_SETUP);
        return true;
    }
    ui_keypad_reset(&s_login, "", PASS_MAX);
    ui_keypad_message(&s_login, "Incorrect Passcode", true);
    return false;
}

static void login_save_cb(lv_event_t *e) { (void)e; ui_keypad_submit(&s_login); }

void ui_passcode_on_enter(void) { ui_keypad_reset(&s_login, "", PASS_MAX); }

lv_obj_t *ui_passcode_create(void)
{
    lv_obj_t *scr = ui_make_screen(true);
    ui_make_title(scr, "Enter Passcode", lv_color_hex(0x2C2C48));
    ui_keypad_create(&s_login, scr, 62, true, false);
    s_login.submit_cb = login_submit;

    lv_obj_t *cancel = ui_make_button(scr, "Cancel", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    ui_add_nav(cancel, UI_SCR_MAINT);

    lv_obj_t *save = ui_make_button(scr, "Save", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(save, login_save_cb, LV_EVENT_CLICKED, NULL);

    ui_passcode_on_enter();
    return scr;
}

/* ------------------------------------------------------------------ change passcode */
static ui_keypad_t s_ch;
static lv_obj_t   *s_ch_step, *s_ch_next_lbl;
static int         s_ch_stage;       /* 0 = old, 1 = new, 2 = done */
static lv_timer_t *s_done_timer;

static void set_stage(int stage)
{
    s_ch_stage = stage;
    lv_label_set_text(s_ch_step, stage == 0 ? "1. Enter Old Passcode" : "2. Enter New Passcode");
    lv_label_set_text(s_ch_next_lbl, stage == 0 ? "Next" : "Save");
    ui_keypad_reset(&s_ch, "", PASS_MAX);
}

static void done_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_done_timer = NULL;
    ui_go(UI_SCR_SETUP);
}

static bool ch_submit(const char *text, void *user)
{
    (void)user;
    if (s_ch_stage == 0) {
        if (strcmp(text, g_settings.passcode) != 0) {
            ui_keypad_reset(&s_ch, "", PASS_MAX);
            ui_keypad_message(&s_ch, "Incorrect Passcode", true);
            return false;
        }
        set_stage(1);
        return true;
    }
    if (s_ch_stage == 1) {
        if (strlen(text) < 1) {
            ui_keypad_message(&s_ch, "Enter a passcode", true);
            return false;
        }
        snprintf(g_settings.passcode, sizeof(g_settings.passcode), "%s", text);
        settings_save();
        s_ch_stage = 2;
        ui_keypad_message(&s_ch, "Passcode Changed", false);
        s_done_timer = lv_timer_create(done_timer_cb, 1200, NULL);
        lv_timer_set_repeat_count(s_done_timer, 1);
        return true;
    }
    return false;
}

static void ch_next_cb(lv_event_t *e) { (void)e; if (s_ch_stage < 2) ui_keypad_submit(&s_ch); }

static void ch_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (s_done_timer) { lv_timer_delete(s_done_timer); s_done_timer = NULL; }
    ui_go(UI_SCR_SETUP);
}

void ui_chpass_on_enter(void) { set_stage(0); }

lv_obj_t *ui_chpass_create(void)
{
    lv_obj_t *scr = ui_make_screen(true);
    lv_obj_t *t = ui_make_title(scr, "Change Passcode", lv_color_hex(0x2C2C48));
    lv_obj_set_style_text_font(t, UI_FONT_32, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 4);

    s_ch_step = lv_label_create(scr);
    lv_obj_set_style_text_font(s_ch_step, UI_FONT_28, 0);
    lv_obj_align(s_ch_step, LV_ALIGN_TOP_MID, 0, 44);

    ui_keypad_create(&s_ch, scr, 88, true, false);
    s_ch.submit_cb = ch_submit;

    lv_obj_t *cancel = ui_make_button(scr, "Cancel", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(cancel, ch_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *next = ui_make_button(scr, "Next", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(next, ch_next_cb, LV_EVENT_CLICKED, NULL);
    s_ch_next_lbl = lv_obj_get_child(next, 0);

    set_stage(0);
    return scr;
}
