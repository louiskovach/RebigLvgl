/*
 * Filter Starting / Stopping Filter / Revive Media - driven by the real PLC program.
 *
 * Nothing is timed here any more: every 100 ms the screens read the program's state (VB202)
 * from plc_link and show it. The step times come from the program's timer presets, which
 * plc_link fills from the parameters.
 *
 *   state 1-5  -> Filter Starting       6 -> Filter Mode screen
 *   state 10-14-> Stopping Filter       15-19 -> Revive Media
 *   state 0    -> back to Home
 *
 * The screen follows the program automatically while you're on Home or one of these screens
 * (or just pressed a button that starts something); it never pulls you out of a menu.
 * This file also feeds the status bar, the Idle/Service IO I/O lamps and the pump-fault alarm.
 */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "plc_link.h"

typedef enum { K_TIMED, K_HOLD, K_COUNT, K_END } kind_t;
typedef struct { const char *name; uint8_t state; kind_t kind; } row_def_t;

/* =====================  STEP NAMES (program state in brackets)  ===================== */
static const row_def_t k_filter_rows[] = {
    { "Revival Start",          1, K_TIMED },   /* regen valve open        T37 */
    { "Start Pump",             2, K_TIMED },   /* pump on                 T38 */
    { "Precoat / Revival",      3, K_TIMED },   /* coat media              T39 */
    { "Opening Effluent Valve", 4, K_TIMED },   /* effluent valve open     T40 */
    { "Closing Revival Valve",  5, K_TIMED },   /* regen valve closing     T41 */
    { "Filter Mode",            6, K_HOLD  },   /* running                     */
};
static const row_def_t k_stopping_rows[] = {
    { "Opening Revival Valve",  10, K_TIMED },  /* T42 */
    { "Closing Effluent Valve", 11, K_TIMED },  /* T43 */
    { "Stopping Pump",          12, K_TIMED },  /* T44 */
    { "Closing Revival Valve",  13, K_TIMED },  /* T45 */
    { "System Idle",            14, K_END   },
};
static const row_def_t k_revive_rows[] = {
    { "Revive Starting",         15, K_TIMED },
    { "Lowering Cylinder",       16, K_TIMED },  /* bump down T60 */
    { "Raising Cylinder",        17, K_TIMED },  /* bump up   T61 */
    { "Revive Cycles Remaining",  0, K_COUNT },
    { "System Idle",             18, K_END   },
};
/* =================================================================================== */

#define ARRAY_LEN(a)   ((uint8_t)(sizeof(a) / sizeof((a)[0])))
#define SEQ_MAX_ROWS   8
#define ROW_H          50
#define ROW_GAP        4
#define VISIBLE_ROWS   5
#define LIST_X         190
#define LIST_Y         66
#define LIST_W         (UI_W - UI_MARGIN - LIST_X)
#define LIST_H         (VISIBLE_ROWS * ROW_H + (VISIBLE_ROWS - 1) * ROW_GAP)
#define TEXT_X         46
#define HOME_DELAY_MS  1200        /* show the finished list briefly before going Home */

typedef enum { ROW_PENDING, ROW_ACTIVE, ROW_DONE } row_state_t;

typedef struct {
    const char      *title;
    const row_def_t *rows;
    uint8_t          n;
    ui_screen_id_t   id;
    bool             has_stop;

    lv_obj_t *scr, *list, *title_lbl;
    lv_obj_t *row[SEQ_MAX_ROWS], *box[SEQ_MAX_ROWS], *tick[SEQ_MAX_ROWS];
    lv_obj_t *name[SEQ_MAX_ROWS], *time[SEQ_MAX_ROWS], *bar[SEQ_MAX_ROWS];
    int8_t    shown[SEQ_MAX_ROWS];  /* last drawn row_state_t, -1 = not drawn */
    int       scrolled_to;
} seq_t;

static seq_t s_filter   = { .title = "Filter Starting",   .rows = k_filter_rows,   .n = ARRAY_LEN(k_filter_rows),
                            .id = UI_SCR_FILTER,   .has_stop = true };
