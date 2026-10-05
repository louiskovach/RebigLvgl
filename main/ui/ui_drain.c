/*
 * Maintenance -> Drain / Rinse
 *
 * One screen whose contents change per step (keeps memory low):
 *   1. Revival      "Perform Revival to loosen Perlite"   [Revive] [Next]
 *                   Revive runs Revive Starting -> Lowering -> Raising -> Cycles in the list.
 *   2-7. Valves     photo + instruction + red "Valve N"   [Next]
 *                   V4 close, V5 open, V7 open, V8 open (wait until empty), V5 close, V4 open
 *   8. Pump         "Turn on pump to fill tank ..."        [Pump On] [Pump Off] [Next]
 *                   Pump On  -> Starting Pump (param 13) -> Pump running, filling tank
 *                   Pump Off -> Stopping Pump (param 18) -> Pump off
 * Stop (any step) and Next on the last step turn the pump off and return to Maintenance.
 * Driven by the PLC program: Revive pulses the bump input (states 15-18), Pump On/Off sets the
 * HMI pump bit V250.0 and waits for the pump running signal I1.3.
 */
#include <stdio.h>
#include "ui.h"
#include "params.h"
#include "plc_link.h"
#include "images/img_v4.h"
#include "images/img_v5.h"
#include "images/img_v7.h"
#include "images/img_v8.h"

typedef enum {
    ST_REVIVE, ST_V4_CLOSE, ST_V5_OPEN, ST_V7_OPEN, ST_V8_OPEN, ST_V5_CLOSE, ST_V4_OPEN, ST_PUMP, ST_COUNT
} step_t;

static const struct { const char *text; const lv_image_dsc_t *img; const char *valve; } k_valve[ST_COUNT] = {
    [ST_V4_CLOSE] = { "Close Pump Throttle Valve (V4)",                &img_v4, "Valve 4" },
    [ST_V5_OPEN]  = { "Open Drain Valve (V5)",                         &img_v5, "Valve 5" },
    [ST_V7_OPEN]  = { "Open Tank Vent Valve (V7)",                     &img_v7, "Valve 7" },
    [ST_V8_OPEN]  = { "Open System Vent (V8) wait until tank empties", &img_v8, "Valve 8" },
    [ST_V5_CLOSE] = { "Close Drain Valve (V5)",                        &img_v5, "Valve 5" },
    [ST_V4_OPEN]  = { "Open Pump Throttle Valve (V4)",                 &img_v4, "Valve 4" },
};

typedef enum { RS_PENDING, RS_ACTIVE, RS_DONE } row_state_t;
typedef enum { PUMP_FIRST, PUMP_STARTING, PUMP_ON, PUMP_STOPPING, PUMP_OFF } pump_t;

#define MAX_ROWS  5
#define LIST_X    190
#define LIST_Y    70
#define LIST_W    (UI_W - UI_MARGIN - LIST_X)
#define IMG_Y     94

static lv_obj_t *s_scr, *s_sub, *s_list, *s_img, *s_valve_lbl, *s_valve_sh;
static lv_obj_t *s_row[MAX_ROWS], *s_box[MAX_ROWS], *s_tick[MAX_ROWS], *s_name[MAX_ROWS], *s_time[MAX_ROWS];
static lv_obj_t *s_btn_revive, *s_btn_pon, *s_btn_poff, *s_btn_next;
static lv_timer_t *s_timer;

static step_t   s_step;
static bool     s_rv_running, s_rv_done, s_rv_seen;   /* seen = program entered the bump states */
static pump_t   s_pump;


/* ------------------------------------------------------------------ helpers */

static void fmt_mmss(char *buf, size_t n, uint32_t ms)
{
    unsigned s = (unsigned)((ms + 999) / 1000);
    snprintf(buf, n, "%02u:%02u", s / 60, s % 60);
}

