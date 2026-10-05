/* View Alarms. The PLC adds alarms with ui_alarm_add().
 * Ack = acknowledge all; Clear = remove acknowledged alarms. The bell turns red while any
 * alarm is unacknowledged. */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "plc_link.h"

#define MAX_ALARMS   50
#define ROW_H        56
#define VISIBLE_ROWS 5

typedef struct { char msg[48]; time_t t; bool acked; } alarm_t;

static alarm_t   s_alarms[MAX_ALARMS];   /* newest first */
static int       s_n;
static lv_obj_t *s_list, *s_empty;

static void rebuild(void)
{
    if (!s_list) return;
    lv_obj_clean(s_list);
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);

    for (int i = 0; i < s_n; i++) {
        const alarm_t *a = &s_alarms[i];
        lv_color_t col = a->acked ? UI_COL_DONE : UI_COL_TITLE_Y;

        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x8C94E0), 0);
        lv_obj_set_style_text_color(row, col, 0);

        lv_obj_t *icon = lv_label_create(row);
        lv_label_set_text(icon, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_font(icon, UI_FONT_28, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 10, 0);

        lv_obj_t *msg = lv_label_create(row);
        lv_label_set_text_fmt(msg, "%d. %s", i + 1, a->msg);
        lv_obj_set_style_text_font(msg, UI_FONT_24, 0);
        lv_label_set_long_mode(msg, LV_LABEL_LONG_DOT);
        lv_obj_set_size(msg, 400, lv_font_get_line_height(UI_FONT_24));   /* one line only */
        lv_obj_align(msg, LV_ALIGN_LEFT_MID, 54, 0);

        char ts[72];
        ui_format_datetime(ts, sizeof(ts), a->t, false);
        lv_obj_t *tl = lv_label_create(row);
        lv_label_set_text(tl, ts);
        lv_obj_set_style_text_font(tl, UI_FONT_24, 0);
        lv_obj_align(tl, LV_ALIGN_RIGHT_MID, -10, 0);
    }
    if (s_n) lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    else     lv_obj_remove_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
}

int ui_alarm_unacked_count(void)
{
    int c = 0;
    for (int i = 0; i < s_n; i++) if (!s_alarms[i].acked) c++;
    return c;
}

void ui_alarm_add(const char *msg)
{
    if (s_n == MAX_ALARMS) s_n--;                       /* drop the oldest */
    memmove(&s_alarms[1], &s_alarms[0], sizeof(alarm_t) * s_n);
    snprintf(s_alarms[0].msg, sizeof(s_alarms[0].msg), "%s", msg);
    s_alarms[0].t = time(NULL);
    s_alarms[0].acked = false;
    s_n++;
    rebuild();
    ui_update_bells(true);
}

static void ack_cb(lv_event_t *e)
{
    (void)e;
    plc_link_cmd_fault_reset();                      /* Ack = fault reset (I1.0) */
    for (int i = 0; i < s_n; i++) s_alarms[i].acked = true;
    rebuild();
    ui_update_bells(false);
}

static void clear_cb(lv_event_t *e)
{
    (void)e;
    int w = 0;
    for (int i = 0; i < s_n; i++) if (!s_alarms[i].acked) s_alarms[w++] = s_alarms[i];
    s_n = w;
    rebuild();
    ui_update_bells(ui_alarm_unacked_count() > 0);
}

static void up_cb(lv_event_t *e)   { (void)e; lv_obj_scroll_by_bounded(s_list, 0, ROW_H, LV_ANIM_ON); }
static void down_cb(lv_event_t *e) { (void)e; lv_obj_scroll_by_bounded(s_list, 0, -ROW_H, LV_ANIM_ON); }
static void exit_cb(lv_event_t *e) { (void)e; ui_go(ui_caller(UI_SCR_ALARMS)); }

void ui_alarms_on_enter(void) { rebuild(); }

lv_obj_t *ui_alarms_create(void)
{
    lv_obj_t *scr = ui_make_screen(false);
    ui_make_title(scr, "View Alarms", UI_COL_TEXT);

    const int32_t list_x = UI_MARGIN + 90 + UI_GAP, list_h = VISIBLE_ROWS * ROW_H + 4, mid = 62 + list_h / 2;
    lv_obj_t *up = ui_make_button(scr, LV_SYMBOL_UP, 90, UI_BTN_H, UI_FONT_36);
    lv_obj_set_pos(up, UI_MARGIN, mid - 8 - UI_BTN_H);
    lv_obj_add_event_cb(up, up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *down = ui_make_button(scr, LV_SYMBOL_DOWN, 90, UI_BTN_H, UI_FONT_36);
    lv_obj_set_pos(down, UI_MARGIN, mid + 8);
    lv_obj_add_event_cb(down, down_cb, LV_EVENT_CLICKED, NULL);

    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, list_x, 62);
    lv_obj_set_size(s_list, UI_W - UI_MARGIN - list_x, list_h);
    lv_obj_set_style_border_width(s_list, 2, 0);
    lv_obj_set_style_border_color(s_list, lv_color_hex(0x8C94E0), 0);
    lv_obj_set_style_pad_all(s_list, 2, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_list, UI_COL_YELLOW, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_70, LV_PART_SCROLLBAR);

    s_empty = lv_label_create(scr);
    lv_label_set_text(s_empty, "No alarms");
    lv_obj_set_style_text_font(s_empty, UI_FONT_28, 0);
    lv_obj_align(s_empty, LV_ALIGN_TOP_MID, (list_x + UI_W - UI_MARGIN) / 2 - UI_W / 2, mid - 16);

    lv_obj_t *exit = ui_make_button(scr, "Exit", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_add_event_cb(exit, exit_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *clear = ui_make_button(scr, "Clear", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_add_event_cb(clear, clear_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ack = ui_make_button(scr, "Ack", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_add_event_cb(ack, ack_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *const row[] = { exit, clear, ack };
    ui_spread_row(row, 3, UI_ROW_Y);

    rebuild();
    return scr;
}