static seq_t s_stopping = { .title = "Stopping Filter",   .rows = k_stopping_rows, .n = ARRAY_LEN(k_stopping_rows),
                            .id = UI_SCR_STOPPING, .has_stop = false };
static seq_t s_revive   = { .title = "Revive Media Mode", .rows = k_revive_rows,   .n = ARRAY_LEN(k_revive_rows),
                            .id = UI_SCR_REVIVE,   .has_stop = true };

/* follower state */
static plc_snap_t s_snap;
static uint8_t    s_prev_state = 0xFF;
static uint8_t    s_last_phase;        /* 1 start, 2 stop, 3 revive (for the finishing view) */
static bool       s_follow;            /* a screen button just started something */
static bool       s_fast;              /* Diagnostic Filter run */
static lv_timer_t *s_home_timer;
static uint8_t    s_out_cache[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
static bool       s_fault_prev;

/* ------------------------------------------------------------------ helpers */
static void fmt_mmss(char *buf, size_t len, uint32_t ms)
{
    unsigned s = (unsigned)((ms + 999u) / 1000u);
    snprintf(buf, len, "%02u:%02u", s / 60, s % 60);
}

static ui_screen_id_t phase_screen(uint8_t st)
{
    if (st >= 1 && st <= 5)   return UI_SCR_FILTER;
    if (st == 19)             return UI_SCR_FILTER;
    if (st == 6)              return UI_SCR_FILTERMODE;
    if (st >= 10 && st <= 14) return UI_SCR_STOPPING;
    if (st >= 15 && st <= 18) return UI_SCR_REVIVE;
    return UI_SCR_HOME;
}

static const char *state_status(uint8_t st, bool fault)
{
    if (fault)                         return "Pump Fault";
    if ((st >= 1 && st <= 5) || st == 19) return "Starting";
    if (st == 6)                       return "Filtering";
    if (st >= 10 && st <= 14)          return "Stopping";
    if (st >= 15 && st <= 18)          return "Reviving";
    return "Ready";
}

static bool on_follow_screen(void)
{
    lv_obj_t *a = lv_screen_active();
    return s_follow || a == s_filter.scr || a == s_stopping.scr || a == s_revive.scr ||
           ui_screen_is(UI_SCR_HOME) || ui_screen_is(UI_SCR_FILTERMODE);
}

/* Sweep animation for the Filter Mode row */
static void sweep_cb(void *var, int32_t v)
{
    int32_t s = v < 0 ? 0 : v;
    int32_t e = v + 250 > 1000 ? 1000 : v + 250;
    lv_bar_set_start_value(var, s, LV_ANIM_OFF);
    lv_bar_set_value(var, e, LV_ANIM_OFF);
}

static void sweep(lv_obj_t *bar, bool on)
{
    bool running = lv_anim_get(bar, sweep_cb) != NULL;
    if (on == running) return;
    if (on) {
        lv_bar_set_mode(bar, LV_BAR_MODE_RANGE);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, bar);
        lv_anim_set_exec_cb(&a, sweep_cb);
        lv_anim_set_values(&a, -250, 1000);
        lv_anim_set_duration(&a, 1400);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    } else {
        lv_anim_delete(bar, sweep_cb);
        lv_bar_set_mode(bar, LV_BAR_MODE_NORMAL);
        lv_bar_set_start_value(bar, 0, LV_ANIM_OFF);
    }
}

