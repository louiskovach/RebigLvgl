#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* The machine's parameter list (Setup -> Parameters). Values persist in NVS. */
typedef enum {
    P_LANGUAGE,            /*  1 */
    P_AIR_PRESS_EN,        /*  2 */
    P_LOW_PRESS_CUTOFF,    /*  3 */
    P_DP_REVIVE_EN,        /*  4 */
    P_DP_RANGE,            /*  5 */
    P_WAIT_BEFORE_REVIVE,  /*  6 */
    P_PRESSURE_UNITS,      /*  7 */
    P_FLOW_PROBE_EN,       /*  8 */
    P_FLOW_UNITS,          /*  9 */
    P_FLOW_MAX,            /* 10 */
    P_RESTART_AFTER_PF,    /* 11 */
    P_EFF_VALVE_CLOSE_DLY, /* 12 */
    P_PUMP_RUN_CONFIRM,    /* 13 */
    P_PRECOAT_MIN,         /* 14 */
    P_PUMP_SHUTDOWN_DLY,   /* 15 */
    P_FP_OFF_LATCH_S,      /* 16 */
    P_FP_ON_LATCH_MIN,     /* 17 */
    P_PUMP_STOP_DLY,       /* 18 */
    P_REV_VALVE_CLOSE_DLY, /* 19 */
    P_REVIVE_STROKES,      /* 20 */
    P_CLEAN_PUMP_RUN_S,    /* 21 */
    P_CLEAN_PUMP_DLY_MIN,  /* 22 */
    P_CLEAN_OVERALL_H,     /* 23 */
    P_BEEP,                /* 24 */
    P_CRYPTOCAPTURE,       /* 25 */
    P_REMOTE_START_STOP,   /* 26 */
    P_VFD_DISPLAY,         /* 27 */
    P_FAULT_PRESS,         /* 28 */
    P_REV_CYL_OFF_S,       /* 29 */
    P_REV_CYL_ON_S,        /* 30 */
    P_MODBUS_ID,           /* 31 */
    P_MODBUS_BAUD,         /* 32 */
    P_IDLE_EXIT_S,         /* 33 */
    P_SHOW_STARTUP,        /* 34 */
    PARAM_COUNT
} param_id_t;

typedef enum { PT_NUMBER, PT_TOGGLE, PT_CHOICE } param_type_t;

typedef struct {
    const char         *name;
    param_type_t        type;
    float               def;
    float               min, max;     /* PT_NUMBER */
    bool                decimals;     /* PT_NUMBER: allow e.g. 0.5 */
    uint8_t             pad;          /* PT_NUMBER: zero-pad whole numbers to this many digits */
    bool                zero_is_off;  /* PT_NUMBER: show 0 as "OFF" */
    const char *const  *choices;      /* PT_CHOICE */
    uint8_t             n_choices;
} param_def_t;

const param_def_t *param_def(int id);
float  param_get(int id);
void   param_set(int id, float v);            /* saves to NVS */
void   param_format(int id, char *buf, size_t len, bool padded);
void   params_defaults(float *vals);
