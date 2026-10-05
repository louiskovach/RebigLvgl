/* Button-grid menus. Items with target = NONE do nothing yet. */
#include "ui.h"

#define NONE (-1)
typedef struct { const char *text; int target; void (*cb)(void); } menu_item_t;

static void cb_event(lv_event_t *e)
{
    void (*cb)(void) = (void (*)(void))lv_event_get_user_data(e);
    cb();
}

void ui_advanced_reboot(void);     /* ui_advanced.c */
void ui_advanced_defaults(void);

#define GRID_W     UI_COL_W
#define GRID_X0    UI_MARGIN
#define GRID_X1    UI_COL1_X
#define GRID_Y0    72
#define GRID_PITCH (UI_BTN_H + UI_GAP)

/* Items fill the left column first, then the right. */
static lv_obj_t *build_menu(const char *title, const menu_item_t *items, int n, int exit_to, int back_to)
{
    lv_obj_t *scr = ui_make_screen(false);
    ui_make_title(scr, title, UI_COL_TEXT);

    int rows = (n + 1) / 2;
    for (int i = 0; i < n; i++) {
        int col = i / rows, row = i % rows;
        lv_obj_t *b = ui_make_menu_button(scr, items[i].text, GRID_W);
        lv_obj_set_pos(b, col ? GRID_X1 : GRID_X0, GRID_Y0 + row * GRID_PITCH);
        if (items[i].cb)                 lv_obj_add_event_cb(b, cb_event, LV_EVENT_CLICKED, (void *)items[i].cb);
        else if (items[i].target != NONE) ui_add_nav(b, (ui_screen_id_t)items[i].target);
    }
    if (exit_to != NONE) {
        lv_obj_t *b = ui_make_button(scr, "Exit", UI_BTN_W, UI_BTN_H, UI_FONT_32);
        lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, UI_MARGIN, -UI_MARGIN);
        ui_add_nav(b, (ui_screen_id_t)exit_to);
    }
    if (back_to != NONE) {
        lv_obj_t *b = ui_make_button(scr, "Back", UI_BTN_W, UI_BTN_H, UI_FONT_32);
        lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, -UI_MARGIN, -UI_MARGIN);
        ui_add_nav(b, (ui_screen_id_t)back_to);
    }
    return scr;
}

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

lv_obj_t *ui_maint_create(void)
{
    static const menu_item_t items[] = {
        { "Drain/Rinse", NONE, ui_drain_start }, { "Media Fill", NONE }, { "Clean Chamber", NONE },
        { "Setup", UI_SCR_PASSCODE }, { "View Settings", UI_SCR_VIEWINFO }, { "View Alarms", UI_SCR_ALARMS },
    };
    return build_menu("Maintenance", items, N(items), UI_SCR_HOME, NONE);
}

lv_obj_t *ui_setup_create(void)
{
    static const menu_item_t items[] = {
        { "Parameters", UI_SCR_PARAMS }, { "Edit Passcode", UI_SCR_CHPASS }, { "Advanced", UI_SCR_ADVANCED },
        { "Schedule Types", NONE }, { "Date/Time", UI_SCR_DATETIME }, { "Network", UI_SCR_NETWORK },
    };
    return build_menu("Setup", items, N(items), UI_SCR_HOME, UI_SCR_MAINT);
}

lv_obj_t *ui_viewinfo_create(void)
{
    static const menu_item_t items[] = {
        { "Information", UI_SCR_INFO }, { "Parameters", UI_SCR_PARAMS }, { "History", NONE },
        { "Revive Exclusion", NONE }, { "Revive Schedule", NONE }, { "System Diagram", NONE },
    };
    return build_menu("View Information", items, N(items), UI_SCR_HOME, UI_SCR_MAINT);
}

lv_obj_t *ui_network_create(void)
{
    static const menu_item_t items[] = {
        { "IP Address", UI_SCR_IPCFG }, { "Web Server", UI_SCR_WEBCFG },
    };
    return build_menu("Network", items, N(items), UI_SCR_HOME, UI_SCR_SETUP);
}

lv_obj_t *ui_advanced_create(void)
{
    static const menu_item_t items[] = {
        { "Reboot System", NONE, ui_advanced_reboot }, { "Analog Scaling", UI_SCR_ANALOG },
        { "Upgrade Remote IO", NONE }, { "Custom Pictures", NONE },
        { "Adjust Display", UI_SCR_DISPLAY }, { "Save Parameters as Defaults", NONE, ui_advanced_defaults },
        { "Select System Model", UI_SCR_MODEL }, { "Update Email Strings", NONE },
    };
    return build_menu("Advanced", items, N(items), UI_SCR_HOME, UI_SCR_SETUP);
}

lv_obj_t *ui_diag_create(void)
{
    static const menu_item_t items[] = {
        { "Service IO", UI_SCR_SERVICE_IO }, { "Analog Scaling", UI_SCR_ANALOG },
        { "Color Display", UI_SCR_COLOR }, { "Diagnostic Filter", NONE, ui_diag_filter_start },
        { "Touch Test", UI_SCR_TOUCH }, { "Detailed SMTP Logging", UI_SCR_SMTP },
    };
    return build_menu("Diagnostics", items, N(items), UI_SCR_HOME, NONE);
}
