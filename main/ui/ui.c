#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ui.h"
#include "settings.h"
#include "params.h"
#include "sensors.h"
#include "esp_log.h"

/* =================================================================== fonts
 * Wrap the built-in Montserrat fonts so that the digits 0-9 all use the same advance width
 * (like tabular figures). Timers and clocks then never shift as the numbers change.     */
lv_font_t ui_font_16, ui_font_20, ui_font_24, ui_font_28, ui_font_32, ui_font_36;

typedef struct { const lv_font_t *base; uint16_t digit_w; } mono_info_t;
static mono_info_t s_mono[6];

static bool mono_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t letter, uint32_t next)
{
    const mono_info_t *mi = font->user_data;
    if (!mi->base->get_glyph_dsc(mi->base, dsc, letter, next)) return false;
    if (letter >= '0' && letter <= '9') {
        int16_t extra = (int16_t)mi->digit_w - (int16_t)dsc->adv_w;
        dsc->ofs_x += extra / 2;
        dsc->adv_w = mi->digit_w;
    }
    return true;
}

static void mono_init(lv_font_t *dst, mono_info_t *mi, const lv_font_t *base)
{
    *dst = *base;
    mi->base = base;
    mi->digit_w = 0;
    for (uint32_t c = '0'; c <= '9'; c++) {
        lv_font_glyph_dsc_t g;
        if (base->get_glyph_dsc(base, &g, c, 0) && g.adv_w > mi->digit_w) mi->digit_w = g.adv_w;
    }
    dst->get_glyph_dsc = mono_glyph_dsc;
    dst->user_data = mi;
}

static void fonts_init(void)
{
    mono_init(&ui_font_16, &s_mono[5], &lv_font_montserrat_16);
    mono_init(&ui_font_20, &s_mono[0], &lv_font_montserrat_20);
    mono_init(&ui_font_24, &s_mono[1], &lv_font_montserrat_24);
    mono_init(&ui_font_28, &s_mono[2], &lv_font_montserrat_28);
    mono_init(&ui_font_32, &s_mono[3], &lv_font_montserrat_32);
    mono_init(&ui_font_36, &s_mono[4], &lv_font_montserrat_36);
}

/* =================================================================== styles */
static lv_style_t st_scr_blue, st_scr_lav, st_btn, st_btn_pr, st_field, st_field_pr;

static void styles_init(void)
{
    lv_style_init(&st_scr_blue);
    lv_style_set_bg_opa(&st_scr_blue, LV_OPA_COVER);
    lv_style_set_bg_color(&st_scr_blue, UI_COL_BG);
    lv_style_set_text_color(&st_scr_blue, UI_COL_TEXT);
    lv_style_set_text_font(&st_scr_blue, UI_FONT_24);
    lv_style_set_pad_all(&st_scr_blue, 0);

    lv_style_init(&st_scr_lav);
    lv_style_set_bg_opa(&st_scr_lav, LV_OPA_COVER);
    lv_style_set_bg_color(&st_scr_lav, UI_COL_BG_LAV);
    lv_style_set_text_color(&st_scr_lav, lv_color_hex(0x2C2C48));
    lv_style_set_text_font(&st_scr_lav, UI_FONT_24);
    lv_style_set_pad_all(&st_scr_lav, 0);

    lv_style_init(&st_btn);
    lv_style_set_bg_opa(&st_btn, LV_OPA_COVER);
    lv_style_set_bg_color(&st_btn, lv_color_hex(0xF8DE45));
    lv_style_set_bg_grad_color(&st_btn, lv_color_hex(0xD4AE12));
    lv_style_set_bg_grad_dir(&st_btn, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&st_btn, 3);
    lv_style_set_border_color(&st_btn, UI_COL_DARK);
    lv_style_set_outline_width(&st_btn, 3);
    lv_style_set_outline_color(&st_btn, lv_color_hex(0xD9DFF5));
    lv_style_set_outline_pad(&st_btn, 0);
    lv_style_set_radius(&st_btn, 12);
    lv_style_set_shadow_width(&st_btn, 10);
    lv_style_set_shadow_offset_y(&st_btn, 4);
    lv_style_set_shadow_color(&st_btn, lv_color_black());
    lv_style_set_shadow_opa(&st_btn, LV_OPA_40);
    lv_style_set_text_color(&st_btn, UI_COL_DARK);
    lv_style_set_pad_all(&st_btn, 4);

    lv_style_init(&st_btn_pr);
    lv_style_set_bg_color(&st_btn_pr, lv_color_hex(0xCFAA0E));
    lv_style_set_bg_grad_color(&st_btn_pr, lv_color_hex(0xAD8A00));
    lv_style_set_shadow_offset_y(&st_btn_pr, 1);

    lv_style_init(&st_field);
    lv_style_set_bg_opa(&st_field, LV_OPA_COVER);
    lv_style_set_bg_color(&st_field, UI_COL_YELLOW);
    lv_style_set_text_color(&st_field, UI_COL_DARK);
    lv_style_set_pad_left(&st_field, 12);

    lv_style_init(&st_field_pr);
    lv_style_set_bg_color(&st_field_pr, lv_color_hex(0xC9A80C));
}

