#pragma once
#include <stdbool.h>
#include "settings.h"

/* Analog inputs 0..3 = Influent, Effluent, Air Supply, Flow.
 * Not connected yet: no readings exist until real sensor code calls sensor_set_raw(), and
 * every screen shows "--" for an input that has no reading. */

const char *sensor_name(int i);
const char *sensor_raw_unit(int i);   /* "V" or "mA" */
const char *sensor_unit(int i);       /* "PSI" or "GpM" */
bool        sensor_present(int i);    /* a real reading has been received */
float       sensor_raw(int i);
float       sensor_value(int i);                              /* scaled with saved calibration */
float       sensor_scale(float raw, const scaling_t *sc);
void        sensor_set_raw(int i, float raw);                 /* call from the sensor driver */
void        sensors_tick(void);                               /* periodic hook (nothing yet) */
float       sensor_health(void);      /* media health 0..100 %, or -1 = not available yet */
