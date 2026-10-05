#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include "lvgl.h"

#define UI_W        800
#define UI_H        480
#define UI_STATUS_H 44
#define UI_BTN_H    66      /* every button is this tall (matches the Maintenance menu) */
#define UI_MARGIN   20      /* outer margin on every screen */
#define UI_GAP      16      /* gap between neighbouring buttons */
#define UI_BTN_W    180     /* standard bottom-row button width (Exit, Back, Cancel, Save...) */
#define UI_BAR_GAP  12      /* space between bottom buttons and the status bar */
#define UI_COL_W    ((UI_W - 2 * UI_MARGIN - 20) / 2)   /* menu grid column width (370) */
#define UI_COL1_X   (UI_W - UI_MARGIN - UI_COL_W)       /* menu grid right column x (410) */

/* Montserrat with fixed-width digits so changing numbers never shift (see ui.c). */
extern lv_font_t ui_font_16, ui_font_20, ui_font_24, ui_font_28, ui_font_32, ui_font_36;
#define UI_FONT_16  (&ui_font_16)
#define UI_FONT_20  (&ui_font_20)
#define UI_FONT_24  (&ui_font_24)
#define UI_FONT_28  (&ui_font_28)
#define UI_FONT_32  (&ui_font_32)
#define UI_FONT_36  (&ui_font_36)

/* Palette */
#define UI_COL_BG          lv_color_hex(0x2328B6)  /* solid blue background */
#define UI_COL_BG_LAV      lv_color_hex(0xB3B5D8)  /* solid lavender (keypad screens) */
#define UI_COL_TEXT        lv_color_hex(0xDDE3FA)
#define UI_COL_TITLE_Y     lv_color_hex(0xE9D24B)
#define UI_COL_YELLOW      lv_color_hex(0xF3D431)
#define UI_COL_GREEN       lv_color_hex(0x2DB34A)
#define UI_COL_DONE        lv_color_hex(0x5E62A0)
#define UI_COL_NAVY        lv_color_hex(0x1D2399)
#define UI_COL_DARK        lv_color_hex(0x151515)
#define UI_COL_RED         lv_color_hex(0xD01818)
#define UI_COL_PANEL       lv_color_hex(0xC9CCE6)

typedef enum {
    UI_SCR_HOME,
    UI_SCR_IDLE,        /* "Idle" live-status screen (the (i) button) */
    UI_SCR_PASSCODE,    /* login to Setup */
    UI_SCR_CHPASS,      /* change passcode */
    UI_SCR_MAINT,
    UI_SCR_SETUP,
    UI_SCR_VIEWINFO,
    UI_SCR_INFO,        /* system information */
    UI_SCR_PARAMS,
    UI_SCR_NETWORK,
    UI_SCR_IPCFG,
    UI_SCR_WEBCFG,
    UI_SCR_DATETIME,
    UI_SCR_ALARMS,
    UI_SCR_ADVANCED,
    UI_SCR_DIAG,
    UI_SCR_FILTER,
    UI_SCR_REVIVE,
    UI_SCR_STOPPING,    /* Stopping Filter */
    UI_SCR_PARAM_EDIT,  /* numeric entry for one parameter */
    UI_SCR_FILTERMODE,  /* running filter: health + Stop */
    UI_SCR_DISPLAY,     /* Adjust Display */
    UI_SCR_MODEL,       /* Select System Model */
    UI_SCR_MESSAGE,     /* generic message / confirm */
    UI_SCR_ANALOG,      /* Analog Scaling list */
    UI_SCR_CALIBRATE,   /* one input's scaling */
    UI_SCR_CAL_EDIT,    /* keypad for one scaling value */
    UI_SCR_SERVICE_IO,  /* Diagnostics */
    UI_SCR_COLOR,
    UI_SCR_TOUCH,
    UI_SCR_SMTP,
    UI_SCR_SPLASH,      /* power-up logos + bubbles */
    UI_SCR_DRAIN,       /* Maintenance -> Drain / Rinse */
    UI_SCR_COUNT
} ui_screen_id_t;

/* ---------------- core (ui.c) ---------------- */
void           ui_init(void);                 /* call with the LVGL port lock held */
void           ui_go(ui_screen_id_t id);
ui_screen_id_t ui_caller(ui_screen_id_t id);  /* screen that last opened 'id' */
bool           ui_screen_is(ui_screen_id_t id);/* is this screen showing now? */
ui_screen_id_t ui_seq_home_screen(void);      /* Home, or the running sequence's screen */
void           ui_set_system_status(const char *status);
const char    *ui_get_system_status(void);
void           ui_format_datetime(char *buf, size_t len, time_t utc, bool seconds);