/* =================================================================== helpers */
lv_obj_t *ui_make_screen(bool lavender)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, lavender ? &st_scr_lav : &st_scr_blue, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

lv_obj_t *ui_make_title(lv_obj_t *parent, const char *txt, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, UI_FONT_36, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 10);
    return l;
}

void ui_fit_label(lv_obj_t *label, int32_t max_w)
{
    const lv_font_t *fonts[] = { UI_FONT_36, UI_FONT_32, UI_FONT_28, UI_FONT_24, UI_FONT_20 };
    const char *txt = lv_label_get_text(label);
    const lv_font_t *font = UI_FONT_20;
    for (unsigned i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        lv_point_t sz;
        lv_text_get_size(&sz, txt, fonts[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x <= max_w) { font = fonts[i]; break; }
    }
    lv_obj_set_style_text_font(label, font, 0);
}

lv_obj_t *ui_make_title_bar(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(bar, UI_W, 52);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, UI_COL_YELLOW, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 2, 0);
    lv_obj_set_style_border_color(bar, UI_COL_DARK, 0);
    lv_obj_t *l = lv_label_create(bar);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, UI_COL_DARK, 0);
    lv_obj_set_style_text_font(l, UI_FONT_32, 0);
    lv_obj_center(l);
    return l;
}

lv_obj_t *ui_make_button(lv_obj_t *parent, const char *txt, int32_t w, int32_t h, const lv_font_t *font)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);          /* no default-theme press/grow animations */
    lv_obj_add_style(b, &st_btn, 0);
    lv_obj_add_style(b, &st_btn_pr, LV_STATE_PRESSED);
    lv_obj_set_size(b, w, h);
    if (txt) {
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_font(l, font ? font : UI_FONT_28, 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
    }
    return b;
}

/* Pick the largest font that fits on one line; otherwise wrap in the smallest. */
lv_obj_t *ui_make_menu_button(lv_obj_t *parent, const char *txt, int32_t w)
{
    lv_obj_t *b = ui_make_button(parent, NULL, w, UI_BTN_H, NULL);
    const int32_t max_w = w - 32;
    const lv_font_t *fonts[] = { UI_FONT_32, UI_FONT_28, UI_FONT_24, UI_FONT_20 };
    const lv_font_t *font = UI_FONT_20;
    for (unsigned i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        lv_point_t sz;
        lv_text_get_size(&sz, txt, fonts[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x <= max_w) { font = fonts[i]; break; }
    }
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, max_w);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, 0);
    return b;
}

/* Same button, but the "i" sits in a thin black ring with the yellow button showing inside
 * (used on the Idle/info screen itself). */
