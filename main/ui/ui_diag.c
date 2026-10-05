/* Diagnostics screens: Service IO, Color Display, Touch Test, Detailed SMTP Logging. */
#include <stdio.h>
#include "ui.h"
#include "settings.h"
#include "sensors.h"

/* ====================================================================== Service IO */
typedef enum { IO_ANALOG, IO_INPUT, IO_OUTPUT } io_kind_t;

static const struct { const char *name; const char *con; io_kind_t kind; } k_io[] = {
    { "Influent Pressure",   NULL,          IO_ANALOG },   /*  1 */
    { "Effluent Pressure",   NULL,          IO_ANALOG },   /*  2 */
    { "Pressure Enable",     NULL,          IO_ANALOG },   /*  3 */
    { "Flow Rate",           NULL,          IO_ANALOG },   /*  4 */
    { "Remote Start",        "CON11/11A",   IO_INPUT  },   /*  5 */
    { "Remote Stop",         "CON12/12A",   IO_INPUT  },   /*  6 */
    { "Pump Confirm Input",  "CON17/17A",   IO_INPUT  },   /*  7 */
    { "UV Ready",            "CON18/18A",   IO_INPUT  },   /*  8 */
    { "Revive Control",      "CON20-22",    IO_OUTPUT },   /*  9 */
    { "Revival Valve (V11)", "CON23-25",    IO_OUTPUT },   /* 10 */
    { "Effluent Valve (V10)","CON26-28",    IO_OUTPUT },   /* 11 */
    { "Option 1",            "CON29-31",    IO_OUTPUT },   /* 12 */
    { "Option 2",            "CON32-34",    IO_OUTPUT },   /* 13 */
    { "Option 3",            "CON35-37",    IO_OUTPUT },   /* 14 */
    { "Pump Control #1",     "CON38-40",    IO_OUTPUT },   /* 15 */
    { "Pump Control #2",     "CON41-43",    IO_OUTPUT },   /* 16 */
    { "Filter Mode #1",      "CON44-46",    IO_OUTPUT },   /* 17 */
    { "Filter Mode #2",      "CON47-49",    IO_OUTPUT },   /* 18 */
    { "Pump Enable Output",  "CON50-52",    IO_OUTPUT },   /* 19 */
    { "Fireman Control",     "CON54",       IO_OUTPUT },   /* 20 */
    { "Alarm Buzzer",        NULL,          IO_OUTPUT },   /* 21 */
};
#define N_IO   ((int)(sizeof(k_io) / sizeof(k_io[0])))
#define IO_CARD_H   70
#define IO_GAP      8
#define IO_FONT     UI_FONT_16      /* one size for all Service IO card text */

static bool       s_io_on[N_IO + 1];   /* 1-based, filled from the PLC */
static lv_obj_t  *s_io_scr, *s_io_list, *s_io_line2[N_IO + 1], *s_io_pill[N_IO + 1], *s_io_box[N_IO + 1];
static lv_style_t st_io_card, st_io_card_pr;

