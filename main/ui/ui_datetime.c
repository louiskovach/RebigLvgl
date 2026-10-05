/*
 * Date & Time
 *
 *   [ live preview: "Tuesday, September 29, 2026  ->  14:05" ]
 *   +------ Date ------+  +---- Time ----+  +- Time Zone -+
 *   | Month| Day| Year |  | Hour : Min   |  |   UTC-5     |     <- scroll wheels
 *   +------------------+  +--------------+  +-------------+
 *   Format [ M-D-Y (24) ]        [x] Daylight Savings
 *   [x] Auto Network Time    <network time status>
 *   [Cancel]                                     [Save]
 *
 * The clock is kept in UTC (system time + RTC). If the wheels are not touched, Save keeps the
 * running time and only applies the time zone / DST / format settings.
 * With Auto Network Time on, the date/time wheels are locked (the time comes from the network).
 */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "ui.h"
#include "settings.h"
#include "board_periph.h"
#include "net_time.h"

#define YEAR_MIN   2020
#define YEAR_MAX   2099
#define CARD_Y     90
#define CARD_H     166
#define WHEEL_Y    34
#define WHEEL_ROWS 3

static lv_obj_t *s_scr, *s_preview;
static lv_obj_t *s_mon, *s_day, *s_year, *s_hour, *s_min, *s_zone;
static lv_obj_t *s_fmt_btn, *s_dst, *s_auto, *s_net_lbl;
static int8_t    s_tz;
static uint8_t   s_fmt;
static bool      s_edited;      /* wheels touched -> Save sets the clock */
static bool      s_loading;     /* ignore VALUE_CHANGED while we set wheels ourselves */
static lv_style_t st_wheel, st_wheel_sel, st_card;

static char s_year_opts[(YEAR_MAX - YEAR_MIN + 1) * 5];
static char s_day_opts[31 * 3];
static char s_hour_opts[24 * 6];
static char s_min_opts[60 * 3];
static char s_zone_opts[27 * 8];
static int  s_day_count;

static const char *const k_months = "Jan\nFeb\nMar\nApr\nMay\nJun\nJul\nAug\nSep\nOct\nNov\nDec";
static const char *const k_mon_long[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December",
};
static const char *const k_wday[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
};

static bool fmt_is_12h(uint8_t f) { return f == DATEFMT_MDY12 || f == DATEFMT_DMY12; }

static int days_in_month(int mon0, int year)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (mon0 == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return d[mon0];
}

/* ------------------------------------------------------------------ option strings */
static void build_static_opts(void)
{
    char *p = s_year_opts;
    for (int y = YEAR_MIN; y <= YEAR_MAX; y++) p += sprintf(p, y == YEAR_MIN ? "%d" : "\n%d", y);
    p = s_min_opts;
    for (int m = 0; m < 60; m++) p += sprintf(p, m ? "\n%02d" : "%02d", m);
    p = s_zone_opts;
    for (int z = -12; z <= 14; z++) {
        if (z != -12) *p++ = '\n';
        if (z == 0) p += sprintf(p, "UTC");
        else        p += sprintf(p, "UTC%+d", z);
    }
}

static void build_hour_opts(bool h12)
{
    char *p = s_hour_opts;
    for (int h = 0; h < 24; h++) {
        if (h12) {
            int hh = h % 12 ? h % 12 : 12;
            p += sprintf(p, h ? "\n%d %s" : "%d %s", hh, h < 12 ? "AM" : "PM");
        } else {
            p += sprintf(p, h ? "\n%02d" : "%02d", h);
        }
    }
}

