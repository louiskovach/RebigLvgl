/* Filter Mode: shown once the filter start-up steps finish. */
#include "ui.h"

static lv_obj_t *s_ring, *s_health;

/* green (good) -> yellow (middle) -> red (low), blended smoothly */
static lv_color_t health_color(int pct)
{
    const lv_color_t green = UI_COL_GREEN, yellow = lv_color_hex(0xF3D431), red = lv_color_hex(0xE0302A);
    if (pct >= 50) return lv_color_mix(green, yellow, (uint8_t)((pct - 50) * 255 / 50));
    return lv_color_mix(yellow, red, (uint8_t)(pct * 255 / 50));
}

void ui_filtermode_set_health(float pct)
{
    if (!s_ring) return;
    if (pct < 0) {                                   /* not available yet (no sensors) */
        lv_arc_set_value(s_ring, 0);
        lv_label_set_text(s_health, "--");
        return;
    }
    int p = (int)(pct + 0.5f);
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    lv_arc_set_value(s_ring, p);
    lv_obj_set_style_arc_color(s_ring, health_color(p), LV_PART_INDICATOR);
    lv_label_set_text_fmt(s_health, "%d%%", p);
}

static void stop_cb(lv_event_t *e)   { (void)e; ui_filter_stop(); }
static void revive_cb(lv_event_t *e) { (void)e; ui_revive_start(); }   /* revives, then back here */

lv_obj_t *ui_filtermode_create(void)
{
    lv_obj_t *scr = ui_make_screen(false);
    ui_make_title(scr, "Filter Mode", UI_COL_TEXT);

    /* health ring: full circle track, coloured arc = health */
    const int32_t ring = 150;
    s_ring = lv_arc_create(scr);
    lv_obj_set_size(s_ring, ring, ring);
    lv_obj_align(s_ring, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_remove_style(s_ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(s_ring, 270);                    /* start at 12 o'clock */
    lv_arc_set_bg_angles(s_ring, 0, 360);
    lv_arc_set_range(s_ring, 0, 100);
    lv_obj_set_style_pad_all(s_ring, 0, 0);
    lv_obj_set_style_bg_opa(s_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_arc_width(s_ring, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(0x0B0F66), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ring, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_ring, true, LV_PART_INDICATOR);

    s_health = lv_label_create(s_ring);
    lv_obj_set_style_text_font(s_health, UI_FONT_36, 0);
    lv_obj_set_style_text_color(s_health, UI_COL_TEXT, 0);
    lv_obj_align(s_health, LV_ALIGN_CENTER, 0, -8);
    lv_obj_t *cap = lv_label_create(s_ring);
    lv_label_set_text(cap, "Health");
    lv_obj_set_style_text_font(cap, UI_FONT_20, 0);
    lv_obj_set_style_text_color(cap, UI_COL_TEXT, 0);
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, 24);

    ui_filtermode_set_health(-1);

    /* "View Settings" row -> Maintenance */
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, 320, 66);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 214);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_bg_color(row, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_t *fp = ui_make_fingerprint(row);
    lv_obj_align(fp, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, "View Settings");
    lv_obj_set_style_text_font(l, UI_FONT_32, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 70, 0);
    ui_add_nav(row, UI_SCR_MAINT);

    const int32_t by = -(UI_STATUS_H + UI_BAR_GAP);

    lv_obj_t *stop = ui_make_stop_button(scr);
    lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, -(UI_STATUS_H + 6));
    lv_obj_add_event_cb(stop, stop_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *revive = ui_make_button(scr, "Revive\nMedia", UI_BTN_W, UI_BTN_H, UI_FONT_20);
    lv_obj_align(revive, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, by);
    lv_obj_add_event_cb(revive, revive_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *info = ui_make_info_button(scr);
    lv_obj_align(info, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, by);
    ui_add_nav(info, UI_SCR_IDLE);

    lv_obj_t *bell = ui_make_bell_button(scr);
    lv_obj_align(bell, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN - UI_BTN_H - UI_GAP, by);

    ui_make_status_bar(scr);
    return scr;
}
