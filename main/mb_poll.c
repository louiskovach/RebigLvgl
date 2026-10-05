/* mb_poll.c - see mb_poll.h */
#include <string.h>
#include <stdio.h>
#include "mb_poll.h"
#include "modbus_master.h"
#include "plc.h"

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
static const char *TAG = "mb_poll";
static SemaphoreHandle_t s_img;          /* protects s_line[] data shared by the two tasks */
#define IMG_LOCK()   xSemaphoreTake(s_img, portMAX_DELAY)
#define IMG_UNLOCK() xSemaphoreGive(s_img)
#define STATUS_VB    CONFIG_MB_STATUS_VBYTE
#else                                     /* PC test build */
#define IMG_LOCK()   ((void)0)
#define IMG_UNLOCK() ((void)0)
#define STATUS_VB    500
#endif

/* Shared between the mb_poll task and the PLC task (under s_img) */
typedef struct {
    uint16_t data[MB_MAP_MAX_COUNT];  /* read line: latest from the slave; write line: latest from the PLC */
    bool     have;                    /* data is valid */
    bool     ok;                      /* last exchange worked -> status bit */
    bool     disabled;                /* bad table line (see the boot log) */
    mb_err_t err;                     /* last error */
    uint8_t  ex;                      /* last exception code */
    uint32_t good, bad;               /* counters */
} line_t;
static PLC_PSRAM line_t s_line[MB_MAP_MAX_LINES];
static int  s_lines;
static bool s_started;

/* ================================================================== table helpers */
static bool is_read(uint8_t fc) { return fc >= 1 && fc <= 4; }
static bool is_bits(uint8_t fc) { return fc == 1 || fc == 2 || fc == 5 || fc == 15; }

static const char *line_problem(const mb_map_t *m)
{
    if (m->slave < 1 || m->slave > 247)                         return "slave must be 1-247";
    if (!(is_read(m->fc) || m->fc == 5 || m->fc == 6 || m->fc == 15 || m->fc == 16))
                                                                return "fc must be 1, 2, 3, 4, 5, 6, 15 or 16";
    if ((m->fc == 5 || m->fc == 6) && m->count != 1)            return "fc 5 and 6 write exactly 1 (count = 1)";
    if (m->count < 1 || m->count > MB_MAP_MAX_COUNT)            return "count must be 1-125";
    if ((uint32_t)m->mb_addr + m->count > 0x10000u)             return "Modbus address range too big";
    if (is_bits(m->fc)) {
        if (m->area == 'A' || m->area == 'O')                   return "coils / discrete inputs need a bit area (I Q M V)";
        if (m->bit > 7)                                         return "bit must be 0-7";
        if (!plc_valid(m->area, m->byte, (m->bit + m->count + 7) / 8)) return "PLC address out of range";
    } else {
        if (!plc_valid(m->area, m->byte, m->count * 2))         return "PLC address out of range";
    }
    if (is_read(m->fc) && m->area == 'Q')                       return "don't read into Q: the program writes Q";
    return NULL;
}

/* slave values -> PLC memory (PLC task, PLC lock held) */
void mb_poll_to_plc(const mb_map_t *m, const uint16_t *data)
{
    if (is_bits(m->fc)) {
        for (uint16_t i = 0; i < m->count; i++) {
            int pos = m->byte * 8 + m->bit + i;
            plc_wr_bit(m->area, pos / 8, pos % 8, data[i] != 0);
        }
    } else {
        for (uint16_t i = 0; i < m->count; i++) plc_wr_word(m->area, m->byte + 2 * i, (int16_t)data[i]);
    }
}

/* PLC memory -> values to send (PLC task, PLC lock held) */
void mb_poll_from_plc(const mb_map_t *m, uint16_t *data)
{
    if (is_bits(m->fc)) {
        for (uint16_t i = 0; i < m->count; i++) {
            int pos = m->byte * 8 + m->bit + i;
            data[i] = (uint16_t)plc_rd_bit(m->area, pos / 8, pos % 8);
        }
    } else {
        for (uint16_t i = 0; i < m->count; i++) data[i] = (uint16_t)plc_rd_word(m->area, m->byte + 2 * i);
    }
}

/* ================================================================== PLC task side */
void mb_poll_apply_inputs(void)
{
    if (!s_started) return;
    IMG_LOCK();
    for (int i = 0; i < s_lines; i++) {
        const mb_map_t *m = &g_mb_map[i];
        plc_wr_bit('V', STATUS_VB + i / 8, i % 8, s_line[i].ok);       /* communication OK bit */
        if (!s_line[i].disabled && is_read(m->fc) && s_line[i].have) mb_poll_to_plc(m, s_line[i].data);
    }
    IMG_UNLOCK();
}