static void show(lv_obj_t *o, bool on)
{
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void enable(lv_obj_t *b, bool on)
{
    if (on) lv_obj_remove_state(b, LV_STATE_DISABLED);
    else    lv_obj_add_state(b, LV_STATE_DISABLED);
}

static void row_set(int i, const char *name, const char *time, row_state_t st)
{
    lv_color_t txt = st == RS_ACTIVE ? UI_COL_TEXT : (st == RS_DONE ? lv_color_hex(0x8F95D8) : UI_COL_TITLE_Y);
    lv_color_t box = st == RS_ACTIVE ? UI_COL_GREEN : (st == RS_DONE ? UI_COL_DONE : UI_COL_TITLE_Y);
    if (name) lv_label_set_text(s_name[i], name);
    if (time) lv_label_set_text(s_time[i], time);
    lv_obj_set_style_text_color(s_name[i], txt, 0);
    lv_obj_set_style_text_color(s_time[i], txt, 0);
    lv_obj_set_style_bg_color(s_box[i], box, 0);
    lv_obj_set_style_bg_opa(s_box[i], st == RS_PENDING ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_box[i], st == RS_ACTIVE ? UI_COL_TEXT : box, 0);
    show(s_tick[i], st == RS_DONE);
    show(s_row[i], true);
}

static void rows_hide(void)
{
    for (int i = 0; i < MAX_ROWS; i++) show(s_row[i], false);
}


/* ------------------------------------------------------------------ revive (step 1) */
#define ROW_PERFORM 4

static void revive_finish(void)
{
    s_rv_running = false;
    s_rv_done = true;
    for (int i = 0; i < 4; i++) row_set(i, NULL, i < 3 ? "00:00" : "00", RS_DONE);
    row_set(ROW_PERFORM, "Perform Revival to loosen Perlite", "", RS_ACTIVE);
    show(s_btn_revive, true);
    show(s_btn_next, true);
    ui_set_system_status("Waiting on User");
}

static void revive_cb(lv_event_t *e)
{
    (void)e;
    char b[16];
    s_rv_running = true;
    s_rv_seen = false;
    row_set(0, "Revive Starting", "", RS_ACTIVE);
    fmt_mmss(b, sizeof(b), plc_link_state_time_ms(16));
    row_set(1, "Lowering Cylinder", b, RS_PENDING);
    fmt_mmss(b, sizeof(b), plc_link_state_time_ms(17));
    row_set(2, "Raising Cylinder", b, RS_PENDING);
    snprintf(b, sizeof(b), "%02d", (int)(param_get(P_REVIVE_STROKES) + 0.5f));
    row_set(3, "Revive Cycles Remaining", b, RS_PENDING);
    show(s_row[ROW_PERFORM], false);
    show(s_btn_revive, false);
    show(s_btn_next, false);
    ui_set_system_status("Reviving");
    plc_link_cmd_revive();                       /* program: bump from idle (states 15-18) */
}

/* follow the program's bump states */
static void revive_tick(const plc_snap_t *sn)
{
    uint8_t st = sn->state;
    if (st >= 15 && st <= 18) s_rv_seen = true;
    if (s_rv_seen && st == 0) { revive_finish(); return; }
    if (!s_rv_seen) return;

    char b[16];
    row_set(0, NULL, NULL, RS_DONE);
    for (int k = 0; k < 2; k++) {                     /* rows 1 (lower, 16) and 2 (raise, 17) */
        uint8_t rs_state = (uint8_t)(16 + k);
        uint32_t total = plc_link_state_time_ms(rs_state), el = sn->step_ms;   /* PLC timer */
        if (st == rs_state) {
            fmt_mmss(b, sizeof(b), el < total ? total - el : 0);
            row_set(1 + k, NULL, b, RS_ACTIVE);
        } else if (st == 18 || (k == 0 && st == 17)) {
            row_set(1 + k, NULL, "00:00", RS_DONE);
        } else {
            fmt_mmss(b, sizeof(b), total);
            row_set(1 + k, NULL, b, RS_PENDING);
        }
    }
    int left = sn->bumps_total - sn->bumps_done;
    snprintf(b, sizeof(b), "%02d", left < 0 || st == 18 ? 0 : left);
    row_set(3, NULL, b, st == 18 ? RS_DONE : RS_PENDING);
}

/* ------------------------------------------------------------------ pump (step 8) */
static void pump_show(void)
{
    rows_hide();
    switch (s_pump) {
    case PUMP_FIRST:    row_set(0, "Turn on pump to fill tank to appropriate level", "", RS_ACTIVE); break;
    case PUMP_STARTING: row_set(0, "Starting Pump", "", RS_ACTIVE); break;
    case PUMP_ON:       row_set(0, "Starting Pump", "", RS_DONE);
                        row_set(1, "Pump running, filling tank", "", RS_ACTIVE); break;
    case PUMP_STOPPING: row_set(0, "Stopping Pump", "", RS_ACTIVE); break;
    case PUMP_OFF:      row_set(0, "Stopping Pump", "", RS_DONE);
                        row_set(1, "Pump off", "", RS_ACTIVE); break;
    }
    /* Pump On only while off, Pump Off only while on; neither while it's changing */
    enable(s_btn_pon,  s_pump == PUMP_FIRST || s_pump == PUMP_OFF);
    enable(s_btn_poff, s_pump == PUMP_ON);
}

static void pump_on_cb(lv_event_t *e)
{
    (void)e;
    s_pump = PUMP_STARTING;
    plc_link_manual_pump(true);                  /* V250.0 -> program turns Q0.0 on */
    pump_show();
}

static void pump_off_cb(lv_event_t *e)
{
    (void)e;
    s_pump = PUMP_STOPPING;
    plc_link_manual_pump(false);
    pump_show();
}

/* Starting -> running once the pump running signal (I1.3) is on; stopping -> off once it's gone */
static void pump_tick(const plc_snap_t *sn)
{
    bool proof = plc_snap_bit(sn, 'I', 1, 3);
    if (s_pump == PUMP_STARTING && (proof || sn->pump_fault)) {
        s_pump = sn->pump_fault ? PUMP_OFF : PUMP_ON;
        if (sn->pump_fault) plc_link_manual_pump(false);
        pump_show();
    } else if (s_pump == PUMP_STOPPING && !proof && !plc_snap_bit(sn, 'Q', 0, 0)) {
        s_pump = PUMP_OFF;
        pump_show();
    } else if (s_pump == PUMP_ON && sn->pump_fault) {   /* lost while running */
        plc_link_manual_pump(false);
        s_pump = PUMP_OFF;
        pump_show();
    }
}

/* ------------------------------------------------------------------ steps */
static void enter_step(step_t st)
{
    s_step = st;
    bool is_valve = (st != ST_REVIVE && st != ST_PUMP);

    show(s_sub, is_valve);
    show(s_img, is_valve);
    show(s_list, !is_valve);
    show(s_btn_revive, st == ST_REVIVE);
    show(s_btn_pon, st == ST_PUMP);
    show(s_btn_poff, st == ST_PUMP);
    show(s_btn_next, true);

    if (is_valve) {
        const lv_font_t *f = UI_FONT_28;
        lv_point_t sz;
        lv_text_get_size(&sz, k_valve[st].text, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x > UI_W - 2 * UI_MARGIN) f = UI_FONT_24;
        lv_obj_set_style_text_font(s_sub, f, 0);
        lv_label_set_text(s_sub, k_valve[st].text);
        lv_image_set_src(s_img, k_valve[st].img);
        lv_label_set_text(s_valve_lbl, k_valve[st].valve);
        lv_label_set_text(s_valve_sh, k_valve[st].valve);
    } else if (st == ST_REVIVE) {
        rows_hide();
        row_set(ROW_PERFORM, "Perform Revival to loosen Perlite", "", RS_ACTIVE);
    } else {
        s_pump = PUMP_FIRST;
        pump_show();
    }
    ui_set_system_status("Waiting on User");
}

static void leave(void)
{
    if (s_rv_running) plc_link_cmd_stop();       /* abort a revive that is still running */
    s_rv_running = false;
    s_pump = PUMP_FIRST;
    plc_link_manual_pump(false);
    lv_timer_pause(s_timer);
    ui_set_system_status("Ready");
    ui_go(UI_SCR_MAINT);
}

static void next_cb(lv_event_t *e)
{
    (void)e;
    if (s_rv_running) return;
    if (s_step < ST_PUMP) enter_step((step_t)(s_step + 1));
    else                  leave();
}

static void stop_cb(lv_event_t *e) { (void)e; leave(); }

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    plc_snap_t sn;
    plc_link_get(&sn);
    if (s_step == ST_REVIVE && s_rv_running)                                 revive_tick(&sn);
    else if (s_step == ST_PUMP && s_pump != PUMP_FIRST && s_pump != PUMP_OFF) pump_tick(&sn);
    else if (s_step == ST_PUMP && s_pump == PUMP_OFF)                         pump_tick(&sn);
}