static void set_day_count(int n)
{
    if (n == s_day_count) return;
    uint32_t sel = s_day_count ? lv_roller_get_selected(s_day) : 0;
    char *p = s_day_opts;
    for (int d = 1; d <= n; d++) p += sprintf(p, d > 1 ? "\n%d" : "%d", d);
    s_day_count = n;
    lv_roller_set_options(s_day, s_day_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(s_day, sel < (uint32_t)n ? sel : (uint32_t)n - 1, LV_ANIM_OFF);
}

/* ------------------------------------------------------------------ read / show */
static void read_wheels(struct tm *tm)
{
    memset(tm, 0, sizeof(*tm));
    tm->tm_year = YEAR_MIN + (int)lv_roller_get_selected(s_year) - 1900;
    tm->tm_mon  = (int)lv_roller_get_selected(s_mon);
    tm->tm_mday = (int)lv_roller_get_selected(s_day) + 1;
    tm->tm_hour = (int)lv_roller_get_selected(s_hour);
    tm->tm_min  = (int)lv_roller_get_selected(s_min);
}

static int32_t pending_offset(void)
{
    return (int32_t)s_tz * 3600 + (ui_check_get(s_dst) ? 3600 : 0);
}

static bool auto_on(void) { return ui_check_get(s_auto); }

/* Auto Network Time locks the date/time wheels */
static void apply_lock(void)
{
    lv_obj_t *w[] = { s_mon, s_day, s_year, s_hour, s_min };
    bool lock = auto_on();
    for (unsigned i = 0; i < sizeof(w) / sizeof(w[0]); i++) {
        if (lock) lv_obj_add_state(w[i], LV_STATE_DISABLED);
        else      lv_obj_remove_state(w[i], LV_STATE_DISABLED);
    }
    if (lock) s_edited = false;
    lv_label_set_text(s_net_lbl, lock ? net_time_status() : "");
}

static void update_preview(void)
{
    struct tm tm;
    read_wheels(&tm);
    time_t t = mktime(&tm);          /* TZ unset -> UTC math; only used for the weekday */
    gmtime_r(&t, &tm);

    char date[40], tim[16];
    switch (s_fmt) {
    case DATEFMT_DMY24: case DATEFMT_DMY12:
        snprintf(date, sizeof(date), "%d %s %d", tm.tm_mday, k_mon_long[tm.tm_mon], tm.tm_year + 1900); break;
    case DATEFMT_YMD24:
        snprintf(date, sizeof(date), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday); break;
    default:
        snprintf(date, sizeof(date), "%s %d, %d", k_mon_long[tm.tm_mon], tm.tm_mday, tm.tm_year + 1900); break;
    }
    if (fmt_is_12h(s_fmt)) {
        int h = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;
        snprintf(tim, sizeof(tim), "%d:%02d %s", h, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
    } else {
        snprintf(tim, sizeof(tim), "%02d:%02d", tm.tm_hour, tm.tm_min);
    }
    lv_label_set_text_fmt(s_preview, "%s,  %s   " LV_SYMBOL_RIGHT "   %s", k_wday[tm.tm_wday], date, tim);
    lv_label_set_text(lv_obj_get_child(s_fmt_btn, 0), settings_date_fmt_name(s_fmt));
}

static void set_roller(lv_obj_t *r, uint32_t v)
{
    if (lv_roller_get_selected(r) != v) lv_roller_set_selected(r, v, LV_ANIM_OFF);
}

/* Put the running clock (with the pending time zone) on the wheels. */
static void load_now(void)
{
    time_t t = time(NULL) + pending_offset();
    struct tm tm;
    gmtime_r(&t, &tm);
    int year = tm.tm_year + 1900;
    if (year < YEAR_MIN) year = YEAR_MIN;
    if (year > YEAR_MAX) year = YEAR_MAX;

    s_loading = true;
    set_roller(s_year, (uint32_t)(year - YEAR_MIN));
    set_roller(s_mon,  (uint32_t)tm.tm_mon);
    set_day_count(days_in_month(tm.tm_mon, year));
    set_roller(s_day,  (uint32_t)tm.tm_mday - 1);
    set_roller(s_hour, (uint32_t)tm.tm_hour);
    set_roller(s_min,  (uint32_t)tm.tm_min);
    s_loading = false;
}

/* ------------------------------------------------------------------ events */
static void settings_changed(void);

static void wheel_cb(lv_event_t *e)
{
    if (s_loading) return;
    lv_obj_t *r = lv_event_get_target(e);
    if (r == s_zone) {
        s_tz = (int8_t)((int)lv_roller_get_selected(s_zone) - 12);
        settings_changed();
        return;
    }
    s_edited = true;
    if (r == s_mon || r == s_year) {
        int year = YEAR_MIN + (int)lv_roller_get_selected(s_year);
        set_day_count(days_in_month((int)lv_roller_get_selected(s_mon), year));
    }
    update_preview();
}

static void settings_changed(void)
{
    if (!s_edited) load_now();       /* show the real time in the new zone */
    update_preview();
}

static void dst_cb(lv_event_t *e)  { (void)e; settings_changed(); }
static void auto_cb(lv_event_t *e) { (void)e; apply_lock(); settings_changed(); }

static void fmt_cb(lv_event_t *e)
{
    (void)e;
    s_fmt = (uint8_t)((s_fmt + 1) % DATEFMT_COUNT);
    uint32_t h = lv_roller_get_selected(s_hour);
    build_hour_opts(fmt_is_12h(s_fmt));
    s_loading = true;
    lv_roller_set_options(s_hour, s_hour_opts, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_selected(s_hour, h, LV_ANIM_OFF);
    s_loading = false;
    update_preview();
}

/* keep the wheels following the clock until the user touches them */
static void live_tick(lv_timer_t *t)
{
    (void)t;
    if (lv_screen_active() != s_scr) return;
    if (auto_on()) lv_label_set_text(s_net_lbl, net_time_status());
    if (s_edited) return;
    load_now();
    update_preview();
}

static void save_cb(lv_event_t *e)
{
    (void)e;
    g_settings.tz_hours = s_tz;
    g_settings.dst = ui_check_get(s_dst);
    g_settings.auto_net_time = ui_check_get(s_auto);
    g_settings.date_fmt = s_fmt;
    settings_save();
    net_time_apply(g_settings.auto_net_time);

    if (s_edited && !g_settings.auto_net_time) {
        struct tm tm;
        read_wheels(&tm);
        time_t utc = mktime(&tm) - settings_utc_offset_s();
        struct timeval tv = { .tv_sec = utc };
        settimeofday(&tv, NULL);
        board_rtc_write(utc);
    }
    ui_go(UI_SCR_SETUP);
}

void ui_datetime_on_enter(void)
{
    s_tz = g_settings.tz_hours;
    s_fmt = g_settings.date_fmt;
    s_edited = false;
    ui_check_set(s_dst, g_settings.dst);
    ui_check_set(s_auto, g_settings.auto_net_time);
    s_loading = true;
    lv_roller_set_selected(s_zone, (uint32_t)(s_tz + 12), LV_ANIM_OFF);
    s_loading = false;
    apply_lock();

    build_hour_opts(fmt_is_12h(s_fmt));
    s_loading = true;
    lv_roller_set_options(s_hour, s_hour_opts, LV_ROLLER_MODE_INFINITE);
    s_loading = false;
    load_now();
    update_preview();
}

/* ------------------------------------------------------------------ build */
static lv_obj_t *make_card(lv_obj_t *parent, const char *title, int32_t x, int32_t w)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_add_style(c, &st_card, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(c, x, CARD_Y);
    lv_obj_set_size(c, w, CARD_H);

    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, title);
    lv_obj_set_style_text_font(l, UI_FONT_24, 0);
    lv_obj_set_style_text_color(l, UI_COL_TITLE_Y, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 4);
    return c;
}

static lv_obj_t *make_wheel(lv_obj_t *card, const char *opts, lv_roller_mode_t mode, int32_t x, int32_t w)
{
    lv_obj_t *r = lv_roller_create(card);
    lv_obj_add_style(r, &st_wheel, 0);
    lv_obj_add_style(r, &st_wheel_sel, LV_PART_SELECTED);
    lv_roller_set_options(r, opts, mode);
    lv_roller_set_visible_row_count(r, WHEEL_ROWS);
    lv_obj_set_width(r, w);
    lv_obj_set_pos(r, x, WHEEL_Y);
    lv_obj_set_style_opa(r, LV_OPA_40, LV_STATE_DISABLED);    /* locked by Auto Network Time */
    lv_obj_add_event_cb(r, wheel_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return r;
}

static void styles_init(void)
{
    lv_style_init(&st_card);
    lv_style_set_bg_opa(&st_card, LV_OPA_COVER);
    lv_style_set_bg_color(&st_card, lv_color_hex(0x161B8E));
    lv_style_set_border_width(&st_card, 2);
    lv_style_set_border_color(&st_card, lv_color_hex(0x8C94E0));
    lv_style_set_radius(&st_card, 14);

    lv_style_init(&st_wheel);
    lv_style_set_bg_opa(&st_wheel, LV_OPA_COVER);
    lv_style_set_bg_color(&st_wheel, lv_color_hex(0x0B0F66));
    lv_style_set_border_width(&st_wheel, 2);
    lv_style_set_border_color(&st_wheel, lv_color_hex(0x5A61C8));
    lv_style_set_radius(&st_wheel, 10);
    lv_style_set_text_font(&st_wheel, UI_FONT_28);
    lv_style_set_text_color(&st_wheel, lv_color_hex(0x8F95D8));
    lv_style_set_text_align(&st_wheel, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_line_space(&st_wheel, 8);
    lv_style_set_pad_all(&st_wheel, 2);

    lv_style_init(&st_wheel_sel);
    lv_style_set_bg_opa(&st_wheel_sel, LV_OPA_COVER);
    lv_style_set_bg_color(&st_wheel_sel, UI_COL_YELLOW);
    lv_style_set_text_color(&st_wheel_sel, UI_COL_DARK);
    lv_style_set_text_font(&st_wheel_sel, UI_FONT_28);
}

lv_obj_t *ui_datetime_create(void)
{
    styles_init();
    build_static_opts();
    build_hour_opts(false);

    s_scr = ui_make_screen(false);
    ui_make_title(s_scr, "Date & Time", UI_COL_TEXT);

    s_preview = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_preview, UI_FONT_24, 0);
    lv_obj_set_style_text_color(s_preview, UI_COL_TITLE_Y, 0);
    lv_obj_align(s_preview, LV_ALIGN_TOP_MID, 0, 56);

    /* three cards: Date | Time | Time Zone */
    const int32_t date_w = 330, time_w = 220;
    const int32_t time_x = UI_MARGIN + date_w + UI_GAP;
    const int32_t zone_x = time_x + time_w + UI_GAP, zone_w = UI_W - UI_MARGIN - zone_x;

    lv_obj_t *dc = make_card(s_scr, "Date", UI_MARGIN, date_w);
    s_mon  = make_wheel(dc, k_months, LV_ROLLER_MODE_INFINITE, 12, 110);
    s_day  = make_wheel(dc, "1", LV_ROLLER_MODE_NORMAL, 12 + 110 + 8, 76);
    s_year = make_wheel(dc, s_year_opts, LV_ROLLER_MODE_NORMAL, 12 + 110 + 8 + 76 + 8, 104);
    set_day_count(31);

    lv_obj_t *tc = make_card(s_scr, "Time", time_x, time_w);
    const int32_t hw = 104, mw = 76, colon = 16;
    const int32_t tx = (time_w - (hw + colon + mw)) / 2;
    s_hour = make_wheel(tc, s_hour_opts, LV_ROLLER_MODE_INFINITE, tx, hw);
    lv_obj_t *col = lv_label_create(tc);
    lv_label_set_text(col, ":");
    lv_obj_set_style_text_font(col, UI_FONT_32, 0);
    lv_obj_set_style_text_color(col, UI_COL_YELLOW, 0);
    lv_obj_set_width(col, colon);
    lv_obj_set_style_text_align(col, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(col, tx + hw, WHEEL_Y + 40);
    s_min = make_wheel(tc, s_min_opts, LV_ROLLER_MODE_INFINITE, tx + hw + colon, mw);

    lv_obj_t *zc = make_card(s_scr, "Time Zone", zone_x, zone_w);
    s_zone = make_wheel(zc, s_zone_opts, LV_ROLLER_MODE_NORMAL, (zone_w - 140) / 2, 140);

    /* Format selector (tap to cycle) + DST */
    const int32_t row_y = CARD_Y + CARD_H + 12;
    lv_obj_t *fl = lv_label_create(s_scr);
    lv_label_set_text(fl, "Format");
    lv_obj_set_style_text_font(fl, UI_FONT_24, 0);
    lv_obj_set_style_text_color(fl, UI_COL_TITLE_Y, 0);
    lv_obj_set_pos(fl, UI_MARGIN, row_y + 20);
    s_fmt_btn = ui_make_button(s_scr, "", 220, UI_BTN_H, UI_FONT_24);
    lv_obj_set_pos(s_fmt_btn, UI_MARGIN + 94, row_y);
    lv_obj_add_event_cb(s_fmt_btn, fmt_cb, LV_EVENT_CLICKED, NULL);

    s_dst = ui_make_check(s_scr, "Daylight Savings");
    lv_obj_set_pos(s_dst, time_x, row_y + 11);
    lv_obj_add_event_cb(s_dst, dst_cb, LV_EVENT_CLICKED, NULL);

    /* Auto Network Time + its status */
    s_auto = ui_make_check(s_scr, "Auto Network Time");
    lv_obj_set_pos(s_auto, UI_MARGIN, row_y + UI_BTN_H + 6);
    lv_obj_add_event_cb(s_auto, auto_cb, LV_EVENT_CLICKED, NULL);

    s_net_lbl = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_net_lbl, UI_FONT_20, 0);
    lv_obj_set_style_text_color(s_net_lbl, UI_COL_TEXT, 0);
    lv_obj_set_pos(s_net_lbl, time_x, row_y + UI_BTN_H + 16);

    lv_obj_t *cancel = ui_make_button(s_scr, "Cancel", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    ui_add_nav(cancel, UI_SCR_SETUP);

    lv_obj_t *save = ui_make_button(s_scr, "Save", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(save, save_cb, LV_EVENT_CLICKED, NULL);

    lv_timer_create(live_tick, 1000, NULL);
    ui_datetime_on_enter();
    return s_scr;
}
