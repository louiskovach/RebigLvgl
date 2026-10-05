#include <string.h>
#include "ui_keypad.h"

#define KEY   UI_BTN_H
#define GAP   8
#define PAD   10
#define BOX_H 56

static void refresh(ui_keypad_t *kp)
{
    if (kp->masked) {
        char tmp[sizeof(kp->buf) * 2];
        size_t n = strlen(kp->buf);
        for (size_t i = 0; i < n; i++) { tmp[i * 2] = '*'; tmp[i * 2 + 1] = ' '; }
        tmp[n * 2] = '\0';
        lv_label_set_text(kp->text, tmp);
    } else {
        lv_label_set_text(kp->text, kp->buf);
    }
}

static void hide_msg(ui_keypad_t *kp)
{
    if (kp->msg_timer) { lv_timer_delete(kp->msg_timer); kp->msg_timer = NULL; }
    lv_obj_add_flag(kp->msg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(kp->text, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(kp->cursor, LV_OBJ_FLAG_HIDDEN);
}

static void msg_timer_cb(lv_timer_t *t)
{
    ui_keypad_t *kp = lv_timer_get_user_data(t);
    kp->msg_timer = NULL;           /* one-shot timer deletes itself */
    lv_obj_add_flag(kp->msg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(kp->text, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(kp->cursor, LV_OBJ_FLAG_HIDDEN);
}

static void shake_cb(void *obj, int32_t v)
{
    int32_t amp = 14 * (1080 - v) / 1080;
    lv_obj_set_style_translate_x(obj, lv_trigo_sin((int16_t)(v % 360)) * amp / 32767, 0);
}

void ui_keypad_message(ui_keypad_t *kp, const char *msg, bool error)
{
    if (kp->msg_timer) { lv_timer_delete(kp->msg_timer); kp->msg_timer = NULL; }
    lv_label_set_text(kp->msg, msg);
    lv_obj_set_style_text_color(kp->msg, error ? UI_COL_RED : lv_color_hex(0x1A8A30), 0);
    lv_obj_add_flag(kp->text, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(kp->cursor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(kp->msg, LV_OBJ_FLAG_HIDDEN);

    if (error) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, kp->box);
        lv_anim_set_exec_cb(&a, shake_cb);
        lv_anim_set_values(&a, 0, 1080);
        lv_anim_set_duration(&a, 450);
        lv_anim_start(&a);
    }
    kp->msg_timer = lv_timer_create(msg_timer_cb, 1500, kp);
    lv_timer_set_repeat_count(kp->msg_timer, 1);
}

void ui_keypad_submit(ui_keypad_t *kp)
{
    hide_msg(kp);
    if (kp->submit_cb) kp->submit_cb(kp->buf, kp->user);
}

static void key_cb(lv_event_t *e)
{
    ui_keypad_t *kp = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_current_target(e);
    char k = (char)(intptr_t)lv_obj_get_user_data(btn);

    hide_msg(kp);
    size_t n = strlen(kp->buf);
    if (k == '.' && (!kp->allow_dot || strchr(kp->buf, '.'))) return;
    if ((k >= '0' && k <= '9') || k == '.') {
        if (n < kp->max_len) { kp->buf[n] = k; kp->buf[n + 1] = '\0'; }
    } else if (k == 'B') {
        if (n) kp->buf[n - 1] = '\0';
    } else if (k == 'C') {
        kp->buf[0] = '\0';
    } else if (k == 'E') {
        ui_keypad_submit(kp);
        return;
    }
    refresh(kp);
}

static void blink_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa(obj, v < 500 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

void ui_keypad_reset(ui_keypad_t *kp, const char *initial, uint8_t max_len)
{
    kp->max_len = max_len < sizeof(kp->buf) - 1 ? max_len : sizeof(kp->buf) - 1;
    snprintf(kp->buf, sizeof(kp->buf), "%s", initial ? initial : "");
    hide_msg(kp);
    refresh(kp);
}

void ui_keypad_create(ui_keypad_t *kp, lv_obj_t *parent, int32_t y, bool masked, bool allow_dot)
{
    memset(kp, 0, sizeof(*kp));
    kp->masked = masked;
    kp->allow_dot = allow_dot;
    kp->max_len = 8;
    const int32_t x = (UI_W - UI_KEYPAD_W) / 2;

    /* entry box */
    kp->box = lv_obj_create(parent);
    lv_obj_remove_style_all(kp->box);
    lv_obj_remove_flag(kp->box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(kp->box, x, y);
    lv_obj_set_size(kp->box, UI_KEYPAD_W, BOX_H);
    lv_obj_set_style_bg_opa(kp->box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(kp->box, lv_color_hex(0xDADDF2), 0);
    lv_obj_set_style_border_width(kp->box, 2, 0);
    lv_obj_set_style_border_color(kp->box, lv_color_hex(0x202040), 0);
    lv_obj_set_style_pad_left(kp->box, 14, 0);
    lv_obj_set_flex_flow(kp->box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(kp->box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    kp->text = lv_label_create(kp->box);
    lv_obj_set_style_text_font(kp->text, UI_FONT_32, 0);
    lv_obj_set_style_text_color(kp->text, lv_color_hex(0x202040), 0);

    kp->cursor = lv_label_create(kp->box);
    lv_label_set_text(kp->cursor, "_");
    lv_obj_set_style_text_font(kp->cursor, UI_FONT_32, 0);
    lv_obj_set_style_text_color(kp->cursor, UI_COL_RED, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, kp->cursor);
    lv_anim_set_exec_cb(&a, blink_cb);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    kp->msg = lv_label_create(kp->box);
    lv_obj_set_style_text_font(kp->msg, UI_FONT_24, 0);
    lv_obj_add_flag(kp->msg, LV_OBJ_FLAG_HIDDEN);

    /* key panel */
    lv_obj_t *pad = lv_obj_create(parent);
    lv_obj_remove_style_all(pad);
    lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(pad, x, y + BOX_H + 6);
    lv_obj_set_size(pad, UI_KEYPAD_W, 4 * KEY + 3 * GAP + 2 * PAD);
    lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pad, lv_color_hex(0x1428A8), 0);
    lv_obj_set_style_border_width(pad, 2, 0);
    lv_obj_set_style_border_color(pad, UI_COL_DARK, 0);

    static const struct { const char *txt; char code; uint8_t col, row, rows; } keys[] = {
        { "7", '7', 0, 0, 1 }, { "8", '8', 1, 0, 1 }, { "9", '9', 2, 0, 1 }, { LV_SYMBOL_BACKSPACE, 'B', 3, 0, 1 },
        { "4", '4', 0, 1, 1 }, { "5", '5', 1, 1, 1 }, { "6", '6', 2, 1, 1 },
        { "1", '1', 0, 2, 1 }, { "2", '2', 1, 2, 1 }, { "3", '3', 2, 2, 1 }, { LV_SYMBOL_NEW_LINE, 'E', 3, 2, 2 },
        { "C", 'C', 0, 3, 1 }, { "0", '0', 1, 3, 1 }, { ".", '.', 2, 3, 1 },
    };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (keys[i].code == '.' && !allow_dot) continue;
        int32_t h = keys[i].rows * KEY + (keys[i].rows - 1) * GAP;
        lv_obj_t *b = ui_make_button(pad, keys[i].txt, KEY, h, UI_FONT_36);
        lv_obj_set_pos(b, PAD + keys[i].col * (KEY + GAP), PAD + keys[i].row * (KEY + GAP));
        lv_obj_set_user_data(b, (void *)(intptr_t)keys[i].code);
        if (keys[i].code == '.') kp->dot_btn = b;
        lv_obj_add_event_cb(b, key_cb, LV_EVENT_CLICKED, kp);
    }
    refresh(kp);
}

void ui_keypad_set_allow_dot(ui_keypad_t *kp, bool allow)
{
    kp->allow_dot = allow && kp->dot_btn;
    if (!kp->dot_btn) return;
    if (allow) lv_obj_remove_flag(kp->dot_btn, LV_OBJ_FLAG_HIDDEN);
    else       lv_obj_add_flag(kp->dot_btn, LV_OBJ_FLAG_HIDDEN);
}

/* =================================================================== popup */
static bool popup_submit(const char *text, void *user)
{
    ui_popup_t *p = user;
    if (p->ok_cb && !p->ok_cb(text, p->user)) return false;  /* callback shows its own error */
    ui_popup_close(p);
    return true;
}

static void popup_ok_cb(lv_event_t *e)     { ui_popup_t *p = lv_event_get_user_data(e); ui_keypad_submit(&p->kp); }
static void popup_cancel_cb(lv_event_t *e) { ui_popup_close(lv_event_get_user_data(e)); }

void ui_popup_create(ui_popup_t *p, lv_obj_t *scr, bool allow_dot)
{
    memset(p, 0, sizeof(*p));
    p->root = lv_obj_create(scr);
    lv_obj_remove_style_all(p->root);
    lv_obj_set_size(p->root, UI_W, UI_H);
    lv_obj_set_style_bg_opa(p->root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p->root, UI_COL_BG_LAV, 0);
    lv_obj_remove_flag(p->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p->root, LV_OBJ_FLAG_CLICKABLE);   /* swallow touches meant for the screen below */

    p->title = ui_make_title(p->root, "", lv_color_hex(0x2C2C48));
    ui_keypad_create(&p->kp, p->root, 64, false, allow_dot);
    p->kp.submit_cb = popup_submit;
    p->kp.user = p;

    lv_obj_t *cancel = ui_make_button(p->root, "Cancel", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(cancel, popup_cancel_cb, LV_EVENT_CLICKED, p);

    lv_obj_t *ok = ui_make_button(p->root, "OK", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(ok, popup_ok_cb, LV_EVENT_CLICKED, p);

    lv_obj_add_flag(p->root, LV_OBJ_FLAG_HIDDEN);
}

void ui_popup_open(ui_popup_t *p, const char *title, const char *initial, uint8_t max_len,
                   ui_kp_submit_cb_t ok_cb, void *user)
{
    p->ok_cb = ok_cb;
    p->user = user;
    lv_label_set_text(p->title, title);
    ui_keypad_reset(&p->kp, initial, max_len);
    lv_obj_move_foreground(p->root);
    lv_obj_remove_flag(p->root, LV_OBJ_FLAG_HIDDEN);
}

void ui_popup_close(ui_popup_t *p)
{
    lv_obj_add_flag(p->root, LV_OBJ_FLAG_HIDDEN);
}