lv_obj_t *ui_make_info_button_outline(lv_obj_t *parent)
{
    lv_obj_t *b = ui_make_button(parent, NULL, UI_BTN_H, UI_BTN_H, NULL);
    lv_obj_t *c = lv_obj_create(b);
    lv_obj_remove_style_all(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(c, 36, 36);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_color(c, UI_COL_DARK, 0);
    lv_obj_center(c);
    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, "i");
    lv_obj_set_style_text_font(l, UI_FONT_28, 0);
    lv_obj_set_style_text_color(l, UI_COL_DARK, 0);
    lv_obj_center(l);
    return b;
}

lv_obj_t *ui_make_info_button(lv_obj_t *parent)
{
    lv_obj_t *b = ui_make_button(parent, NULL, UI_BTN_H, UI_BTN_H, NULL);
    lv_obj_t *c = lv_obj_create(b);
    lv_obj_remove_style_all(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(c, 36, 36);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, UI_COL_DARK, 0);
    lv_obj_center(c);
    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, "i");
    lv_obj_set_style_text_font(l, UI_FONT_28, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_center(l);
    return b;
}

#define MAX_BELLS 4
static lv_obj_t *s_bell_lbls[MAX_BELLS];
static int s_bell_n;

lv_obj_t *ui_make_bell_button(lv_obj_t *parent)
{
    lv_obj_t *b = ui_make_button(parent, LV_SYMBOL_BELL, UI_BTN_H, UI_BTN_H, UI_FONT_32);
    if (s_bell_n < MAX_BELLS) s_bell_lbls[s_bell_n++] = lv_obj_get_child(b, 0);
    ui_add_nav(b, UI_SCR_ALARMS);
    return b;
}

void ui_update_bells(bool alarm_active)
{
    for (int i = 0; i < s_bell_n; i++)
        lv_obj_set_style_text_color(s_bell_lbls[i], alarm_active ? UI_COL_RED : UI_COL_DARK, 0);
}

