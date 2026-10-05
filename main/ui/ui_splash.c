/*
 * Power-up splash:
 *   1. AquaRevival logo on its own                        (SPLASH_LOGO_MS)
 *   2. bubbles rise from the bottom and float off the top (SPLASH_BUBBLES_MS), then
 *      we wait until every bubble has left the top of the screen
 *   3. [optional, off for now] customer logo with bubbles  (SPLASH_CUSTOMER_MS)
 *   4. -> Home
 * Parameter 34 "Show Startup Screen" OFF skips all of this (see ui_init).
 *
 * Logos come from main/ui/logos/ (made with tools/png_to_lvgl.py). If a logo file isn't
 * there yet, a text placeholder is shown instead.
 */
#include "ui.h"

#if __has_include("logos/logo_aquarevival.h")
#include "logos/logo_aquarevival.h"
#define HAVE_LOGO_AQUAREVIVAL 1
#endif
#if __has_include("logos/logo_customer.h")
#include "logos/logo_customer.h"
#define HAVE_LOGO_CUSTOMER 1
#endif

/* =====================  TIMING  ===================== */
#define SPLASH_SHOW_CUSTOMER 0          /* 1 = show the customer logo stage again */
#define SPLASH_BG           0x144D81    /* same blue as the AquaRevival logo background */
#define SPLASH_LOGO_MS      3000
#define SPLASH_BUBBLES_MS   2000    /* new bubbles stop after this; the rest float off */
#define SPLASH_CUSTOMER_MS  5000
/* ==================================================== */

#define N_BUBBLES 18

typedef struct {
    lv_obj_t *obj;
    int32_t   x0, y0, rise, amp, phase, size;
} bubble_t;

static lv_obj_t *s_scr, *s_logo_ar, *s_logo_cust;
static bubble_t  s_bub[N_BUBBLES];
static bool      s_spawning;     /* relaunch bubbles when they reach the top */
static bool      s_finishing;    /* last stage: leave once every bubble is gone */
static int       s_active;       /* bubbles currently rising (or waiting to start) */
static int       s_stage;

/* ------------------------------------------------------------------ bubbles */
static void bubble_exec(void *var, int32_t v)          /* v: 0..1000 progress */
{
    bubble_t *b = var;
    int32_t y = b->y0 - b->rise * v / 1000;
    int32_t x = b->x0 + lv_trigo_sin((int16_t)((v * 720 / 1000 + b->phase) % 360)) * b->amp / 32767;
    lv_obj_set_pos(b->obj, x, y);
}

static void bubble_launch(bubble_t *b, uint32_t delay);

static void splash_finish(void)
{
    s_finishing = false;
    ui_go(UI_SCR_HOME);
}

static void bubble_done(lv_anim_t *a)
{
    bubble_t *b = lv_anim_get_user_data(a);
    if (s_spawning) {
        bubble_launch(b, lv_rand(0, 400));
        return;
    }
    lv_obj_add_flag(b->obj, LV_OBJ_FLAG_HIDDEN);
    if (s_active > 0) s_active--;
    if (s_finishing && s_active == 0) splash_finish();   /* last bubble has left the screen */
}

static void bubble_launch(bubble_t *b, uint32_t delay)
{
    b->size  = (int32_t)lv_rand(10, 26);
    b->x0    = (int32_t)lv_rand(10, UI_W - 10 - b->size);
    b->y0    = UI_H + (int32_t)lv_rand(0, 30);
    b->rise  = b->y0 + b->size + 10;                 /* all the way off the top */
    b->amp   = (int32_t)lv_rand(4, 16);
    b->phase = (int32_t)lv_rand(0, 359);
    lv_obj_set_size(b->obj, b->size, b->size);
    lv_obj_set_pos(b->obj, b->x0, b->y0);
    lv_obj_remove_flag(b->obj, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, b);
    lv_anim_set_user_data(&a, b);
    lv_anim_set_exec_cb(&a, bubble_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, lv_rand(2200, 4000));  /* bigger variety = more natural */
    lv_anim_set_delay(&a, delay);
    lv_anim_set_completed_cb(&a, bubble_done);
    lv_anim_start(&a);
}