/* ------------------------------------------------------------------ drawing */
static void draw_state(seq_t *s, int i, row_state_t st)
{
    if (s->shown[i] == (int8_t)st) return;
    s->shown[i] = (int8_t)st;
    lv_color_t txt, box;
    lv_opa_t   box_opa = LV_OPA_COVER;
    switch (st) {
    case ROW_ACTIVE: txt = UI_COL_TEXT;            box = UI_COL_GREEN; break;
    case ROW_DONE:   txt = lv_color_hex(0x8F95D8); box = UI_COL_DONE;  break;
    default:         txt = UI_COL_TITLE_Y;         box = UI_COL_TITLE_Y; box_opa = LV_OPA_TRANSP; break;
    }
    lv_obj_set_style_text_color(s->name[i], txt, 0);
    lv_obj_set_style_text_color(s->time[i], txt, 0);
    lv_obj_set_style_bg_color(s->box[i], box, 0);
    lv_obj_set_style_bg_opa(s->box[i], box_opa, 0);
    lv_obj_set_style_border_color(s->box[i], st == ROW_ACTIVE ? UI_COL_TEXT : box, 0);
    if (st == ROW_DONE) lv_obj_remove_flag(s->tick[i], LV_OBJ_FLAG_HIDDEN);
    else                lv_obj_add_flag(s->tick[i], LV_OBJ_FLAG_HIDDEN);
}

static void set_text_if(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

/* What each row looks like for program state 'st'. 'finished' = the program has gone back to
 * idle (0) and we're showing the completed list for a moment. */
static row_state_t row_for(const seq_t *s, int i, uint8_t st, bool finished)
{
    const row_def_t *r = &s->rows[i];
    if (s == &s_revive) {
        bool done_all = finished || st == 18;
        switch (r->kind) {
        case K_COUNT: return done_all ? ROW_DONE : ROW_PENDING;
        case K_END:   return done_all ? ROW_ACTIVE : ROW_PENDING;
        default:
            if (done_all)              return ROW_DONE;
            if (st == r->state)        return ROW_ACTIVE;
            if (r->state == 15)        return st >= 16 ? ROW_DONE : ROW_PENDING;
            if (r->state == 16)        return st == 17 ? ROW_DONE : ROW_PENDING;   /* alternates */
            return ROW_PENDING;
        }
    }
    uint8_t lo = s->rows[0].state, hi = s->rows[s->n - 1].state;
    if (finished)          return r->kind == K_END ? ROW_ACTIVE : ROW_DONE;
    if (st == r->state)    return ROW_ACTIVE;
    if (st > r->state && st >= lo && st <= hi) return ROW_DONE;
    return ROW_PENDING;
}

static void seq_draw(seq_t *s, uint8_t st, bool finished)
{
    uint32_t el = s_snap.step_ms;            /* the program's own timer for this step */
    int active = -1;
    for (int i = 0; i < s->n; i++) {
        const row_def_t *r = &s->rows[i];
        row_state_t rs = row_for(s, i, st, finished);
        draw_state(s, i, rs);
        if (rs == ROW_ACTIVE && active < 0) active = i;

        char buf[16] = "";
        bool show_bar = false;
        if (r->kind == K_TIMED) {
            uint32_t total = plc_link_state_time_ms(r->state);
            if (rs == ROW_DONE)                 snprintf(buf, sizeof(buf), "00:00");
            else if (rs == ROW_ACTIVE && total) {
                fmt_mmss(buf, sizeof(buf), el < total ? total - el : 0);
                lv_bar_set_value(s->bar[i], (int32_t)(el >= total ? 1000 : el * 1000u / total), LV_ANIM_OFF);
                show_bar = true;
            } else if (total)                   fmt_mmss(buf, sizeof(buf), total);
        } else if (r->kind == K_COUNT) {
            int left = s_snap.bumps_total - s_snap.bumps_done;
            snprintf(buf, sizeof(buf), "%02d", (finished || st == 18 || left < 0) ? 0 : left);
        }
        set_text_if(s->time[i], buf);

        bool hold = (r->kind == K_HOLD && rs == ROW_ACTIVE && !s_snap.halted);
        sweep(s->bar[i], hold);
        if (show_bar || (r->kind == K_HOLD && rs == ROW_ACTIVE)) lv_obj_remove_flag(s->bar[i], LV_OBJ_FLAG_HIDDEN);
        else                  lv_obj_add_flag(s->bar[i], LV_OBJ_FLAG_HIDDEN);
    }
    /* keep the previous row visible at the top, active row second (row boundaries only) */
    int top = active > 0 ? active - 1 : 0;
    if (active >= 0 && top != s->scrolled_to) {
        lv_obj_scroll_to_y(s->list, top * (ROW_H + ROW_GAP), LV_ANIM_ON);
        s->scrolled_to = top;
    }
}

static void seq_reset_view(seq_t *s)
{
    for (int i = 0; i < s->n; i++) s->shown[i] = -1;
    s->scrolled_to = 0;
    lv_obj_scroll_to_y(s->list, 0, LV_ANIM_OFF);
}

/* ------------------------------------------------------------------ follower */
static void home_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_home_timer = NULL;
    if (s_snap.state == 0 && (ui_screen_is(UI_SCR_FILTER) || ui_screen_is(UI_SCR_STOPPING) ||
                              ui_screen_is(UI_SCR_REVIVE) || ui_screen_is(UI_SCR_FILTERMODE)))
        ui_go(UI_SCR_HOME);
}