void ui_drain_start(void)
{
    plc_snap_t sn;
    plc_link_get(&sn);
    if (sn.state != 0) {
        ui_message_show("Drain / Rinse", "Stop the filter before starting Drain / Rinse.", "OK", NULL, NULL,
                        UI_SCR_MAINT);
        return;
    }
    s_rv_running = false;
    s_rv_done = false;
    s_pump = PUMP_FIRST;
    plc_link_manual_pump(false);
    enter_step(ST_REVIVE);
    lv_timer_resume(s_timer);
    ui_go(UI_SCR_DRAIN);
}

/* ------------------------------------------------------------------ build */
static void build_row(int i)
{
    lv_obj_t *row = lv_obj_create(s_list);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, LIST_W, LV_SIZE_CONTENT);
    s_row[i] = row;

    lv_obj_t *box = lv_obj_create(row);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(box, 30, 30);
    lv_obj_set_pos(box, 0, 4);
    lv_obj_set_style_border_width(box, 3, 0);
    lv_obj_set_style_radius(box, 2, 0);
    s_box[i] = box;
    s_tick[i] = lv_label_create(box);
    lv_label_set_text(s_tick[i], LV_SYMBOL_OK);
    lv_obj_set_style_text_font(s_tick[i], UI_FONT_20, 0);
    lv_obj_set_style_text_color(s_tick[i], UI_COL_TEXT, 0);
    lv_obj_center(s_tick[i]);

    s_name[i] = lv_label_create(row);
    lv_label_set_text(s_name[i], "");
    lv_obj_set_style_text_font(s_name[i], UI_FONT_28, 0);
    lv_label_set_long_mode(s_name[i], LV_LABEL_LONG_WRAP);        /* long instructions wrap */
    lv_obj_set_width(s_name[i], LIST_W - 46 - 110);
    lv_obj_set_pos(s_name[i], 46, 2);

    s_time[i] = lv_label_create(row);
    lv_label_set_text(s_time[i], "");
    lv_obj_set_style_text_font(s_time[i], UI_FONT_28, 0);
    lv_obj_set_width(s_time[i], 100);
    lv_obj_set_style_text_align(s_time[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_time[i], LV_ALIGN_TOP_RIGHT, -8, 2);
}