static void io_show(int n)
{
    bool on = s_io_on[n];
    if (s_io_pill[n]) {
        lv_obj_set_style_bg_color(s_io_pill[n], on ? UI_COL_GREEN : UI_COL_NAVY, 0);
        lv_obj_t *l = lv_obj_get_child(s_io_pill[n], 0);
        lv_label_set_text(l, on ? "ON" : "OFF");
        lv_obj_set_style_text_color(l, on ? UI_COL_DARK : UI_COL_TEXT, 0);
    }
    if (s_io_box[n]) {
        lv_obj_set_style_bg_opa(s_io_box[n], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(s_io_box[n], on ? UI_COL_DARK : UI_COL_TEXT, 0);
    }
}

void ui_service_set_io(int num, bool on)
{
    if (num < 1 || num > N_IO) return;
    s_io_on[num] = on;
    if (s_io_scr) io_show(num);
}

bool ui_service_get_io(int num) { return num >= 1 && num <= N_IO && s_io_on[num]; }

void ui_service_refresh(void)
{
    if (!s_io_scr || lv_screen_active() != s_io_scr) return;
    for (int n = 1; n <= 4; n++) {
        int i = n - 1;
        if (!sensor_present(i)) {
            lv_label_set_text(s_io_line2[n], "--");
            lv_obj_set_style_text_color(s_io_line2[n], lv_color_hex(0x1D2399), 0);
            continue;
        }
        float raw = sensor_raw(i);
        char buf[48];
        if (i == 3 && raw < 3.5f) {                  /* 4-20 mA loop open */
            lv_label_set_text(s_io_line2[n], "Disconnect");
            lv_obj_set_style_text_color(s_io_line2[n], UI_COL_RED, 0);
            continue;
        }
        snprintf(buf, sizeof(buf), "%.1f %s   (%.2f %s)", (double)sensor_value(i), sensor_unit(i),
                 (double)raw, sensor_raw_unit(i));
        lv_label_set_text(s_io_line2[n], buf);
        lv_obj_set_style_text_color(s_io_line2[n], lv_color_hex(0x1D2399), 0);
    }
}

static void io_up_cb(lv_event_t *e)   { (void)e; lv_obj_scroll_by_bounded(s_io_list, 0, IO_CARD_H + IO_GAP, LV_ANIM_ON); }
static void io_down_cb(lv_event_t *e) { (void)e; lv_obj_scroll_by_bounded(s_io_list, 0, -(IO_CARD_H + IO_GAP), LV_ANIM_ON); }

void ui_service_on_enter(void)
{
    lv_obj_scroll_to_y(s_io_list, 0, LV_ANIM_OFF);
    for (int n = 1; n <= N_IO; n++) io_show(n);
}

static lv_obj_t *io_card(lv_obj_t *parent, int n, int32_t w)
{
    const io_kind_t kind = k_io[n - 1].kind;
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_add_style(c, &st_io_card, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(c, w, IO_CARD_H);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);    /* status only: the PLC drives the I/O */

    lv_obj_t *badge = lv_obj_create(c);
    lv_obj_remove_style_all(badge);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(badge, 32, 32);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, kind == IO_OUTPUT ? UI_COL_DARK : UI_COL_NAVY, 0);
    lv_obj_align(badge, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_t *num = lv_label_create(badge);
    lv_label_set_text_fmt(num, "%d", n);
    lv_obj_set_style_text_font(num, IO_FONT, 0);
    lv_obj_set_style_text_color(num, lv_color_white(), 0);
    lv_obj_center(num);

    const int32_t text_x = 48, text_w = w - text_x - (kind == IO_ANALOG ? 10 : 88);
    /* All card text is the same size (IO_FONT). Each line has a fixed one-line height so
     * LVGL can never wrap the name down onto the connector line. */
    lv_obj_t *name = lv_label_create(c);
    lv_label_set_text(name, k_io[n - 1].name);
    lv_obj_set_style_text_font(name, IO_FONT, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_size(name, text_w, lv_font_get_line_height(IO_FONT));
    lv_obj_set_pos(name, text_x, 12);

    char l2txt[32];
    if (k_io[n - 1].con)       snprintf(l2txt, sizeof(l2txt), "[%s]", k_io[n - 1].con);
    else if (kind == IO_ANALOG) snprintf(l2txt, sizeof(l2txt), "--");
    else                        snprintf(l2txt, sizeof(l2txt), " ");
    lv_obj_t *l2 = lv_label_create(c);
    lv_label_set_text(l2, l2txt);                       /* text BEFORE long mode / width */
    lv_obj_set_style_text_font(l2, IO_FONT, 0);
    lv_obj_set_style_text_color(l2, lv_color_hex(0x1D2399), 0);
    lv_label_set_long_mode(l2, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(l2, text_w, lv_font_get_line_height(IO_FONT));
    lv_obj_set_pos(l2, text_x, 38);
    s_io_line2[n] = l2;

    if (kind != IO_ANALOG) {
        lv_obj_t *pill = lv_obj_create(c);
        lv_obj_remove_style_all(pill);
        lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(pill, 72, 36);
        lv_obj_set_style_radius(pill, 18, 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(pill, 2, 0);
        lv_obj_set_style_border_color(pill, UI_COL_DARK, 0);
        lv_obj_align(pill, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_t *pl = lv_label_create(pill);
        lv_obj_set_style_text_font(pl, IO_FONT, 0);
        lv_obj_center(pl);
        s_io_pill[n] = pill;
    }
    return c;
}

static lv_obj_t *io_strip_box(lv_obj_t *scr, int n, int32_t x, int32_t y)
{
    lv_obj_t *b = lv_obj_create(scr);
    lv_obj_remove_style_all(b);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(b, 30, 28);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, UI_COL_TEXT, 0);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_bg_color(b, UI_COL_GREEN, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text_fmt(l, "%d", n);
    lv_obj_set_style_text_font(l, UI_FONT_16, 0);
    lv_obj_center(l);
    return b;
}

lv_obj_t *ui_service_create(void)
{
    lv_style_init(&st_io_card);
    lv_style_set_bg_opa(&st_io_card, LV_OPA_COVER);
    lv_style_set_bg_color(&st_io_card, lv_color_hex(0xF3DC5A));
    lv_style_set_border_width(&st_io_card, 2);
    lv_style_set_border_color(&st_io_card, UI_COL_DARK);
    lv_style_set_radius(&st_io_card, 10);
    lv_style_set_text_color(&st_io_card, UI_COL_DARK);
    lv_style_init(&st_io_card_pr);
    lv_style_set_bg_color(&st_io_card_pr, lv_color_hex(0xC9A80C));

    s_io_scr = ui_make_screen(false);
    ui_make_title(s_io_scr, "Service IO", UI_COL_TEXT);

    const int32_t list_x = UI_MARGIN + 90 + UI_GAP, list_y = 60;
    const int32_t list_w = UI_W - UI_MARGIN - list_x;
    const int32_t list_h = 4 * IO_CARD_H + 3 * IO_GAP;
    const int32_t mid = list_y + list_h / 2;

    lv_obj_t *up = ui_make_button(s_io_scr, LV_SYMBOL_UP, 90, UI_BTN_H, UI_FONT_36);
    lv_obj_set_pos(up, UI_MARGIN, mid - 8 - UI_BTN_H);
    lv_obj_add_event_cb(up, io_up_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(up, io_up_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);
    lv_obj_t *down = ui_make_button(s_io_scr, LV_SYMBOL_DOWN, 90, UI_BTN_H, UI_FONT_36);
    lv_obj_set_pos(down, UI_MARGIN, mid + 8);
    lv_obj_add_event_cb(down, io_down_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(down, io_down_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

    s_io_list = lv_obj_create(s_io_scr);
    lv_obj_remove_style_all(s_io_list);
    lv_obj_set_pos(s_io_list, list_x, list_y);
    lv_obj_set_size(s_io_list, list_w, list_h);
    lv_obj_set_flex_flow(s_io_list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(s_io_list, IO_GAP, 0);
    lv_obj_set_style_pad_column(s_io_list, 10, 0);
    lv_obj_set_scrollbar_mode(s_io_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_io_list, LV_DIR_VER);

    const int32_t card_w = (list_w - 10) / 2;
    for (int n = 1; n <= N_IO; n++) io_card(s_io_list, n, card_w);

    lv_obj_t *exit = ui_make_button(s_io_scr, "Exit", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(exit, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    ui_add_nav(exit, UI_SCR_DIAG);

    /* status strip: inputs 5-8, outputs 9-21 */
    const int32_t sx = UI_MARGIN + UI_BTN_W + UI_GAP, bx = sx + 52;
    const int32_t y_in = UI_H - UI_MARGIN - UI_BTN_H, y_out = y_in + 36;
    lv_obj_t *li = lv_label_create(s_io_scr);
    lv_label_set_text(li, "In:");
    lv_obj_set_style_text_font(li, IO_FONT, 0);
    lv_obj_set_pos(li, sx, y_in + 3);
    lv_obj_t *lo = lv_label_create(s_io_scr);
    lv_label_set_text(lo, "Out:");
    lv_obj_set_style_text_font(lo, IO_FONT, 0);
    lv_obj_set_pos(lo, sx, y_out + 3);
    for (int n = 5; n <= 8; n++)  s_io_box[n] = io_strip_box(s_io_scr, n, bx + (n - 5) * 34, y_in);
    for (int n = 9; n <= N_IO; n++) s_io_box[n] = io_strip_box(s_io_scr, n, bx + (n - 9) * 34, y_out);

    ui_service_on_enter();
    return s_io_scr;
}

/* ====================================================================== Color Display */
static int        s_rgb[3];
static lv_obj_t  *s_swatch, *s_hex, *s_rgb_val[3], *s_rgb_sl[3];
static lv_style_t st_card, st_track, st_knob, st_ind[3];
static const char *const k_rgb_names[3] = { "RED", "GREEN", "BLUE" };
static const uint32_t    k_rgb_hex[3]   = { 0xFF3030, 0x30E040, 0x4070FF };

static void color_update(bool move_sliders)
{
    lv_color_t c = lv_color_make((uint8_t)s_rgb[0], (uint8_t)s_rgb[1], (uint8_t)s_rgb[2]);
    lv_obj_set_style_bg_color(s_swatch, c, 0);
    int lum = (s_rgb[0] * 77 + s_rgb[1] * 150 + s_rgb[2] * 29) >> 8;
    lv_obj_set_style_text_color(s_hex, lum > 140 ? UI_COL_DARK : lv_color_white(), 0);
    lv_label_set_text_fmt(s_hex, "#%02X%02X%02X", s_rgb[0], s_rgb[1], s_rgb[2]);
    for (int i = 0; i < 3; i++) {
        lv_label_set_text_fmt(s_rgb_val[i], "%03d", s_rgb[i]);
        if (move_sliders) lv_slider_set_value(s_rgb_sl[i], s_rgb[i], LV_ANIM_OFF);
    }
}

static void rgb_set(int i, int v, bool move)
{
    s_rgb[i] = v < 0 ? 0 : (v > 255 ? 255 : v);
    color_update(move);
}

static void rgb_slider_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    rgb_set(i, (int)lv_slider_get_value(lv_event_get_target(e)), false);
}

static void rgb_step_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_event_get_user_data(e);   /* ch * 10 + step + 1 */
    int ch = code / 10, step = code % 10 - 1;
    rgb_set(ch, s_rgb[ch] + step, true);
}

static void preset_cb(lv_event_t *e)
{
    uint32_t hex = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    s_rgb[0] = (hex >> 16) & 0xFF;
    s_rgb[1] = (hex >> 8) & 0xFF;
    s_rgb[2] = hex & 0xFF;
    color_update(true);
}

void ui_color_on_enter(void)
{
    s_rgb[0] = s_rgb[1] = s_rgb[2] = 0;
    color_update(true);
}

lv_obj_t *ui_color_create(void)
{
    lv_style_init(&st_card);
    lv_style_set_bg_opa(&st_card, LV_OPA_COVER);
    lv_style_set_bg_color(&st_card, lv_color_hex(0x161B8E));
    lv_style_set_border_width(&st_card, 2);
    lv_style_set_border_color(&st_card, lv_color_hex(0x8C94E0));
    lv_style_set_radius(&st_card, 14);

    lv_style_init(&st_track);
    lv_style_set_bg_opa(&st_track, LV_OPA_COVER);
    lv_style_set_bg_color(&st_track, lv_color_hex(0x0B0F66));
    lv_style_set_border_width(&st_track, 2);
    lv_style_set_border_color(&st_track, lv_color_hex(0x5A61C8));
    lv_style_set_radius(&st_track, LV_RADIUS_CIRCLE);

    lv_style_init(&st_knob);
    lv_style_set_bg_opa(&st_knob, LV_OPA_COVER);
    lv_style_set_bg_color(&st_knob, UI_COL_YELLOW);
    lv_style_set_border_width(&st_knob, 3);
    lv_style_set_border_color(&st_knob, UI_COL_DARK);
    lv_style_set_radius(&st_knob, LV_RADIUS_CIRCLE);
    lv_style_set_pad_all(&st_knob, 9);

    lv_obj_t *scr = ui_make_screen(false);
    ui_make_title(scr, "Color Display", UI_COL_TEXT);

    /* R / G / B cards:  row 1 = name ........ value   row 2 = [-] ===slider=== [+] */
    const int32_t card_h = 104, card_gap = 6, top = 64;
    const int32_t sw = 280, sh = 3 * card_h + 2 * card_gap;

    s_swatch = lv_obj_create(scr);
    lv_obj_remove_style_all(s_swatch);
    lv_obj_remove_flag(s_swatch, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_swatch, UI_MARGIN, top);
    lv_obj_set_size(s_swatch, sw, sh);
    lv_obj_set_style_bg_opa(s_swatch, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_swatch, 3, 0);
    lv_obj_set_style_border_color(s_swatch, lv_color_hex(0xD9DFF5), 0);
    lv_obj_set_style_radius(s_swatch, 12, 0);
    s_hex = lv_label_create(s_swatch);
    lv_obj_set_style_text_font(s_hex, UI_FONT_28, 0);
    lv_obj_align(s_hex, LV_ALIGN_BOTTOM_MID, 0, -10);

    const int32_t cx = UI_MARGIN + sw + UI_GAP, cw = UI_W - UI_MARGIN - cx;
    const int32_t pad = 12, row2_y = card_h - pad - UI_BTN_H + 4;
    const int32_t sl_x = pad + UI_BTN_H + 18, sl_w = cw - 2 * (pad + UI_BTN_H + 18);

    for (int i = 0; i < 3; i++) {
        lv_style_init(&st_ind[i]);
        lv_style_set_bg_opa(&st_ind[i], LV_OPA_COVER);
        lv_style_set_bg_color(&st_ind[i], lv_color_black());
        lv_style_set_bg_grad_color(&st_ind[i], lv_color_hex(k_rgb_hex[i]));
        lv_style_set_bg_grad_dir(&st_ind[i], LV_GRAD_DIR_HOR);
        lv_style_set_radius(&st_ind[i], LV_RADIUS_CIRCLE);

        lv_obj_t *card = lv_obj_create(scr);
        lv_obj_remove_style_all(card);
        lv_obj_add_style(card, &st_card, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(card, cx, top + i * (card_h + card_gap));
        lv_obj_set_size(card, cw, card_h);

        lv_obj_t *name = lv_label_create(card);
        lv_label_set_text(name, k_rgb_names[i]);
        lv_obj_set_style_text_font(name, UI_FONT_20, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(k_rgb_hex[i]), 0);
        lv_obj_set_pos(name, pad, 4);

        s_rgb_val[i] = lv_label_create(card);            /* one line, never wraps */
        lv_label_set_text(s_rgb_val[i], "000");
        lv_obj_set_style_text_font(s_rgb_val[i], UI_FONT_20, 0);
        lv_label_set_long_mode(s_rgb_val[i], LV_LABEL_LONG_CLIP);
        lv_obj_align(s_rgb_val[i], LV_ALIGN_TOP_RIGHT, -pad, 4);

        for (int k = 0; k < 2; k++) {
            int step = k ? 1 : -1;
            lv_obj_t *btn = ui_make_button(card, k ? LV_SYMBOL_PLUS : LV_SYMBOL_MINUS, UI_BTN_H, UI_BTN_H, UI_FONT_24);
            lv_obj_set_pos(btn, k ? cw - pad - UI_BTN_H - 4 : pad, row2_y);
            void *code = (void *)(intptr_t)(i * 10 + step + 1);
            lv_obj_add_event_cb(btn, rgb_step_cb, LV_EVENT_CLICKED, code);
            lv_obj_add_event_cb(btn, rgb_step_cb, LV_EVENT_LONG_PRESSED_REPEAT, code);
        }

        lv_obj_t *sl = lv_slider_create(card);
        lv_obj_remove_style_all(sl);
        lv_obj_add_style(sl, &st_track, LV_PART_MAIN);
        lv_obj_add_style(sl, &st_ind[i], LV_PART_INDICATOR);
        lv_obj_add_style(sl, &st_knob, LV_PART_KNOB);
        lv_slider_set_range(sl, 0, 255);
        lv_obj_set_size(sl, sl_w, 14);
        lv_obj_set_pos(sl, sl_x, row2_y + (UI_BTN_H - 14) / 2);
        lv_obj_set_ext_click_area(sl, 24);
        lv_obj_add_event_cb(sl, rgb_slider_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
        s_rgb_sl[i] = sl;
    }

    /* quick test colours */
    static const uint32_t presets[] = { 0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 };
    const int np = sizeof(presets) / sizeof(presets[0]);
    int32_t px = UI_W - UI_MARGIN - np * UI_BTN_H - (np - 1) * 12;
    for (int i = 0; i < np; i++) {
        lv_obj_t *b = ui_make_button(scr, NULL, UI_BTN_H, UI_BTN_H, NULL);
        lv_obj_set_style_bg_color(b, lv_color_hex(presets[i]), 0);
        lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_pos(b, px + i * (UI_BTN_H + 12), UI_H - UI_MARGIN - UI_BTN_H);
        lv_obj_add_event_cb(b, preset_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)presets[i]);
    }

    lv_obj_t *exit = ui_make_button(scr, "Exit", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(exit, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    ui_add_nav(exit, UI_SCR_DIAG);

    ui_color_on_enter();
    return scr;
}

/* ====================================================================== Touch Test
 * Corner dots start black and turn red when touched. A red cross follows the finger while
 * it is down and disappears 2 s after release. X/Y shown on the right. */
#define DOT_D     44
#define DOT_INSET 8
#define CROSS_LEN 44
#define CROSS_W   5

static lv_obj_t   *s_touch_scr, *s_dots[4], *s_cross, *s_tx, *s_ty, *s_corners;
static bool        s_dot_hit[4];
static lv_timer_t *s_cross_timer;

static void dot_set(int i, bool hit)
{
    s_dot_hit[i] = hit;
    lv_obj_set_style_bg_color(s_dots[i], hit ? lv_color_hex(0xE0302A) : UI_COL_DARK, 0);
}

static void cross_hide_cb(lv_timer_t *t)
{
    (void)t;
    s_cross_timer = NULL;                          /* one-shot: deletes itself */
    lv_obj_add_flag(s_cross, LV_OBJ_FLAG_HIDDEN);
}

static void corners_update(lv_point_t p)
{
    int hits = 0;
    for (int i = 0; i < 4; i++) {
        int32_t cx = lv_obj_get_x(s_dots[i]) + DOT_D / 2, cy = lv_obj_get_y(s_dots[i]) + DOT_D / 2;
        int32_t dx = p.x - cx, dy = p.y - cy;
        if (!s_dot_hit[i] && dx * dx + dy * dy <= 40 * 40) dot_set(i, true);
        hits += s_dot_hit[i];
    }
    lv_label_set_text_fmt(s_corners, "Corners: %d / 4%s", hits, hits == 4 ? "  " LV_SYMBOL_OK : "");
}

static void touch_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_cross_timer) lv_timer_delete(s_cross_timer);
        s_cross_timer = lv_timer_create(cross_hide_cb, 2000, NULL);
        lv_timer_set_repeat_count(s_cross_timer, 1);
        return;
    }

    /* PRESSED / PRESSING: cross follows the finger */
    if (s_cross_timer) { lv_timer_delete(s_cross_timer); s_cross_timer = NULL; }
    lv_obj_set_pos(s_cross, p.x - CROSS_LEN / 2, p.y - CROSS_LEN / 2);
    lv_obj_remove_flag(s_cross, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_cross);

    lv_label_set_text_fmt(s_tx, "%d", (int)p.x);
    lv_label_set_text_fmt(s_ty, "%d", (int)p.y);
    corners_update(p);
}

void ui_touch_on_enter(void)
{
    for (int i = 0; i < 4; i++) dot_set(i, false);
    if (s_cross_timer) { lv_timer_delete(s_cross_timer); s_cross_timer = NULL; }
    lv_obj_add_flag(s_cross, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_tx, "---");
    lv_label_set_text(s_ty, "---");
    lv_label_set_text(s_corners, "Corners: 0 / 4");
}

static lv_obj_t *coord_row(lv_obj_t *card, const char *axis, int32_t y)
{
    lv_obj_t *a = lv_label_create(card);
    lv_label_set_text(a, axis);
    lv_obj_set_style_text_font(a, UI_FONT_28, 0);
    lv_obj_set_style_text_color(a, UI_COL_TITLE_Y, 0);
    lv_obj_set_pos(a, 16, y);
    lv_obj_t *v = lv_label_create(card);
    lv_label_set_text(v, "---");
    lv_obj_set_style_text_font(v, UI_FONT_36, 0);
    lv_obj_set_style_text_color(v, UI_COL_TEXT, 0);
    lv_obj_set_width(v, 100);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(v, 56, y - 4);
    return v;
}

static lv_obj_t *plain_obj(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *ui_touch_create(void)
{
    s_touch_scr = ui_make_screen(true);
    lv_obj_add_flag(s_touch_scr, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(s_touch_scr, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_touch_scr, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_touch_scr, touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_touch_scr, touch_cb, LV_EVENT_PRESS_LOST, NULL);

    ui_make_title(s_touch_scr, "Touch Screen Test", lv_color_hex(0x2C2C48));
    lv_obj_t *sub = lv_label_create(s_touch_scr);
    lv_label_set_text(sub, "Test all positions of touch screen");
    lv_obj_set_style_text_font(sub, UI_FONT_28, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 150);

    s_corners = lv_label_create(s_touch_scr);
    lv_label_set_text(s_corners, "Corners: 0 / 4");
    lv_obj_set_style_text_font(s_corners, UI_FONT_24, 0);
    lv_obj_align(s_corners, LV_ALIGN_TOP_MID, 0, 200);

    const int32_t far_x = UI_W - DOT_INSET - DOT_D, far_y = UI_H - DOT_INSET - DOT_D;
    const int32_t pos[4][2] = { { DOT_INSET, DOT_INSET }, { far_x, DOT_INSET }, { DOT_INSET, far_y }, { far_x, far_y } };
    for (int i = 0; i < 4; i++) {
        s_dots[i] = plain_obj(s_touch_scr);
        lv_obj_set_size(s_dots[i], DOT_D, DOT_D);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_pos(s_dots[i], pos[i][0], pos[i][1]);
    }

    /* X / Y read-out card on the right */
    lv_obj_t *card = plain_obj(s_touch_scr);
    lv_obj_set_size(card, 180, 130);
    lv_obj_align(card, LV_ALIGN_RIGHT_MID, -70, 40);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x161B8E), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, UI_COL_DARK, 0);
    s_tx = coord_row(card, "X", 18);
    s_ty = coord_row(card, "Y", 74);

    lv_obj_t *exit = ui_make_button(s_touch_scr, "Exit", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(exit, LV_ALIGN_BOTTOM_MID, 0, -UI_MARGIN);
    ui_add_nav(exit, UI_SCR_DIAG);

    /* the red cross (two bars), created last so it draws on top */
    s_cross = plain_obj(s_touch_scr);
    lv_obj_set_size(s_cross, CROSS_LEN, CROSS_LEN);
    for (int k = 0; k < 2; k++) {
        lv_obj_t *bar = plain_obj(s_cross);
        lv_obj_set_size(bar, k ? CROSS_W : CROSS_LEN, k ? CROSS_LEN : CROSS_W);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, UI_COL_DARK, 0);        /* black cross */
        lv_obj_center(bar);
    }

    ui_touch_on_enter();
    return s_touch_scr;
}

/* ====================================================================== Detailed SMTP Logging */
static lv_obj_t *s_smtp_off, *s_smtp_on;
static bool      s_smtp_sel;

static void smtp_show(void)
{
    ui_check_set(s_smtp_off, !s_smtp_sel);
    ui_check_set(s_smtp_on, s_smtp_sel);
}

static void smtp_pick_cb(lv_event_t *e) { s_smtp_sel = (bool)(intptr_t)lv_event_get_user_data(e); smtp_show(); }

static void smtp_save_cb(lv_event_t *e)
{
    (void)e;
    g_settings.smtp_detail = s_smtp_sel;
    settings_save();
    ui_go(UI_SCR_DIAG);
}

void ui_smtp_on_enter(void)
{
    s_smtp_sel = g_settings.smtp_detail != 0;
    smtp_show();
}

lv_obj_t *ui_smtp_create(void)
{
    lv_obj_t *scr = ui_make_screen(true);
    ui_make_title(scr, "Detailed SMTP Logging", lv_color_hex(0x2C2C48));

    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_remove_style_all(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(panel, 300, 150);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xD8DAF0), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, UI_COL_DARK, 0);
    lv_obj_set_style_pad_all(panel, 16, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 12, 0);

    s_smtp_off = ui_make_check(panel, "OFF");
    s_smtp_on  = ui_make_check(panel, "ON");
    lv_obj_t *opts[2] = { s_smtp_off, s_smtp_on };
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_text_color(lv_obj_get_child(opts[i], 1), UI_COL_DARK, 0);
        lv_obj_set_style_text_font(lv_obj_get_child(opts[i], 1), UI_FONT_28, 0);
        lv_obj_add_event_cb(opts[i], smtp_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    lv_obj_t *cancel = ui_make_button(scr, "Cancel", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    ui_add_nav(cancel, UI_SCR_DIAG);
    lv_obj_t *save = ui_make_button(scr, "Save", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(save, smtp_save_cb, LV_EVENT_CLICKED, NULL);

    ui_smtp_on_enter();
    return scr;
}