static void set_fast(bool fast)
{
    s_fast = fast;
    plc_link_set_fast(fast);
    lv_label_set_text(s_filter.title_lbl, fast ? "Diagnostic Filter" : "Filter Starting");
}

static void on_state_change(uint8_t prev, uint8_t st)
{

    if (st >= 1 && st <= 6)   s_last_phase = 1;
    if (st >= 10 && st <= 14) s_last_phase = 2;
    if (st >= 15 && st <= 18) s_last_phase = 3;

    if (s_fast && (st == 6 || st == 0)) set_fast(false);       /* Diagnostic Filter done */

    /* new run of a sequence: start its list fresh */
    if (phase_screen(st) != phase_screen(prev)) {
        if (phase_screen(st) == UI_SCR_FILTER)   seq_reset_view(&s_filter);
        if (phase_screen(st) == UI_SCR_STOPPING) seq_reset_view(&s_stopping);
        if (phase_screen(st) == UI_SCR_REVIVE)   seq_reset_view(&s_revive);
    }

    if (!ui_screen_is(UI_SCR_DRAIN)) ui_set_system_status(state_status(st, s_snap.pump_fault));

    if (st == 0) {
        s_follow = false;
        if (!s_home_timer) {
            s_home_timer = lv_timer_create(home_timer_cb, HOME_DELAY_MS, NULL);
            lv_timer_set_repeat_count(s_home_timer, 1);
        }
        return;
    }
    if (on_follow_screen()) {
        ui_screen_id_t want = phase_screen(st);
        if (!ui_screen_is(want)) ui_go(want);
    }
}

/* lamps on the Idle screen (A-H) and Service IO, from the program's I/O */
static void update_io(void)
{
    uint8_t now[4] = { s_snap.ib[0], s_snap.ib[1], s_snap.qb[0], s_snap.qb[1] };
    if (memcmp(now, s_out_cache, sizeof(now)) == 0) return;
    memcpy(s_out_cache, now, sizeof(now));

    /* Idle screen outputs A-H */
    ui_idle_set_output(0, plc_snap_bit(&s_snap, 'I', 1, 3));   /* A. Main Pump Input  = I1.3 */
    ui_idle_set_output(1, plc_snap_bit(&s_snap, 'Q', 0, 3));   /* B. Revival Control  = Q0.3 */
    ui_idle_set_output(2, plc_snap_bit(&s_snap, 'Q', 0, 1));   /* C. Revival Valve    = Q0.1 */
    ui_idle_set_output(3, plc_snap_bit(&s_snap, 'Q', 0, 2));   /* D. Effluent Valve   = Q0.2 */
    ui_idle_set_output(4, plc_snap_bit(&s_snap, 'Q', 0, 0));   /* E. Pump Control     = Q0.0 */
    ui_idle_set_output(5, false);                               /* F. Pump Enable      (none) */
    ui_idle_set_output(6, plc_snap_bit(&s_snap, 'Q', 1, 0));   /* G. Filter Mode      = Q1.0 */
    ui_idle_set_output(7, false);                               /* H. Fireman Protect  (none) */

    /* Service IO points (no PLC point = OFF) */
    static const struct { uint8_t num; char area; uint8_t byte, bit; } map[] = {
        { 5, 'I', 0, 0 }, { 6, 'I', 1, 2 }, { 7, 'I', 1, 3 },
        { 9, 'Q', 0, 3 }, { 10, 'Q', 0, 1 }, { 11, 'Q', 0, 2 }, { 15, 'Q', 0, 0 },
        { 17, 'Q', 1, 0 }, { 21, 'Q', 1, 1 },
    };
    for (int n = 5; n <= 21; n++) ui_service_set_io(n, false);
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        ui_service_set_io(map[i].num, plc_snap_bit(&s_snap, map[i].area, map[i].byte, map[i].bit));
}

