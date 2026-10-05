#pragma once
#include "ui.h"

/* Numeric keypad with an entry box above it. Used by the passcode screens and the
 * popup editors (IP addresses, port). */
typedef bool (*ui_kp_submit_cb_t)(const char *text, void *user);  /* return true if accepted */

typedef struct {
    lv_obj_t          *box, *text, *cursor, *msg, *dot_btn;
    lv_timer_t        *msg_timer;
    char               buf[24];
    uint8_t            max_len;
    bool               masked;
    bool               allow_dot;
    ui_kp_submit_cb_t  submit_cb;
    void              *user;
} ui_keypad_t;

#define UI_KEYPAD_W 308
#define UI_KEYPAD_H 370

void ui_keypad_create(ui_keypad_t *kp, lv_obj_t *parent, int32_t y, bool masked, bool allow_dot);
void ui_keypad_reset(ui_keypad_t *kp, const char *initial, uint8_t max_len);
void ui_keypad_submit(ui_keypad_t *kp);                          /* for Save / Next / OK buttons */
void ui_keypad_message(ui_keypad_t *kp, const char *msg, bool error);
void ui_keypad_set_allow_dot(ui_keypad_t *kp, bool allow);   /* only if created with allow_dot */

/* Full-screen popup editor (title + keypad + Cancel/OK) that sits on top of a screen. */
typedef struct {
    lv_obj_t          *root, *title;
    ui_keypad_t        kp;
    ui_kp_submit_cb_t  ok_cb;
    void              *user;
} ui_popup_t;

void ui_popup_create(ui_popup_t *p, lv_obj_t *scr, bool allow_dot);
void ui_popup_open(ui_popup_t *p, const char *title, const char *initial, uint8_t max_len,
                   ui_kp_submit_cb_t ok_cb, void *user);
void ui_popup_close(ui_popup_t *p);