lv_obj_t *ui_make_fingerprint(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(c, 46, 46);

    static const struct { int16_t r, a0, a1; } arcs[] = {
        {  5, 120,  60 }, { 10, 150,  30 }, { 15, 165,  15 }, { 20, 185, 355 },
    };
    for (unsigned i = 0; i < sizeof(arcs) / sizeof(arcs[0]); i++) {
        lv_obj_t *a = lv_arc_create(c);
        lv_obj_remove_style(a, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(a, arcs[i].r * 2, arcs[i].r * 2);
        lv_obj_center(a);
        lv_arc_set_bg_angles(a, arcs[i].a0, arcs[i].a1);
        lv_obj_set_style_pad_all(a, 0, 0);
        lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
        lv_obj_set_style_arc_width(a, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_color(a, UI_COL_TEXT, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
    }
    return c;
}

/* ---- STOP button (option B): round, same light ring + black border as the yellow buttons,
 *      red face darker toward the bottom with a soft glossy highlight on the top half,
 *      bold white "STOP" across the middle. Rendered once (anti-aliased) into a shared
 *      buffer; every Stop button uses it. ---- */
#define STOP_SZ 140
static lv_draw_buf_t *s_stop_buf;

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static bool stop_shade(float x, float y, float R, float *cr, float *cg, float *cb)
{
    float r = sqrtf(x * x + y * y);
    if (r > R) return false;
    if (r > R - 4) { *cr = 0xD9; *cg = 0xDF; *cb = 0xF5; return true; }   /* light outer ring */
    if (r > R - 7) { *cr = 0x15; *cg = 0x15; *cb = 0x15; return true; }   /* black border     */

    float ri = R - 7, t = (y + ri) / (2 * ri);                             /* 0 top .. 1 bottom */
    float rr = lerpf(232, 160, t), gg = lerpf(52, 16, t), bb = lerpf(52, 24, t);
    float hx = x / ri, hy = (y + ri * 0.45f) / (ri * 0.55f);                /* glossy highlight */
    float d = hx * hx * 1.2f + hy * hy;
    if (d < 1) {
        float k = 0.45f * (1 - d);
        rr = lerpf(rr, 255, k); gg = lerpf(gg, 170, k); bb = lerpf(bb, 170, k);
    }
    *cr = rr; *cg = gg; *cb = bb;
    return true;
}

static void stop_buf_render(void)
{
    s_stop_buf = lv_draw_buf_create(STOP_SZ, STOP_SZ, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    if (!s_stop_buf) return;

    const float c = STOP_SZ / 2.0f, R = c - 1.0f;
    for (int y = 0; y < STOP_SZ; y++) {
        uint8_t *row = s_stop_buf->data + y * s_stop_buf->header.stride;
        for (int x = 0; x < STOP_SZ; x++) {
            float sr = 0, sg = 0, sb = 0;
            int hits = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    float r, g, b;
                    if (stop_shade(x + (sx + 0.5f) / 4.0f - c, y + (sy + 0.5f) / 4.0f - c, R, &r, &g, &b)) {
                        sr += r; sg += g; sb += b; hits++;
                    }
                }
            }
            uint8_t *p = row + x * 4;                        /* ARGB8888 in memory: B, G, R, A */
            if (!hits) { p[0] = p[1] = p[2] = p[3] = 0; continue; }
            p[0] = (uint8_t)(sb / hits);
            p[1] = (uint8_t)(sg / hits);
            p[2] = (uint8_t)(sr / hits);
            p[3] = (uint8_t)(hits * 255 / 16);
        }
    }
}

lv_obj_t *ui_make_stop_button(lv_obj_t *parent)
{
    if (!s_stop_buf) stop_buf_render();
    lv_obj_t *img = lv_image_create(parent);
    if (s_stop_buf) lv_image_set_src(img, s_stop_buf);
    lv_obj_set_size(img, STOP_SZ, STOP_SZ);
    lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_image_recolor(img, lv_color_black(), LV_STATE_PRESSED);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_30, LV_STATE_PRESSED);

    /* thin dark-red shadow behind the white text so it stays crisp on the highlight */
    lv_obj_t *sh = lv_label_create(img);
    lv_label_set_text(sh, "STOP");
    lv_obj_set_style_text_font(sh, UI_FONT_32, 0);
    lv_obj_set_style_text_color(sh, lv_color_hex(0x6E0A0F), 0);
    lv_obj_align(sh, LV_ALIGN_CENTER, 1, 3);

    lv_obj_t *l = lv_label_create(img);
    lv_label_set_text(l, "STOP");
    lv_obj_set_style_text_font(l, UI_FONT_32, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 2);
    return img;
}

lv_obj_t *ui_make_field(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *f = lv_obj_create(parent);
    lv_obj_remove_style_all(f);
    lv_obj_add_style(f, &st_field, 0);
    lv_obj_add_style(f, &st_field_pr, LV_STATE_PRESSED);
    lv_obj_remove_flag(f, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(f, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(f, w, h);
    lv_obj_t *l = lv_label_create(f);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, UI_FONT_28, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    return f;
}

/* ---- checkbox: green square + label, whole row is tappable ---- */
static void check_refresh(lv_obj_t *chk)
{
    lv_obj_t *box = lv_obj_get_child(chk, 0);
    bool on = lv_obj_has_state(chk, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(box, on ? UI_COL_GREEN : UI_COL_NAVY, 0);
}

static void check_click_cb(lv_event_t *e)
{
    lv_obj_t *chk = lv_event_get_current_target(e);
    if (lv_obj_has_state(chk, LV_STATE_CHECKED)) lv_obj_remove_state(chk, LV_STATE_CHECKED);
    else                                          lv_obj_add_state(chk, LV_STATE_CHECKED);
    check_refresh(chk);
}

lv_obj_t *ui_make_check(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 44);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 14, 0);

    lv_obj_t *box = lv_obj_create(row);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, 30, 30);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 3, 0);
    lv_obj_set_style_border_color(box, UI_COL_TEXT, 0);
    lv_obj_set_style_radius(box, 4, 0);

    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, UI_FONT_24, 0);
    lv_obj_set_style_text_color(l, UI_COL_TITLE_Y, 0);

    lv_obj_add_event_cb(row, check_click_cb, LV_EVENT_CLICKED, NULL);
    check_refresh(row);
    return row;
}