static void follow_tick(lv_timer_t *t)
{
    (void)t;
    plc_link_get(&s_snap);

    if (s_snap.pump_fault && !s_fault_prev) {
        ui_alarm_add("Pump fault: no pump running signal");
        if (!ui_screen_is(UI_SCR_DRAIN)) ui_set_system_status("Pump Fault");
    }
    if (!s_snap.pump_fault && s_fault_prev && s_snap.state == 0 && !ui_screen_is(UI_SCR_DRAIN))
        ui_set_system_status("Ready");
    s_fault_prev = s_snap.pump_fault;

    if (s_snap.state != s_prev_state) {
        uint8_t prev = s_prev_state;
        s_prev_state = s_snap.state;
        on_state_change(prev == 0xFF ? 0 : prev, s_snap.state);
    }

    /* redraw whichever sequence screen is showing */
    uint8_t st = s_snap.state;
    lv_obj_t *a = lv_screen_active();
    if (a == s_filter.scr)        seq_draw(&s_filter, st, st == 0 && s_last_phase == 1);
    else if (a == s_stopping.scr) seq_draw(&s_stopping, st, st == 0 && s_last_phase == 2);
    else if (a == s_revive.scr)   seq_draw(&s_revive, st, st == 0 && s_last_phase == 3);

    update_io();
}

/* ------------------------------------------------------------------ build */
static void stop_cb(lv_event_t *e) { (void)e; ui_filter_stop(); }

static void build_row(seq_t *s, int i)
{
    lv_obj_t *row = lv_obj_create(s->list);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, LIST_W, ROW_H);
    s->row[i] = row;

    lv_obj_t *box = lv_obj_create(row);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(box, 30, 30);
    lv_obj_set_pos(box, 0, 5);
    lv_obj_set_style_border_width(box, 3, 0);
    lv_obj_set_style_radius(box, 2, 0);
    s->box[i] = box;
    s->tick[i] = lv_label_create(box);
    lv_label_set_text(s->tick[i], LV_SYMBOL_OK);
    lv_obj_set_style_text_font(s->tick[i], UI_FONT_20, 0);
    lv_obj_set_style_text_color(s->tick[i], UI_COL_TEXT, 0);
    lv_obj_center(s->tick[i]);
    lv_obj_add_flag(s->tick[i], LV_OBJ_FLAG_HIDDEN);

    s->name[i] = lv_label_create(row);
    lv_label_set_text(s->name[i], s->rows[i].name);
    lv_obj_set_style_text_font(s->name[i], UI_FONT_28, 0);
    lv_obj_set_pos(s->name[i], TEXT_X, 2);

    s->time[i] = lv_label_create(row);
    lv_label_set_text(s->time[i], "");
    lv_obj_set_style_text_font(s->time[i], UI_FONT_28, 0);
    lv_obj_set_width(s->time[i], 100);
    lv_obj_set_style_text_align(s->time[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s->time[i], LV_ALIGN_TOP_RIGHT, -8, 2);

    lv_obj_t *bar = lv_bar_create(row);
    lv_obj_set_size(bar, LIST_W - TEXT_X - 8, 7);
    lv_obj_set_pos(bar, TEXT_X, 40);
    lv_bar_set_range(bar, 0, 1000);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0A0C55), LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, UI_COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(bar, UI_COL_YELLOW, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    s->bar[i] = bar;
    s->shown[i] = -1;
}

