/* Edit Parameters.
 *  - Tap an ON/OFF row to toggle it, a choice row (units, baud) to step to the next option,
 *    or a number row to open the numeric entry screen.
 *  - Opened from View Information it is view-only (no passcode was entered).
 *  - Exit returns to whichever screen opened it. */
#include "ui.h"
#include "params.h"

#define ROW_H 64

static lv_obj_t *s_title, *s_list, *s_pos_lbl;
static lv_obj_t *s_rows[PARAM_COUNT], *s_badges[PARAM_COUNT], *s_vals[PARAM_COUNT];
static int s_sel;
static bool s_view_only;
static ui_screen_id_t s_return = UI_SCR_SETUP;
static lv_style_t st_row, st_row_sel;

static void refresh_value(int i)
{
    char buf[24];
    param_format(i, buf, sizeof(buf), true);
    lv_label_set_text(s_vals[i], buf);
}

static void select_row(int i, lv_anim_enable_t anim)
{
    if (i < 0) i = 0;
    if (i >= PARAM_COUNT) i = PARAM_COUNT - 1;
    lv_obj_remove_state(s_rows[s_sel], LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s_badges[s_sel], UI_COL_NAVY, 0);
    s_sel = i;
    lv_obj_add_state(s_rows[i], LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s_badges[i], UI_COL_DARK, 0);
    lv_obj_scroll_to_view(s_rows[i], anim);
    lv_label_set_text_fmt(s_pos_lbl, "Parameter %d of %d", i + 1, PARAM_COUNT);
}

static void activate(int i)
{
    if (s_view_only) {
        lv_label_set_text(s_pos_lbl, "View only - edit in Setup > Parameters");
        return;
    }
    const param_def_t *d = param_def(i);
    switch (d->type) {
    case PT_TOGGLE:
        param_set(i, param_get(i) != 0 ? 0 : 1);
        refresh_value(i);
        break;
    case PT_CHOICE:
        param_set(i, (float)(((int)param_get(i) + 1) % d->n_choices));
        refresh_value(i);
        break;
    default:
        ui_param_edit_open(i);
        break;
    }
}

static void row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    select_row(i, LV_ANIM_ON);
    activate(i);
}

static void up_cb(lv_event_t *e)   { (void)e; select_row(s_sel - 1, LV_ANIM_ON); }
static void down_cb(lv_event_t *e) { (void)e; select_row(s_sel + 1, LV_ANIM_ON); }
static void exit_cb(lv_event_t *e) { (void)e; ui_go(s_return); }

void ui_params_on_enter(void)
{
    ui_screen_id_t from = ui_caller(UI_SCR_PARAMS);
    for (int i = 0; i < PARAM_COUNT; i++) refresh_value(i);
    if (from == UI_SCR_PARAM_EDIT) return;          /* back from editing: keep place */

    s_return = from;
    s_view_only = (from == UI_SCR_VIEWINFO);
    lv_label_set_text(s_title, s_view_only ? "View Parameters" : "Edit Parameters");
    select_row(0, LV_ANIM_OFF);
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
}

lv_obj_t *ui_params_create(void)
{
    lv_style_init(&st_row);
    lv_style_set_bg_opa(&st_row, LV_OPA_COVER);
    lv_style_set_bg_color(&st_row, lv_color_hex(0xD3D7F0));
    lv_style_set_radius(&st_row, 10);
    lv_style_set_border_width(&st_row, 2);
    lv_style_set_border_color(&st_row, lv_color_hex(0x3A3F8F));
    lv_style_set_text_color(&st_row, UI_COL_DARK);

    lv_style_init(&st_row_sel);
    lv_style_set_bg_color(&st_row_sel, UI_COL_YELLOW);
    lv_style_set_border_width(&st_row_sel, 3);
    lv_style_set_border_color(&st_row_sel, UI_COL_DARK);
    lv_style_set_outline_width(&st_row_sel, 3);
    lv_style_set_outline_color(&st_row_sel, lv_color_hex(0xD9DFF5));
    lv_style_set_outline_pad(&st_row_sel, 0);

    lv_obj_t *scr = ui_make_screen(false);
    s_title = ui_make_title(scr, "Edit Parameters", UI_COL_TEXT);

    lv_obj_t *up = ui_make_button(scr, LV_SYMBOL_UP, 90, UI_BTN_H, UI_FONT_36);
    lv_obj_set_pos(up, UI_MARGIN, 62 + 160 - 8 - UI_BTN_H);
    lv_obj_add_event_cb(up, up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(up, up_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

    lv_obj_t *down = ui_make_button(scr, LV_SYMBOL_DOWN, 90, UI_BTN_H, UI_FONT_36);
    lv_obj_set_pos(down, UI_MARGIN, 62 + 160 + 8);
    lv_obj_add_event_cb(down, down_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(down, down_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, UI_MARGIN + 90 + UI_GAP, 62);
    lv_obj_set_size(s_list, UI_W - 2 * UI_MARGIN - 90 - UI_GAP, 320);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(0x05074A), 0);
    lv_obj_set_style_radius(s_list, 14, 0);
    lv_obj_set_style_border_width(s_list, 2, 0);
    lv_obj_set_style_border_color(s_list, lv_color_hex(0x8C94E0), 0);
    lv_obj_set_style_pad_all(s_list, 10, 0);
    lv_obj_set_style_pad_row(s_list, 10, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_list, UI_COL_YELLOW, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s_list, 6, LV_PART_SCROLLBAR);

    for (int i = 0; i < PARAM_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_add_style(row, &st_row, 0);
        lv_obj_add_style(row, &st_row_sel, LV_STATE_CHECKED);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_rows[i] = row;

        lv_obj_t *badge = lv_obj_create(row);
        lv_obj_remove_style_all(badge);
        lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(badge, 40, 40);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(badge, UI_COL_NAVY, 0);
        lv_obj_align(badge, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_t *num = lv_label_create(badge);
        lv_label_set_text_fmt(num, "%d", i + 1);
        lv_obj_set_style_text_font(num, UI_FONT_20, 0);
        lv_obj_set_style_text_color(num, lv_color_white(), 0);
        lv_obj_center(num);
        s_badges[i] = badge;

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, param_def(i)->name);
        lv_obj_set_style_text_font(name, UI_FONT_24, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(name, 410);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 60, 0);

        lv_obj_t *pill = lv_obj_create(row);
        lv_obj_remove_style_all(pill);
        lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(pill, 120, 38);
        lv_obj_set_style_radius(pill, 18, 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(pill, UI_COL_NAVY, 0);
        lv_obj_align(pill, LV_ALIGN_RIGHT_MID, -12, 0);
        lv_obj_t *val = lv_label_create(pill);
        lv_obj_set_style_text_font(val, UI_FONT_24, 0);
        lv_obj_set_style_text_color(val, UI_COL_YELLOW, 0);
        lv_obj_center(val);
        s_vals[i] = val;
        refresh_value(i);
    }

    s_pos_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(s_pos_lbl, UI_FONT_20, 0);
    lv_obj_align(s_pos_lbl, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -(UI_MARGIN + 20));

    lv_obj_t *exit = ui_make_button(scr, "Exit", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(exit, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(exit, exit_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_update_layout(scr);
    lv_obj_add_state(s_rows[0], LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(s_badges[0], UI_COL_DARK, 0);
    return scr;
}