void mb_poll_capture_outputs(void)
{
    if (!s_started) return;
    IMG_LOCK();
    for (int i = 0; i < s_lines; i++) {
        const mb_map_t *m = &g_mb_map[i];
        if (s_line[i].disabled || is_read(m->fc)) continue;
        mb_poll_from_plc(m, s_line[i].data);
        s_line[i].have = true;
    }
    IMG_UNLOCK();
}

/* ================================================================== console: mb map */
static void fmt_mb(char *out, size_t len, const mb_map_t *m)
{
    static const int base[17] = { [1] = 1, [2] = 10001, [3] = 40001, [4] = 30001,
                                  [5] = 1, [6] = 40001, [15] = 1, [16] = 40001 };
    int a = base[m->fc] + m->mb_addr;
    if (m->count == 1) snprintf(out, len, "%05d", a);
    else               snprintf(out, len, "%05d-%05d", a, a + m->count - 1);
}

static void fmt_plc(char *out, size_t len, const mb_map_t *m)
{
    const char *an = m->area == 'A' ? "AIW" : m->area == 'O' ? "AQW" : NULL;
    if (is_bits(m->fc)) {
        int last = m->byte * 8 + m->bit + m->count - 1;
        if (m->count == 1) snprintf(out, len, "%c%u.%u", m->area, m->byte, m->bit);
        else snprintf(out, len, "%c%u.%u-%c%d.%d", m->area, m->byte, m->bit, m->area, last / 8, last % 8);
    } else if (an) {
        if (m->count == 1) snprintf(out, len, "%s%u", an, m->byte);
        else snprintf(out, len, "%s%u-%s%u", an, m->byte, an, m->byte + 2 * (m->count - 1));
    } else {
        if (m->count == 1) snprintf(out, len, "%cW%u", m->area, m->byte);
        else snprintf(out, len, "%cW%u-%cW%u", m->area, m->byte, m->area, m->byte + 2 * (m->count - 1));
    }
}

void mb_poll_print(void)
{
    if (!g_mb_map_count) { printf("  mb_map.c has no lines: nothing is exchanged with the PLC\n"); return; }
    printf("  line  slave  fc   Modbus          PLC                    status\n");
    for (int i = 0; i < g_mb_map_count && i < MB_MAP_MAX_LINES; i++) {
        const mb_map_t *m = &g_mb_map[i];
        char mb[24], plc[32];
        fmt_mb(mb, sizeof(mb), m);
        fmt_plc(plc, sizeof(plc), m);
        IMG_LOCK();
        line_t l = s_line[i];
        IMG_UNLOCK();
        printf("  %-4d  %-5u  %-3u  %-14s %s %-20s ", i, m->slave, m->fc, mb, is_read(m->fc) ? "->" : "<-", plc);
        if (l.disabled)          printf("DISABLED: %s\n", line_problem(m));
        else if (!s_started)     printf("not running\n");
        else if (!l.good && !l.bad) printf("waiting\n");
        else if (l.ok)           printf("OK (%lu ok, %lu failed)\n", (unsigned long)l.good, (unsigned long)l.bad);
        else if (l.err == MB_ERR_EXCEPTION)
                                 printf("FAIL: exception %u %s (%lu ok, %lu failed)\n", l.ex, mb_exception_str(l.ex),
                                        (unsigned long)l.good, (unsigned long)l.bad);
        else                     printf("FAIL: %s (%lu ok, %lu failed)\n", mb_err_str(l.err),
                                        (unsigned long)l.good, (unsigned long)l.bad);
    }
    printf("  status bits: V%d.0 = line 0, V%d.1 = line 1 ... (1 = communicating)\n", STATUS_VB, STATUS_VB);
}

/* ================================================================== the polling task */
#ifdef ESP_PLATFORM
#define RETRY_MS 1000           /* a line that failed is tried again after this long */

/* private to the mb_poll task */
static PLC_PSRAM uint16_t s_sent[MB_MAP_MAX_LINES][MB_MAP_MAX_COUNT];   /* last values written */
static bool    s_sent_valid[MB_MAP_MAX_LINES];
static int64_t s_due_us[MB_MAP_MAX_LINES];

static void record(int i, mb_err_t e)
{
    /* called with s_img held */
    s_line[i].ok = (e == MB_OK);
    s_line[i].err = e;
    s_line[i].ex = e == MB_ERR_EXCEPTION ? mb_last_exception() : 0;
    if (e == MB_OK) s_line[i].good++; else s_line[i].bad++;
}