static void seq_build(seq_t *s)
{
    s->scr = ui_make_screen(false);
    s->title_lbl = ui_make_title(s->scr, s->title, UI_COL_TITLE_Y);

    if (s->has_stop) {
        lv_obj_t *stop = ui_make_stop_button(s->scr);
        lv_obj_set_pos(stop, UI_MARGIN, LIST_Y + (LIST_H - 140) / 2);
        lv_obj_add_event_cb(stop, stop_cb, LV_EVENT_CLICKED, NULL);
    }

    s->list = lv_obj_create(s->scr);
    lv_obj_remove_style_all(s->list);
    lv_obj_set_pos(s->list, LIST_X, LIST_Y);
    lv_obj_set_size(s->list, LIST_W, LIST_H);
    lv_obj_set_flex_flow(s->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s->list, ROW_GAP, 0);
    lv_obj_set_style_pad_bottom(s->list, LIST_H - ROW_H, 0);
    lv_obj_set_scrollbar_mode(s->list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(s->list, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                                LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < s->n && i < SEQ_MAX_ROWS; i++) build_row(s, i);

    if (s->has_stop) {                          /* (i) -> Idle screen, program keeps running */
        lv_obj_t *info = ui_make_info_button(s->scr);
        lv_obj_align(info, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -(UI_STATUS_H + UI_BAR_GAP));
        ui_add_nav(info, UI_SCR_IDLE);
    }
    ui_make_status_bar(s->scr);
    lv_obj_update_layout(s->scr);
}

lv_obj_t *ui_filter_create(void)
{
    seq_build(&s_filter);
    lv_timer_create(follow_tick, 100, NULL);     /* one follower for everything */
    return s_filter.scr;
}
lv_obj_t *ui_stopping_create(void) { seq_build(&s_stopping); return s_stopping.scr; }
lv_obj_t *ui_revive_create(void)   { seq_build(&s_revive);   return s_revive.scr; }

/* ------------------------------------------------------------------ public */
static uint8_t cur_state(void)
{
    plc_snap_t s;
    plc_link_get(&s);
    return s.state;
}

bool ui_filter_running(void) { uint8_t s = cur_state(); return (s >= 1 && s <= 14) || s == 19; }
bool ui_revive_running(void) { uint8_t s = cur_state(); return s >= 15 && s <= 18; }
bool ui_filter_in_mode(void) { return cur_state() == 6; }

/* Home while something runs = that sequence's screen */
ui_screen_id_t ui_seq_home_screen(void) { return phase_screen(cur_state()); }

void ui_filter_start(void)
{
    uint8_t st = cur_state();
    if (st != 0) { ui_go(phase_screen(st)); return; }
    if (s_fast) set_fast(false);
    plc_link_cmd_start();
    s_follow = true;
    seq_reset_view(&s_filter);
    ui_go(UI_SCR_FILTER);
}

void ui_diag_filter_start(void)
{
    if (cur_state() != 0) { ui_go(phase_screen(cur_state())); return; }
    set_fast(true);
    plc_link_cmd_start();
    s_follow = true;
    seq_reset_view(&s_filter);
    ui_go(UI_SCR_FILTER);
}

/* Revive from idle runs the bumps; from Filter Mode the program stops, bumps and restarts */
void ui_revive_start(void)
{
    uint8_t st = cur_state();
    plc_link_cmd_revive();
    s_follow = true;
    if (st == 0) { seq_reset_view(&s_revive); ui_go(UI_SCR_REVIVE); }
    else if (!ui_screen_is(phase_screen(st))) ui_go(phase_screen(st));
}

void ui_filter_stop(void)
{
    s_follow = true;
    plc_link_cmd_stop();
}
