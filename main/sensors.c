#include "sensors.h"

static const char *const k_names[SENSOR_COUNT] = { "Influent", "Effluent", "Air Supply", "Flow" };

static float s_raw[SENSOR_COUNT];
static bool  s_present[SENSOR_COUNT];

const char *sensor_name(int i)     { return k_names[i]; }
const char *sensor_raw_unit(int i) { return i == 3 ? "mA" : "V"; }
const char *sensor_unit(int i)     { return i == 3 ? "GpM" : "PSI"; }
bool        sensor_present(int i)  { return i >= 0 && i < SENSOR_COUNT && s_present[i]; }
float       sensor_raw(int i)      { return sensor_present(i) ? s_raw[i] : 0; }

void sensor_set_raw(int i, float raw)
{
    if (i < 0 || i >= SENSOR_COUNT) return;
    s_raw[i] = raw;
    s_present[i] = true;
}

float sensor_scale(float raw, const scaling_t *sc)
{
    float span = sc->hi_raw - sc->lo_raw;
    if (span == 0) return sc->lo_val;
    return sc->lo_val + (raw - sc->lo_raw) * (sc->hi_val - sc->lo_val) / span;
}

float sensor_value(int i) { return sensor_scale(sensor_raw(i), &g_settings.scaling[i]); }

void  sensors_tick(void) { }

/* Health will come from the pressure sensors (delta-P against parameter 5) once they exist */
float sensor_health(void) { return -1; }