/* ---------------- widget helpers (ui.c) ---------------- */
lv_obj_t *ui_make_screen(bool lavender);
lv_obj_t *ui_make_title(lv_obj_t *parent, const char *txt, lv_color_t color);
void      ui_fit_label(lv_obj_t *label, int32_t max_w);   /* largest font (36..20) that fits one line */
lv_obj_t *ui_make_title_bar(lv_obj_t *parent, const char *txt);  /* yellow full-width title bar */
lv_obj_t *ui_make_button(lv_obj_t *parent, const char *txt, int32_t w, int32_t h, const lv_font_t *font);
lv_obj_t *ui_make_menu_button(lv_obj_t *parent, const char *txt, int32_t w);  /* left-aligned, auto-fit */
lv_obj_t *ui_make_info_button(lv_obj_t *parent);
lv_obj_t *ui_make_info_button_outline(lv_obj_t *parent);   /* thin black ring, yellow inside */
lv_obj_t *ui_make_bell_button(lv_obj_t *parent);
lv_obj_t *ui_make_fingerprint(lv_obj_t *parent);
lv_obj_t *ui_make_stop_button(lv_obj_t *parent);
lv_obj_t *ui_make_field(lv_obj_t *parent, int32_t w, int32_t h);           /* flat yellow tappable field */
void      ui_make_status_bar(lv_obj_t *scr);
void      ui_make_clock_bar(lv_obj_t *scr);
lv_obj_t *ui_make_check(lv_obj_t *parent, const char *txt);
void      ui_check_set(lv_obj_t *chk, bool on);
bool      ui_check_get(lv_obj_t *chk);
void      ui_add_nav(lv_obj_t *btn, ui_screen_id_t target);
/* Place n objects left-to-right between the margins with equal gaps; y = top of the row. */
void      ui_spread_row(lv_obj_t *const objs[], int n, int32_t y);
#define   UI_ROW_Y        (UI_H - UI_MARGIN - UI_BTN_H)                   /* bottom row           */
#define   UI_ROW_Y_BAR    (UI_H - UI_STATUS_H - UI_BAR_GAP - UI_BTN_H)    /* above the status bar */
void      ui_update_bells(bool alarm_active);

/* ---------------- screens ---------------- */
lv_obj_t *ui_home_create(void);
lv_obj_t *ui_idle_create(void);
lv_obj_t *ui_passcode_create(void);
lv_obj_t *ui_chpass_create(void);
lv_obj_t *ui_maint_create(void);
lv_obj_t *ui_setup_create(void);
lv_obj_t *ui_viewinfo_create(void);
lv_obj_t *ui_info_create(void);
lv_obj_t *ui_params_create(void);
lv_obj_t *ui_network_create(void);
lv_obj_t *ui_ipcfg_create(void);
lv_obj_t *ui_webcfg_create(void);
lv_obj_t *ui_datetime_create(void);
lv_obj_t *ui_alarms_create(void);
lv_obj_t *ui_advanced_create(void);
lv_obj_t *ui_diag_create(void);
lv_obj_t *ui_filter_create(void);
lv_obj_t *ui_revive_create(void);
lv_obj_t *ui_stopping_create(void);
lv_obj_t *ui_param_edit_create(void);
lv_obj_t *ui_filtermode_create(void);
lv_obj_t *ui_display_create(void);
lv_obj_t *ui_model_create(void);
lv_obj_t *ui_message_create(void);
lv_obj_t *ui_analog_create(void);
lv_obj_t *ui_calibrate_create(void);
lv_obj_t *ui_cal_edit_create(void);
lv_obj_t *ui_service_create(void);
lv_obj_t *ui_color_create(void);
lv_obj_t *ui_touch_create(void);
lv_obj_t *ui_smtp_create(void);
lv_obj_t *ui_splash_create(void);
lv_obj_t *ui_drain_create(void);
void      ui_drain_start(void);     /* Maintenance -> Drain/Rinse */
void      ui_splash_start(void);

void ui_passcode_on_enter(void);
void ui_chpass_on_enter(void);
void ui_info_on_enter(void);
void ui_params_on_enter(void);
void ui_ipcfg_on_enter(void);
void ui_webcfg_on_enter(void);
void ui_datetime_on_enter(void);
void ui_alarms_on_enter(void);
void ui_param_edit_on_enter(void);
void ui_display_on_enter(void);
void ui_model_on_enter(void);
void ui_analog_on_enter(void);
void ui_calibrate_on_enter(void);
void ui_cal_edit_on_enter(void);
void ui_service_on_enter(void);
void ui_service_refresh(void);          /* live analog values */
void ui_color_on_enter(void);
void ui_touch_on_enter(void);
void ui_smtp_on_enter(void);
void ui_idle_refresh_sensors(void);     /* ui_idle.c */
void ui_analog_refresh(void);           /* ui_analog.c */
void ui_filtermode_set_health(float pct);

/* Generic message screen. on_ok == NULL -> OK goes to 'back'. ok_text == NULL -> no buttons. */
typedef void (*ui_action_cb_t)(void);
void ui_message_show(const char *title, const char *body, const char *ok_text, ui_action_cb_t on_ok,
                     const char *cancel_text, ui_screen_id_t back);
void ui_param_edit_open(int param_id);   /* opens the numeric entry screen */

/* ================= PLC-facing API (call with the LVGL port lock held) ================= */
void ui_filter_start(void);     /* start (or show, if already running) */
void ui_revive_start(void);
void ui_revive_stop(void);      /* aborts the running revive, back to the main page */
void ui_filter_stop(void);      /* runs the Stopping Filter sequence */
bool ui_filter_in_mode(void);   /* start-up finished, filter is in Filter Mode */
void ui_diag_filter_start(void);/* filter start-up with every step = 1 s */

/* Service IO points 1..21 (5-8 inputs, 9-21 outputs) */
void ui_service_set_io(int num, bool on);
bool ui_service_get_io(int num);
bool ui_filter_running(void);
bool ui_revive_running(void);

void ui_alarm_add(const char *msg);
int  ui_alarm_unacked_count(void);

void ui_idle_set_analog(int idx, float raw, float value);   /* idx 0..5 */
void ui_idle_set_output(int idx, bool on);                  /* idx 0..7 = A..H */

void ui_sysinfo_set(const char *serial, const char *model);