void ui_check_set(lv_obj_t *chk, bool on)
{
    if (on) lv_obj_add_state(chk, LV_STATE_CHECKED);
    else    lv_obj_remove_state(chk, LV_STATE_CHECKED);
    check_refresh(chk);
}

bool ui_check_get(lv_obj_t *chk) { return lv_obj_has_state(chk, LV_STATE_CHECKED); }

/* =================================================================== status bar + clock */
#define MAX_BARS 12
static lv_obj_t *s_clock_lbls[MAX_BARS];
static int s_clock_n;
static lv_obj_t *s_status_lbls[MAX_BARS];
static int s_status_n;
static char s_status[32] = "Ready";

void ui_format_datetime(char *buf, size_t len, time_t utc, bool seconds)
{
    time_t t = utc + settings_utc_offset_s();
    struct tm tm;
    gmtime_r(&t, &tm);

    uint8_t fmt = g_settings.date_fmt;
    bool h12 = (fmt == DATEFMT_MDY12 || fmt == DATEFMT_DMY12);
    int h = tm.tm_hour;
    const char *ampm = "";
    if (h12) { ampm = h >= 12 ? " PM" : " AM"; h %= 12; if (h == 0) h = 12; }

    char d[32], tm_s[32];
    switch (fmt) {
    case DATEFMT_DMY24: case DATEFMT_DMY12:
        snprintf(d, sizeof(d), "%d/%d/%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year % 100); break;
    case DATEFMT_YMD24:
        snprintf(d, sizeof(d), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday); break;
    default:
        snprintf(d, sizeof(d), "%d/%d/%02d", tm.tm_mon + 1, tm.tm_mday, tm.tm_year % 100); break;
    }
    if (seconds) snprintf(tm_s, sizeof(tm_s), "%02d:%02d:%02d%s", h, tm.tm_min, tm.tm_sec, ampm);
    else         snprintf(tm_s, sizeof(tm_s), "%02d:%02d%s", h, tm.tm_min, ampm);
    snprintf(buf, len, "%s %s", d, tm_s);
}

static void clock_tick(lv_timer_t *t)
{
    (void)t;
    char buf[72];
    ui_format_datetime(buf, sizeof(buf), time(NULL), true);
    for (int i = 0; i < s_clock_n; i++) lv_label_set_text(s_clock_lbls[i], buf);
}

static lv_obj_t *make_bar(lv_obj_t *scr, int32_t w)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(bar, w, UI_STATUS_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, UI_COL_YELLOW, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(bar, 2, 0);
    lv_obj_set_style_border_color(bar, UI_COL_DARK, 0);

    lv_obj_t *clk = lv_label_create(bar);
    lv_obj_set_style_text_font(clk, UI_FONT_28, 0);
    lv_obj_set_style_text_color(clk, UI_COL_DARK, 0);
    lv_obj_align(clk, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_label_set_text(clk, "");
    if (s_clock_n < MAX_BARS) s_clock_lbls[s_clock_n++] = clk;
    return bar;
}

void ui_make_status_bar(lv_obj_t *scr)
{
    lv_obj_t *bar = make_bar(scr, UI_W);
    lv_obj_t *st = lv_label_create(bar);
    lv_obj_set_style_text_font(st, UI_FONT_28, 0);
    lv_obj_set_style_text_color(st, UI_COL_DARK, 0);
    lv_obj_align(st, LV_ALIGN_LEFT_MID, 12, 0);
    lv_label_set_text_fmt(st, "Status: %s", s_status);
    if (s_status_n < MAX_BARS) s_status_lbls[s_status_n++] = st;
}

void ui_make_clock_bar(lv_obj_t *scr) { make_bar(scr, UI_W / 2); }

void ui_idle_set_title(const char *status);  /* ui_idle.c */

void ui_set_system_status(const char *status)
{
    snprintf(s_status, sizeof(s_status), "%s", status);
    for (int i = 0; i < s_status_n; i++) lv_label_set_text_fmt(s_status_lbls[i], "Status: %s", s_status);
    ui_idle_set_title(s_status);
}

const char *ui_get_system_status(void) { return s_status; }

/* =================================================================== navigation */
static const struct {
    lv_obj_t *(*create)(void);
    void (*enter)(void);
} k_screens[UI_SCR_COUNT] = {
    [UI_SCR_HOME]     = { ui_home_create,     NULL },
    [UI_SCR_IDLE]     = { ui_idle_create,     NULL },
    [UI_SCR_PASSCODE] = { ui_passcode_create, ui_passcode_on_enter },
    [UI_SCR_CHPASS]   = { ui_chpass_create,   ui_chpass_on_enter },
    [UI_SCR_MAINT]    = { ui_maint_create,    NULL },
    [UI_SCR_SETUP]    = { ui_setup_create,    NULL },
    [UI_SCR_VIEWINFO] = { ui_viewinfo_create, NULL },
    [UI_SCR_INFO]     = { ui_info_create,     ui_info_on_enter },
    [UI_SCR_PARAMS]   = { ui_params_create,   ui_params_on_enter },
    [UI_SCR_NETWORK]  = { ui_network_create,  NULL },
    [UI_SCR_IPCFG]    = { ui_ipcfg_create,    ui_ipcfg_on_enter },
    [UI_SCR_WEBCFG]   = { ui_webcfg_create,   ui_webcfg_on_enter },
    [UI_SCR_DATETIME] = { ui_datetime_create, ui_datetime_on_enter },
    [UI_SCR_ALARMS]   = { ui_alarms_create,   ui_alarms_on_enter },
    [UI_SCR_ADVANCED] = { ui_advanced_create, NULL },
    [UI_SCR_DIAG]     = { ui_diag_create,     NULL },
    [UI_SCR_FILTER]   = { ui_filter_create,   NULL },
    [UI_SCR_REVIVE]   = { ui_revive_create,   NULL },
    [UI_SCR_STOPPING] = { ui_stopping_create, NULL },
    [UI_SCR_PARAM_EDIT] = { ui_param_edit_create, ui_param_edit_on_enter },
    [UI_SCR_FILTERMODE] = { ui_filtermode_create, NULL },
    [UI_SCR_DISPLAY]    = { ui_display_create,    ui_display_on_enter },
    [UI_SCR_MODEL]      = { ui_model_create,      ui_model_on_enter },
    [UI_SCR_MESSAGE]    = { ui_message_create,    NULL },
    [UI_SCR_ANALOG]     = { ui_analog_create,     ui_analog_on_enter },
    [UI_SCR_CALIBRATE]  = { ui_calibrate_create,  ui_calibrate_on_enter },
    [UI_SCR_CAL_EDIT]   = { ui_cal_edit_create,   ui_cal_edit_on_enter },
    [UI_SCR_SERVICE_IO] = { ui_service_create,    ui_service_on_enter },
    [UI_SCR_COLOR]      = { ui_color_create,      ui_color_on_enter },
    [UI_SCR_TOUCH]      = { ui_touch_create,      ui_touch_on_enter },
    [UI_SCR_SMTP]       = { ui_smtp_create,       ui_smtp_on_enter },
    [UI_SCR_SPLASH]     = { ui_splash_create,     NULL },
    [UI_SCR_DRAIN]      = { ui_drain_create,      NULL },
};

static lv_obj_t      *s_screens[UI_SCR_COUNT];
static ui_screen_id_t s_caller[UI_SCR_COUNT];
static ui_screen_id_t s_cur = UI_SCR_HOME;

void ui_go(ui_screen_id_t id)
{
    /* While the program runs a sequence, "Home" is that sequence's screen */
    if (id == UI_SCR_HOME) id = ui_seq_home_screen();

    if (id != s_cur) s_caller[id] = s_cur;
    if (k_screens[id].enter) k_screens[id].enter();
    s_cur = id;
    lv_screen_load(s_screens[id]);
}

ui_screen_id_t ui_caller(ui_screen_id_t id) { return s_caller[id]; }

bool ui_screen_is(ui_screen_id_t id) { return s_screens[id] && lv_screen_active() == s_screens[id]; }

static void nav_cb(lv_event_t *e)
{
    ui_go((ui_screen_id_t)(intptr_t)lv_event_get_user_data(e));
}

void ui_add_nav(lv_obj_t *btn, ui_screen_id_t target)
{
    lv_obj_add_event_cb(btn, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)target);
}

/* Parameter 33: go back to Home after this many seconds without a touch on a menu screen. */
static void idle_exit_tick(lv_timer_t *t)
{
    (void)t;
    float secs = param_get(P_IDLE_EXIT_S);
    if (secs <= 0) return;
    switch (s_cur) {
    case UI_SCR_HOME: case UI_SCR_IDLE: case UI_SCR_FILTER: case UI_SCR_REVIVE: case UI_SCR_STOPPING:
    case UI_SCR_FILTERMODE: case UI_SCR_SPLASH: case UI_SCR_DRAIN:
        return;
    default:
        break;
    }
    if (lv_display_get_inactive_time(NULL) >= (uint32_t)(secs * 1000.0f)) ui_go(UI_SCR_HOME);
}

/* Sensor simulator step + refresh the screens that show live readings. */
static void sensor_tick(lv_timer_t *t)
{
    (void)t;
    sensors_tick();
    ui_idle_refresh_sensors();
    ui_analog_refresh();
    ui_service_refresh();
}

void ui_spread_row(lv_obj_t *const objs[], int n, int32_t y)
{
    int32_t total = 0;
    for (int i = 0; i < n; i++) total += lv_obj_get_style_width(objs[i], LV_PART_MAIN);
    int32_t gap = n > 1 ? (UI_W - 2 * UI_MARGIN - total) / (n - 1) : 0;
    int32_t x = UI_MARGIN;
    for (int i = 0; i < n; i++) {
        lv_obj_set_pos(objs[i], n == 1 ? (UI_W - lv_obj_get_style_width(objs[i], LV_PART_MAIN)) / 2 : x, y);
        x += lv_obj_get_style_width(objs[i], LV_PART_MAIN) + gap;
    }
}

void ui_init(void)
{
    fonts_init();
    styles_init();

    for (int i = 0; i < UI_SCR_COUNT; i++) {
        s_screens[i] = k_screens[i].create();
        s_caller[i] = UI_SCR_HOME;
    }

    lv_timer_create(clock_tick, 1000, NULL);
    lv_timer_create(idle_exit_tick, 1000, NULL);
    lv_timer_create(sensor_tick, 500, NULL);
    clock_tick(NULL);
    ui_set_system_status("Ready");
    ui_update_bells(false);

    ESP_LOGI("ui", "Startup screen: %s", param_get(P_SHOW_STARTUP) != 0 ? "show" : "skip");
    if (param_get(P_SHOW_STARTUP) == 0) {          /* parameter 34 OFF: straight to Home */
        s_cur = UI_SCR_HOME;
        lv_screen_load(s_screens[UI_SCR_HOME]);
    } else {                                       /* power-up: logo + bubbles, then Home */
        s_cur = UI_SCR_SPLASH;
        lv_screen_load(s_screens[UI_SCR_SPLASH]);
        ui_splash_start();
    }
}