static lv_obj_t *make_bubble(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x7FE3F5), 0);          /* light top ...   */
    lv_obj_set_style_bg_grad_color(o, lv_color_hex(0x1D8FB5), 0);     /* ... darker base */
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(0xC8F4FF), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_60, 0);

    lv_obj_t *hl = lv_obj_create(o);                                   /* shine */
    lv_obj_remove_style_all(hl);
    lv_obj_remove_flag(hl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(hl, LV_PCT(35), LV_PCT(35));
    lv_obj_align(hl, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_set_style_radius(hl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(hl, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(hl, lv_color_white(), 0);

    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

/* ------------------------------------------------------------------ logos */
static lv_obj_t *make_logo_aquarevival(lv_obj_t *parent)
{
#ifdef HAVE_LOGO_AQUAREVIVAL
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, &logo_aquarevival);
    lv_obj_center(img);
    return img;
#else
    lv_obj_t *l = lv_label_create(parent);           /* placeholder until the logo is added */
    lv_label_set_text(l, "AquaRevival");
    lv_obj_set_style_text_font(l, UI_FONT_36, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_center(l);
    return l;
#endif
}

static lv_obj_t *make_logo_customer(lv_obj_t *parent)
{
#ifdef HAVE_LOGO_CUSTOMER
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, &logo_customer);
    lv_obj_center(img);
    return img;
#else
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, "KALAMAZOO\nCOUNTRY CLUB");
    lv_obj_set_style_text_font(l, UI_FONT_36, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    return l;
#endif
}

/* ------------------------------------------------------------------ sequence */
static void stage_cb(lv_timer_t *t);

static void next_stage_in(uint32_t ms)
{
    lv_timer_t *t = lv_timer_create(stage_cb, ms, NULL);
    lv_timer_set_repeat_count(t, 1);
}

static void stage_cb(lv_timer_t *t)
{
    (void)t;
    s_stage++;
    switch (s_stage) {
    case 1:                                          /* logo off, bubbles on */
        lv_obj_add_flag(s_logo_ar, LV_OBJ_FLAG_HIDDEN);
        s_spawning = true;
        s_active = N_BUBBLES;
        for (int i = 0; i < N_BUBBLES; i++) bubble_launch(&s_bub[i], lv_rand(0, 1500));
        next_stage_in(SPLASH_BUBBLES_MS);
        break;
    case 2:                                          /* customer logo, bubbles keep going */
        if (SPLASH_SHOW_CUSTOMER) {
            lv_obj_remove_flag(s_logo_cust, LV_OBJ_FLAG_HIDDEN);
            next_stage_in(SPLASH_CUSTOMER_MS);
            break;
        }
        /* fall through */
    default:                /* stop making bubbles; Home once the last one floats off the top */
        s_spawning = false;
        s_finishing = true;
        if (s_active == 0) splash_finish();
        break;
    }
}

void ui_splash_start(void)
{
    s_stage = 0;
    lv_obj_remove_flag(s_logo_ar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_logo_cust, LV_OBJ_FLAG_HIDDEN);
    next_stage_in(SPLASH_LOGO_MS);
}

lv_obj_t *ui_splash_create(void)
{
    s_scr = ui_make_screen(false);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(SPLASH_BG), 0);
    for (int i = 0; i < N_BUBBLES; i++) s_bub[i].obj = make_bubble(s_scr);
    s_logo_ar = make_logo_aquarevival(s_scr);        /* logos drawn on top of the bubbles */
    s_logo_cust = make_logo_customer(s_scr);
    lv_obj_add_flag(s_logo_cust, LV_OBJ_FLAG_HIDDEN);
    return s_scr;
}
