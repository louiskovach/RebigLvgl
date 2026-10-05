#include "ui.h"

static void start_filter_cb(lv_event_t *e) { (void)e; ui_filter_start(); }
static void start_revive_cb(lv_event_t *e) { (void)e; ui_revive_start(); }

static lv_obj_t *make_menu_row(lv_obj_t *parent, const char *txt, int32_t y)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 72);
    lv_obj_set_style_pad_right(row, 16, 0);
    lv_obj_set_pos(row, 200, y);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_bg_color(row, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_PRESSED);

    lv_obj_t *fp = ui_make_fingerprint(row);
    lv_obj_align(fp, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, UI_FONT_36, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 72, 0);
    return row;
}

/* Secret: press on the status bar and drag up to the top of the screen -> Diagnostics.
 * The screen needs PRESS_LOCK so it keeps the touch while the finger passes over buttons. */
static bool s_swipe_armed;

static void swipe_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        s_swipe_armed = (lv_event_get_target(e) == lv_event_get_current_target(e)) &&
                        p.y >= UI_H - UI_STATUS_H - 16;
        break;
    case LV_EVENT_PRESSING:
        if (s_swipe_armed && p.y <= 120) {
            s_swipe_armed = false;
            ui_go(UI_SCR_DIAG);
        }
        break;
    default:   /* released / lost */
        s_swipe_armed = false;
        break;
    }
}

lv_obj_t *ui_home_create(void)
{
    lv_obj_t *scr = ui_make_screen(false);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_PRESS_LOST, NULL);

    lv_obj_t *title = ui_make_title(scr, "What would you like to do?", UI_COL_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    lv_obj_add_event_cb(make_menu_row(scr, "Start Filter", 184), start_filter_cb, LV_EVENT_CLICKED, NULL);
    ui_add_nav(make_menu_row(scr, "Perform Maintenance", 264), UI_SCR_MAINT);

    const int32_t by = -(UI_STATUS_H + UI_BAR_GAP);
    lv_obj_t *revive = ui_make_button(scr, "Revive\nMedia", UI_BTN_W, UI_BTN_H, UI_FONT_20);
    lv_obj_align(revive, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, by);
    lv_obj_add_event_cb(revive, start_revive_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *info = ui_make_info_button(scr);
    lv_obj_align(info, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, by);
    ui_add_nav(info, UI_SCR_IDLE);

    lv_obj_t *bell = ui_make_bell_button(scr);
    lv_obj_align(bell, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN - UI_BTN_H - UI_GAP, by);

    ui_make_status_bar(scr);
    return scr;
}
