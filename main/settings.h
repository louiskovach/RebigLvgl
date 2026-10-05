#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "params.h"

typedef enum {
    DATEFMT_MDY24, DATEFMT_MDY12, DATEFMT_DMY24, DATEFMT_DMY12, DATEFMT_YMD24, DATEFMT_COUNT
} date_fmt_t;

/* Linear scaling for one analog input: raw (V or mA) -> engineering value */
typedef struct { float lo_raw, lo_val, hi_raw, hi_val; } scaling_t;
#define SENSOR_COUNT 4

/* Room for parameters: new parameters can be added (up to PARAM_SLOTS) without moving the
 * fields after the array, so saved settings keep loading. */
#define PARAM_SLOTS 64
_Static_assert(PARAM_COUNT <= PARAM_SLOTS, "raise PARAM_SLOTS (and add a migration)");

/* Everything the HMI remembers across power cycles (stored in NVS).
 * !! Only ADD new fields at the END so settings saved by older firmware still load. !! */
typedef struct {
    uint32_t magic;
    char     passcode[9];
    int8_t   tz_hours;
    bool     dst;
    bool     auto_net_time;
    uint8_t  date_fmt;
    bool     dhcp;
    char     ip[16], gateway[16], mask[16], dns[16];
    bool     web_enable;
    uint16_t web_port;
    float    params[PARAM_SLOTS];
    uint8_t  model;                              /* index into the system model list */
    uint8_t  disp_brightness, disp_contrast, disp_saturation;   /* 0..255 */
    scaling_t scaling[SENSOR_COUNT];
    uint8_t  smtp_detail;                        /* Diagnostics -> Detailed SMTP Logging */
    uint8_t  rev;                                /* one-off fix-ups, see settings_load() */
} settings_t;

extern settings_t g_settings;

void        settings_load(void);
void        settings_save(void);
void        settings_default_scaling(scaling_t *sc);   /* fills SENSOR_COUNT entries */
int32_t     settings_utc_offset_s(void);   /* time zone + DST, in seconds */
const char *settings_date_fmt_name(uint8_t fmt);
