/* IP Address Configuration and Web Server Configuration.
 * Values are saved to NVS; they are not applied to a network interface yet. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ui.h"
#include "ui_keypad.h"
#include "settings.h"

/* ================================================================== IP config */

static lv_obj_t  *s_type_field;
static lv_obj_t  *s_ip_fields[4];
static lv_obj_t  *s_ip_labels[4];
static char       s_edit[4][16];
static bool       s_edit_dhcp;
static ui_popup_t s_ip_popup;
static const char *k_ip_names[4] = { "IP Address:", "Gateway:", "Mask:", "DNS:" };

static void set_field_text(lv_obj_t *field, const char *txt)
{
    lv_label_set_text(lv_obj_get_child(field, 0), txt);
}

static void ip_refresh(void)
{
    set_field_text(s_type_field, s_edit_dhcp ? "DHCP" : "Static");
    for (int i = 0; i < 4; i++) {
        set_field_text(s_ip_fields[i], s_edit_dhcp ? "" : s_edit[i]);
        lv_obj_set_style_opa(s_ip_fields[i], s_edit_dhcp ? LV_OPA_50 : LV_OPA_COVER, 0);
        lv_obj_set_style_opa(s_ip_labels[i], s_edit_dhcp ? LV_OPA_50 : LV_OPA_COVER, 0);
    }
}

static bool valid_ipv4(const char *s)
{
    int parts = 0;
    while (*s) {
        if (*s < '0' || *s > '9') return false;
        int v = 0, digits = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; if (++digits > 3) return false; }
        if (v > 255) return false;
        parts++;
        if (*s == '.') { s++; if (!*s) return false; }
        else if (*s) return false;
    }
    return parts == 4;
}

static bool ip_popup_ok(const char *text, void *user)
{
    int idx = (int)(intptr_t)user;
    if (text[0] && !valid_ipv4(text)) {
        ui_keypad_message(&s_ip_popup.kp, "Invalid Address", true);
        return false;
    }
    snprintf(s_edit[idx], sizeof(s_edit[idx]), "%s", text);
    ip_refresh();
    return true;
}

static void ip_field_cb(lv_event_t *e)
{
    if (s_edit_dhcp) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_popup_open(&s_ip_popup, k_ip_names[idx], s_edit[idx], 15, ip_popup_ok, (void *)(intptr_t)idx);
}

static void type_cb(lv_event_t *e) { (void)e; s_edit_dhcp = !s_edit_dhcp; ip_refresh(); }

static void ip_save_cb(lv_event_t *e)
{
    (void)e;
    g_settings.dhcp = s_edit_dhcp;
    snprintf(g_settings.ip,      sizeof(g_settings.ip),      "%s", s_edit[0]);
    snprintf(g_settings.gateway, sizeof(g_settings.gateway), "%s", s_edit[1]);
    snprintf(g_settings.mask,    sizeof(g_settings.mask),    "%s", s_edit[2]);
    snprintf(g_settings.dns,     sizeof(g_settings.dns),     "%s", s_edit[3]);
    settings_save();
    ui_go(UI_SCR_NETWORK);
}

void ui_ipcfg_on_enter(void)
{
    s_edit_dhcp = g_settings.dhcp;
    snprintf(s_edit[0], 16, "%s", g_settings.ip);
    snprintf(s_edit[1], 16, "%s", g_settings.gateway);
    snprintf(s_edit[2], 16, "%s", g_settings.mask);
    snprintf(s_edit[3], 16, "%s", g_settings.dns);
    ui_popup_close(&s_ip_popup);
    ip_refresh();
}

static lv_obj_t *right_label(lv_obj_t *parent, const char *txt, int32_t right_x, int32_t y, int32_t h)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, UI_FONT_28, 0);
    lv_obj_set_style_text_color(l, UI_COL_TITLE_Y, 0);
    lv_obj_set_width(l, 260);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(l, right_x - 260, y + (h - 32) / 2);
    return l;
}

lv_obj_t *ui_ipcfg_create(void)
{
    lv_obj_t *scr = ui_make_screen(false);
    ui_make_title(scr, "IP Address Configuration", UI_COL_TEXT);

    const int32_t fx = 330, fh = 52, pitch = 62, y0 = 66;
    right_label(scr, "Type:", fx - 16, y0, fh);
    s_type_field = ui_make_field(scr, 200, fh);
    lv_obj_set_pos(s_type_field, fx, y0);
    lv_obj_add_event_cb(s_type_field, type_cb, LV_EVENT_CLICKED, NULL);

    for (int i = 0; i < 4; i++) {
        int32_t y = y0 + (i + 1) * pitch;
        s_ip_labels[i] = right_label(scr, k_ip_names[i], fx - 16, y, fh);
        s_ip_fields[i] = ui_make_field(scr, 300, fh);
        lv_obj_set_pos(s_ip_fields[i], fx, y);
        lv_obj_add_event_cb(s_ip_fields[i], ip_field_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    lv_obj_t *cancel = ui_make_button(scr, "Cancel", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    ui_add_nav(cancel, UI_SCR_NETWORK);

    lv_obj_t *save = ui_make_button(scr, "Save", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(save, ip_save_cb, LV_EVENT_CLICKED, NULL);

    ui_popup_create(&s_ip_popup, scr, true);
    ui_ipcfg_on_enter();
    return scr;
}

/* ================================================================== Web server */
static lv_obj_t  *s_web_check, *s_port_field;
static uint16_t   s_edit_port;
static ui_popup_t s_port_popup;

static void port_refresh(void)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", s_edit_port);
    set_field_text(s_port_field, buf);
}

static bool port_ok(const char *text, void *user)
{
    (void)user;
    long v = strtol(text, NULL, 10);
    if (v < 1 || v > 65535) {
        ui_keypad_message(&s_port_popup.kp, "1 - 65535", true);
        return false;
    }
    s_edit_port = (uint16_t)v;
    port_refresh();
    return true;
}

static void port_field_cb(lv_event_t *e)
{
    (void)e;
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", s_edit_port);
    ui_popup_open(&s_port_popup, "Web Server Port", buf, 5, port_ok, NULL);
}

static void web_save_cb(lv_event_t *e)
{
    (void)e;
    g_settings.web_enable = ui_check_get(s_web_check);
    g_settings.web_port = s_edit_port;
    settings_save();
    ui_go(UI_SCR_NETWORK);
}

void ui_webcfg_on_enter(void)
{
    ui_check_set(s_web_check, g_settings.web_enable);
    s_edit_port = g_settings.web_port;
    ui_popup_close(&s_port_popup);
    port_refresh();
}

lv_obj_t *ui_webcfg_create(void)
{
    lv_obj_t *scr = ui_make_screen(false);
    ui_make_title(scr, "Web Server Configuration", UI_COL_TEXT);

    s_web_check = ui_make_check(scr, "Web Server Enable");
    lv_obj_set_pos(s_web_check, 330, 90);

    right_label(scr, "Port:", 330 - 16, 152, 52);
    s_port_field = ui_make_field(scr, 160, 52);
    lv_obj_set_pos(s_port_field, 330, 152);
    lv_obj_add_event_cb(s_port_field, port_field_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel = ui_make_button(scr, "Cancel", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
    ui_add_nav(cancel, UI_SCR_NETWORK);

    lv_obj_t *save = ui_make_button(scr, "Save", UI_BTN_W, UI_BTN_H, UI_FONT_32);
    lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
    lv_obj_add_event_cb(save, web_save_cb, LV_EVENT_CLICKED, NULL);

    ui_popup_create(&s_port_popup, scr, false);
    ui_webcfg_on_enter();
    return scr;
}
