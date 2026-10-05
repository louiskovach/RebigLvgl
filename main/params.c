#include <stdio.h>
#include <math.h>
#include "params.h"
#include "settings.h"
#include "plc_link.h"

static const char *const k_lang[]   = { "English" };
static const char *const k_press[]  = { "PSI", "kPa", "bar" };
static const char *const k_flow[]   = { "GpM", "LpM" };
static const char *const k_baud[]   = { "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200" };

#define NUM(n, d, lo, hi, dec, padw) { .name = n, .type = PT_NUMBER, .def = d, .min = lo, .max = hi, .decimals = dec, .pad = padw }
#define TOG(n, d)                    { .name = n, .type = PT_TOGGLE, .def = d }
#define CHO(n, d, list)              { .name = n, .type = PT_CHOICE, .def = d, .choices = list, \
                                       .n_choices = sizeof(list) / sizeof(list[0]) }

/* =====================  EDIT NAMES / DEFAULTS / RANGES HERE  ===================== */
static const param_def_t k_params[PARAM_COUNT] = {
    [P_LANGUAGE]            = CHO("Language", 0, k_lang),
    [P_AIR_PRESS_EN]        = TOG("Air Pressure Enable", 0),
    [P_LOW_PRESS_CUTOFF]    = NUM("Low Pressure Cutoff", 50, 0, 999, true, 0),
    [P_DP_REVIVE_EN]        = TOG("Delta-P Revive Enable", 0),
    [P_DP_RANGE]            = NUM("Delta-P Range", 12, 0, 999, true, 0),
    [P_WAIT_BEFORE_REVIVE]  = NUM("Wait before Revive (seconds)", 5, 0, 999, true, 3),
    [P_PRESSURE_UNITS]      = CHO("Pressure Units", 0, k_press),
    [P_FLOW_PROBE_EN]       = TOG("Flow Probe Enable", 0),
    [P_FLOW_UNITS]          = CHO("Flow Rate Units", 0, k_flow),
    [P_FLOW_MAX]            = NUM("Flow Probe Maximum GpM", 9999, 0, 9999, false, 0),
    [P_RESTART_AFTER_PF]    = TOG("Restart Filter after Power Fail", 1),
    [P_EFF_VALVE_CLOSE_DLY] = NUM("Effluent Valve Close Delay (seconds)", 5, 0, 999, true, 3),
    [P_PUMP_RUN_CONFIRM]    = NUM("Pump Run Confirm Time (seconds)", 5, 0, 999, true, 3),
    [P_PRECOAT_MIN]         = NUM("Precoat Cycle Time (minutes)", 5, 0, 999, true, 3),
    [P_PUMP_SHUTDOWN_DLY]   = NUM("Time for Pump Shutdown Delay (seconds)", 5, 0, 999, true, 3),
    [P_FP_OFF_LATCH_S]      = NUM("Fireman Protect Off Latch Delay (seconds)", 5, 0, 999, true, 3),
    [P_FP_ON_LATCH_MIN]     = NUM("Fireman Protect On Latch Delay (minutes)", 0, 0, 999, true, 3),
    [P_PUMP_STOP_DLY]       = NUM("Pump Stop Delay (seconds)", 5, 0, 999, true, 3),
    [P_REV_VALVE_CLOSE_DLY] = NUM("Revival Valve V11 Close Delay (seconds)", 5, 0, 999, true, 3),
    [P_REVIVE_STROKES]      = NUM("Revive Strokes", 3, 1, 99, false, 3),
    [P_CLEAN_PUMP_RUN_S]    = NUM("Pump Run time in Clean Cycle (seconds)", 60, 0, 999, true, 3),
    [P_CLEAN_PUMP_DLY_MIN]  = NUM("Clean Cycle Pump Delay (minutes)", 10, 0, 999, true, 3),
    [P_CLEAN_OVERALL_H]     = NUM("Overall Clean Cycle Time (Hours)", 9, 0, 999, true, 3),
    [P_BEEP]                = TOG("Beep when Touch screen pressed", 1),
    [P_CRYPTOCAPTURE]       = TOG("Enable CryptoCapture Mode", 0),
    [P_REMOTE_START_STOP]   = TOG("Remote Start/Stop Enable", 0),
    [P_VFD_DISPLAY]         = TOG("VFD Display Enable", 0),
    [P_FAULT_PRESS]         = NUM("Fault Pressure Limit (PSI)", 50, 0, 999, true, 0),
    [P_REV_CYL_OFF_S]       = NUM("Revive Cylinder Off Time (seconds)", 5, 0, 999, true, 3),
    [P_REV_CYL_ON_S]        = NUM("Revive Cylinder On Time (seconds)", 2, 0, 999, true, 3),
    [P_MODBUS_ID]           = { .name = "Modbus/RTU Slave Id", .type = PT_NUMBER, .def = 0, .min = 0,
                                .max = 247, .zero_is_off = true },
    [P_MODBUS_BAUD]         = CHO("Modbus/RTU Baud Rate", 3, k_baud),
    [P_IDLE_EXIT_S]         = NUM("Idle time before exit Menu (seconds)", 600, 0, 9999, false, 0),
    [P_SHOW_STARTUP]        = TOG("Show Startup Screen", 1),
};
/* ================================================================================ */

const param_def_t *param_def(int id) { return &k_params[id]; }

float param_get(int id)
{
    if (id < 0 || id >= PARAM_COUNT) return 0;
    return g_settings.params[id];
}

void param_set(int id, float v)
{
    if (id < 0 || id >= PARAM_COUNT) return;
    g_settings.params[id] = v;
    settings_save();
    plc_link_load_params();
}

void params_defaults(float *vals)
{
    for (int i = 0; i < PARAM_COUNT; i++) vals[i] = k_params[i].def;
}

void param_format(int id, char *buf, size_t len, bool padded)
{
    const param_def_t *d = &k_params[id];
    float v = param_get(id);
    switch (d->type) {
    case PT_TOGGLE:
        snprintf(buf, len, "%s", v != 0 ? "ON" : "OFF");
        break;
    case PT_CHOICE: {
        int i = (int)v;
        if (i < 0 || i >= d->n_choices) i = 0;
        snprintf(buf, len, "%s", d->choices[i]);
        break;
    }
    default:
        if (d->zero_is_off && v == 0) { snprintf(buf, len, "OFF"); break; }
        if (fabsf(v - roundf(v)) < 0.0005f) {
            snprintf(buf, len, "%0*d", padded ? d->pad : 0, (int)roundf(v));
        } else {
            snprintf(buf, len, "%.2f", (double)v);           /* trim trailing zeros */
            char *p = buf; while (*p) p++;
            while (p > buf && p[-1] == '0') *--p = '\0';
        }
        break;
    }
}