lv_obj_t *ui_drain_create(void)
{
    s_scr = ui_make_screen(false);
    ui_make_title(s_scr, "Drain / Rinse", UI_COL_TITLE_Y);

    s_sub = lv_label_create(s_scr);                  /* valve instruction under the title */
    lv_label_set_text(s_sub, "");
    lv_obj_set_style_text_color(s_sub, UI_COL_TEXT, 0);
    lv_obj_align(s_sub, LV_ALIGN_TOP_MID, 0, 54);

    lv_obj_t *stop = ui_make_stop_button(s_scr);
    lv_obj_set_pos(stop, UI_MARGIN, 150);
    lv_obj_add_event_cb(stop, stop_cb, LV_EVENT_CLICKED, NULL);

    /* valve photo with red "Valve N" on it */
    s_img = lv_image_create(s_scr);
    lv_obj_set_size(s_img, 360, 315);
    lv_obj_align(s_img, LV_ALIGN_TOP_MID, 0, IMG_Y);
    lv_obj_set_style_border_width(s_img, 2, 0);
    lv_obj_set_style_border_color(s_img, UI_COL_DARK, 0);
    s_valve_sh = lv_label_create(s_img);
    lv_obj_set_style_text_font(s_valve_sh, UI_FONT_36, 0);
    lv_obj_set_style_text_color(s_valve_sh, lv_color_hex(0x4A0A0A), 0);
    lv_obj_align(s_valve_sh, LV_ALIGN_BOTTOM_MID, 2, -8);
    s_valve_lbl = lv_label_create(s_img);
    lv_obj_set_style_text_font(s_valve_lbl, UI_FONT_36, 0);
    lv_obj_set_style_text_color(s_valve_lbl, lv_color_hex(0xE0302A), 0);
    lv_obj_align(s_valve_lbl, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* step list (revive / pump) */
    s_list = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_remove_flag(s_list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_list, LIST_X, LIST_Y);
    lv_obj_set_size(s_list, LIST_W, UI_ROW_Y_BAR - 12 - LIST_Y);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    for (int i = 0; i < MAX_ROWS; i++) build_row(i);

    /* buttons */
    s_btn_revive = ui_make_button(s_scr, "Revive", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_set_pos(s_btn_revive, UI_MARGIN, UI_ROW_Y_BAR);
    lv_obj_add_event_cb(s_btn_revive, revive_cb, LV_EVENT_CLICKED, NULL);

    s_btn_pon = ui_make_button(s_scr, "Pump On", UI_BTN_W, UI_BTN_H, UI_FONT_28);
    lv_obj_set_pos(s_btn_pon, UI_MARGIN, UI_ROW_Y_BAR);
    lv_obj_add_event_cb(s_btn_pon, pump_on_cb, LV_EVENT_CLICKED, NULL);
    s_btn_poff = ui_make_button(s_scr, "Pump Off", UI_BTN_W, UI_BTN_H, UI_FONT_28);
    lv_obj_set_pos(s_btn_poff, UI_MARGIN + UI_BTN_W + UI_GAP, UI_ROW_Y_BAR);
    lv_obj_add_event_cb(s_btn_poff, pump_off_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_opa(s_btn_pon, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_opa(s_btn_poff, LV_OPA_40, LV_STATE_DISABLED);

    s_btn_next = ui_make_button(s_scr, "Next", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_set_pos(s_btn_next, UI_W - UI_MARGIN - UI_BTN_W, UI_ROW_Y_BAR);
    lv_obj_add_event_cb(s_btn_next, next_cb, LV_EVENT_CLICKED, NULL);

    ui_make_status_bar(s_scr);

    s_timer = lv_timer_create(tick_cb, 100, NULL);
    lv_timer_pause(s_timer);
    rows_hide();
    return s_scr;
}