static void do_read(int i, const mb_map_t *m)
{
    uint16_t regs[MB_MAP_MAX_COUNT];
    uint8_t  bits[MB_MAP_MAX_COUNT];
    mb_err_t e;
    switch (m->fc) {
    case 1:  e = mb_read_coils(m->slave, m->mb_addr, m->count, bits); break;
    case 2:  e = mb_read_discrete_inputs(m->slave, m->mb_addr, m->count, bits); break;
    case 3:  e = mb_read_holding_registers(m->slave, m->mb_addr, m->count, regs); break;
    default: e = mb_read_input_registers(m->slave, m->mb_addr, m->count, regs); break;
    }
    if (e == MB_OK && is_bits(m->fc)) for (int k = 0; k < m->count; k++) regs[k] = bits[k];
    IMG_LOCK();
    if (e == MB_OK) {
        memcpy(s_line[i].data, regs, m->count * sizeof(uint16_t));
        s_line[i].have = true;
    }
    record(i, e);
    IMG_UNLOCK();
}

static mb_err_t send(const mb_map_t *m, const uint16_t *v)
{
    uint8_t bits[MB_MAP_MAX_COUNT];
    switch (m->fc) {
    case 5:  return mb_write_coil(m->slave, m->mb_addr, v[0] != 0);
    case 6:  return mb_write_register(m->slave, m->mb_addr, v[0]);
    case 15: for (int k = 0; k < m->count; k++) bits[k] = v[k] != 0;
             return mb_write_coils(m->slave, m->mb_addr, m->count, bits);
    default: return mb_write_registers(m->slave, m->mb_addr, m->count, v);
    }
}

static void do_write(int i, const mb_map_t *m, int64_t now)
{
    uint16_t v[MB_MAP_MAX_COUNT];
    IMG_LOCK();
    bool have = s_line[i].have;
    bool failed = !s_line[i].ok && (s_line[i].good || s_line[i].bad);
    memcpy(v, s_line[i].data, m->count * sizeof(uint16_t));
    IMG_UNLOCK();
    if (!have) return;                                       /* PLC hasn't scanned yet */
    bool changed = !s_sent_valid[i] || memcmp(v, s_sent[i], m->count * sizeof(uint16_t));
    if (now < s_due_us[i] && (!changed || failed)) return;   /* nothing new (or waiting to retry) */
    mb_err_t e = send(m, v);
    if (e == MB_OK) {
        memcpy(s_sent[i], v, m->count * sizeof(uint16_t));
        s_sent_valid[i] = true;
        s_due_us[i] = now + (int64_t)m->period_ms * 1000;
    } else {
        s_due_us[i] = now + (int64_t)RETRY_MS * 1000;
    }
    IMG_LOCK();
    record(i, e);
    IMG_UNLOCK();
}

static void poll_task(void *arg)
{
    (void)arg;
    for (;;) {
        for (int i = 0; i < s_lines; i++) {
            const mb_map_t *m = &g_mb_map[i];
            if (s_line[i].disabled) continue;
            int64_t now = esp_timer_get_time();
            if (is_read(m->fc)) {
                if (now < s_due_us[i]) continue;
                do_read(i, m);
                bool ok = s_line[i].ok;
                s_due_us[i] = now + (int64_t)(ok ? m->period_ms : RETRY_MS) * 1000;
            } else {
                do_write(i, m, now);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_MB_POLL_CYCLE_MS));
    }
}

void mb_poll_start(void)
{
    if (s_started) return;
    if (!g_mb_map_count) { ESP_LOGI(TAG, "mb_map.c is empty: no Modbus polling"); return; }
    if (!mb_master_ready()) { ESP_LOGE(TAG, "Modbus master not running: no polling"); return; }
    s_lines = g_mb_map_count > MB_MAP_MAX_LINES ? MB_MAP_MAX_LINES : g_mb_map_count;
    if (g_mb_map_count > MB_MAP_MAX_LINES)
        ESP_LOGE(TAG, "mb_map.c has %d lines, only the first %d are used", g_mb_map_count, MB_MAP_MAX_LINES);
    int bad = 0;
    for (int i = 0; i < s_lines; i++) {
        const char *why = line_problem(&g_mb_map[i]);
        if (why) { s_line[i].disabled = true; bad++; ESP_LOGE(TAG, "mb_map.c line %d disabled: %s", i, why); }
    }
    s_img = xSemaphoreCreateMutex();
    if (!s_img || xTaskCreatePinnedToCore(poll_task, "mb_poll", 4096, NULL, 3, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "polling task NOT started: out of internal RAM");
        return;
    }
    s_started = true;
    ESP_LOGI(TAG, "polling %d line(s)%s, status bits from V%d.0", s_lines - bad,
             bad ? " (some disabled, see above)" : "", STATUS_VB);
}
#else
/* PC test build: no task; lines are checked and the exchange can be run by hand */
void mb_poll_start(void)
{
    s_lines = g_mb_map_count;
    for (int i = 0; i < s_lines; i++) s_line[i].disabled = line_problem(&g_mb_map[i]) != NULL;
    s_started = true;
}
#endif
