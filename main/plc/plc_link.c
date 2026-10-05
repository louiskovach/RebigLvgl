/* plc_link.c - see plc_link.h */
#include <string.h>
#include "plc_link.h"
#include "plc.h"
#include "params.h"
#include "mb_poll.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#if PLC_DEBUG
#include <stdio.h>
#include "plc_debug.h"
#include "modbus_master.h"
#include "driver/uart.h"
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#endif
#endif

#define SCAN_MS        10
#define PULSE_MS       200     /* how long a screen button holds an input on */
#define PROOF_DELAY_MS 1000    /* simulated pump: running signal 1 s after the pump output */

static const char *TAG = "plc";

static SemaphoreHandle_t s_lock;
static plc_snap_t        s_snap;

/* commands from the screens */
static volatile uint32_t s_pulse_start, s_pulse_stop, s_pulse_revive, s_pulse_reset, s_pulse_abort;
static volatile bool     s_manual_pump;

/* presets written into V memory every scan */
enum { PR_T37, PR_T38, PR_T39, PR_T40, PR_T41, PR_T42, PR_T43, PR_T44, PR_T45, PR_T47, PR_T60, PR_T61, PR_COUNT };
static int16_t       s_preset[PR_COUNT];      /* 0.1 s units, from the parameters */
static const int     k_preset_vw[PR_COUNT] = { 300, 302, 304, 306, 308, 310, 312, 314, 316, 318, 320, 322 };
/* the presets the program actually used in the last scan (read back from V memory, so a
   debugger force shows on the screen too); 16-bit, read by the screens without the lock */
static volatile int16_t s_used_preset[PR_COUNT];
static int16_t       s_bumps = 3;
static volatile bool s_fast;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ------------------------------------------------------------------ presets */
static int16_t tenths(float seconds)
{
    float v = seconds * 10.0f + 0.5f;
    if (v < 0) v = 0;
    if (v > 32767) v = 32767;            /* S7-200 preset limit: about 54 minutes */
    return (int16_t)v;
}

void plc_link_load_params(void)
{
    int16_t p[PR_COUNT] = {
        [PR_T37] = 50,
        [PR_T38] = tenths(param_get(P_PUMP_RUN_CONFIRM)),
        [PR_T39] = tenths(param_get(P_PRECOAT_MIN) * 60.0f),
        [PR_T40] = 50,
        [PR_T41] = tenths(param_get(P_REV_VALVE_CLOSE_DLY)),
        [PR_T42] = 50,
        [PR_T43] = tenths(param_get(P_EFF_VALVE_CLOSE_DLY)),
        [PR_T44] = tenths(param_get(P_PUMP_STOP_DLY)),
        [PR_T45] = 50,
        [PR_T47] = 50,
        [PR_T60] = tenths(param_get(P_REV_CYL_ON_S)),
        [PR_T61] = tenths(param_get(P_REV_CYL_OFF_S)),
    };
    int strokes = (int)(param_get(P_REVIVE_STROKES) + 0.5f);
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_preset, p, sizeof(p));
    s_bumps = (int16_t)(strokes < 1 ? 1 : strokes);
    if (s_lock) xSemaphoreGive(s_lock);
}

void plc_link_set_fast(bool fast) { s_fast = fast; }

static int16_t preset_now(int i)
{
    if (s_fast && i <= PR_T41) return 10;          /* Diagnostic Filter: start-up 1 s each */
    return s_preset[i];
}

/* The timer that times each step of the program */
static int state_timer(uint8_t state)
{
    switch (state) {
    case 1: return 37;  case 2: return 38;  case 3: return 39;  case 4: return 40;  case 5: return 41;
    case 10: return 42; case 11: return 43; case 12: return 44; case 13: return 45;
    case 16: return 60; /* bump down: off-delay T60 */
    case 17: return 61; /* bump up:   off-delay T61 */
    default: return -1;
    }
}

uint32_t plc_link_state_time_ms(uint8_t state)
{
    int i;
    switch (state) {
    case 1: i = PR_T37; break;  case 2: i = PR_T38; break;  case 3: i = PR_T39; break;
    case 4: i = PR_T40; break;  case 5: i = PR_T41; break;
    case 10: i = PR_T42; break; case 11: i = PR_T43; break; case 12: i = PR_T44; break;
    case 13: i = PR_T45; break;
    case 16: i = PR_T60; break; case 17: i = PR_T61; break;
    default: return 0;
    }
    int16_t pt = s_lock ? s_used_preset[i] : preset_now(i);    /* before the first scan: parameters */
    return pt > 0 ? (uint32_t)pt * 100u : 0;
}

/* ------------------------------------------------------------------ commands */
void plc_link_cmd_start(void)       { s_pulse_start = now_ms() + PULSE_MS; }
void plc_link_cmd_stop(void)        { s_pulse_stop = now_ms() + PULSE_MS; }
void plc_link_cmd_abort(void)      { s_pulse_abort = now_ms() + PULSE_MS; }
void plc_link_cmd_revive(void)      { s_pulse_revive = now_ms() + PULSE_MS; }
void plc_link_cmd_fault_reset(void) { s_pulse_reset = now_ms() + PULSE_MS; }
void plc_link_manual_pump(bool on)  { s_manual_pump = on; }

void plc_link_get(plc_snap_t *out)
{
    if (!s_lock) { memset(out, 0, sizeof(*out)); return; }   /* PLC not started yet: idle */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_snap;
    xSemaphoreGive(s_lock);
}

/* ------------------------------------------------------------------ task */
/* real = clock (button pulses), t = PLC time (simulated pump, frozen at breakpoints) */
static void inputs_before_scan(uint32_t real, uint32_t t)
{
    static uint32_t pump_on_since;

    plc_wr_bit('I', 0, 0, (int32_t)(s_pulse_start - real) > 0);     /* Start */
    plc_wr_bit('I', 1, 2, (int32_t)(s_pulse_stop - real) > 0);      /* Stop */
    plc_wr_bit('I', 0, 4, (int32_t)(s_pulse_revive - real) > 0);    /* Bump / Revive */
    plc_wr_bit('I', 1, 0, !((int32_t)(s_pulse_reset - real) > 0));  /* Fault reset: normally 1 */
    plc_wr_bit('I', 0, 2, (int32_t)(s_pulse_abort - real) > 0);     /* Immediate stop (aborts a revive) */

    /* SIMULATED pump: the running signal follows the pump output after a short delay */
    bool pump = plc_rd_bit('Q', 0, 0);
    if (!pump) pump_on_since = 0;
    else if (!pump_on_since) pump_on_since = t ? t : 1;   /* t = PLC time */
    plc_wr_bit('I', 1, 3, pump && t - pump_on_since >= PROOF_DELAY_MS);

    /* HMI -> PLC memory */
    for (int i = 0; i < PR_COUNT; i++) plc_wr_word('V', k_preset_vw[i], preset_now(i));
    plc_wr_word('V', 0, s_bumps);
    plc_wr_bit('V', 250, 0, s_manual_pump);
}

static void snapshot_after_scan(void)
{
    s_snap.state = plc_rd_byte('V', 202);
    s_snap.ib[0] = plc_rd_byte('I', 0);
    s_snap.ib[1] = plc_rd_byte('I', 1);
    s_snap.qb[0] = plc_rd_byte('Q', 0);
    s_snap.qb[1] = plc_rd_byte('Q', 1);
    s_snap.pump_fault = plc_rd_bit('V', 210, 0);
    s_snap.manual_pump = plc_rd_bit('V', 250, 0);
    s_snap.bumps_done = plc_c_count(0);
    s_snap.bumps_total = plc_rd_word('V', 0);
    for (int i = 0; i < PR_COUNT; i++) s_used_preset[i] = plc_rd_word('V', k_preset_vw[i]);
    int t = state_timer(s_snap.state);
    s_snap.step_ms = t >= 0 ? (uint32_t)plc_t_acc(t) * 100u : 0;
}

/* Scan times in SMW22 / SMW24 / SMW26 (ms), like the S7-200 */
static void scan_time_sm(uint32_t us, bool reset)
{
    static uint32_t lo = UINT32_MAX, hi;
    if (reset) { lo = UINT32_MAX; hi = 0; }
    uint32_t ms = (us + 500) / 1000;
    if (ms < lo) lo = ms;
    if (ms > hi) hi = ms;
    plc_wr_word('X', 22, (int16_t)ms);
    plc_wr_word('X', 24, (int16_t)lo);
    plc_wr_word('X', 26, (int16_t)hi);
}

static void plc_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    uint32_t real_last = now_ms();
    uint32_t vt = 0;           /* PLC time: stands still while paused or stopped at a breakpoint */
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SCAN_MS));
        uint32_t real = now_ms();
        xSemaphoreTake(s_lock, portMAX_DELAY);
#if PLC_DEBUG
        plc_debug_begin_scan();
        bool scanning = plc_debug_should_scan();
        if (scanning) vt += real - real_last;
        real_last = real;
        inputs_before_scan(real, vt);
        mb_poll_apply_inputs();                  /* Modbus slaves -> PLC memory (overrides simulation) */
        if (scanning) {
            plc_debug_before_scan();
            int64_t t0 = esp_timer_get_time();
            plc_scan(plc_main, vt);
            uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
            uint32_t stopped = plc_debug_take_stopped_ms();
            if (!stopped) scan_time_sm(us, false);
            plc_debug_after_scan(vt, us);
            real_last += stopped;            /* time at a breakpoint doesn't count */
            mb_poll_capture_outputs();       /* PLC memory -> values for the Modbus slaves */
        }
#else
        vt += real - real_last;
        real_last = real;
        inputs_before_scan(real, vt);
        mb_poll_apply_inputs();                  /* Modbus slaves -> PLC memory */
        int64_t t0 = esp_timer_get_time();
        plc_scan(plc_main, vt);
        scan_time_sm((uint32_t)(esp_timer_get_time() - t0), false);
        mb_poll_capture_outputs();               /* PLC memory -> values for the Modbus slaves */
#endif
        snapshot_after_scan();
#if PLC_DEBUG
        s_snap.halted = !scanning;
#endif
        xSemaphoreGive(s_lock);
    }
}

#if PLC_DEBUG
/* ------------------------------------------------------------------ debug console */
#ifdef CONFIG_ESP_CONSOLE_UART_NUM
#define CONSOLE_UART CONFIG_ESP_CONSOLE_UART_NUM
#else
#define CONSOLE_UART UART_NUM_0
#endif

/* one character from whichever port the console (log) is on */
static int console_getc(void)
{
    uint8_t c;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) == 1) return c;
#else
    if (uart_read_bytes(CONSOLE_UART, &c, 1, portMAX_DELAY) == 1) return c;
#endif
    return -1;
}

/* Called by the debugger over and over while the program is stopped at a breakpoint
 * (inside the PLC task, in the middle of a scan): let the console and the screens in. */
static void wait_while_stopped(void)
{
    s_snap.halted = true;
    xSemaphoreGive(s_lock);
    vTaskDelay(pdMS_TO_TICKS(10));
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static uint32_t dbg_now_ms(void) { return now_ms(); }

/* Reads lines from the serial monitor (with echo and backspace) and runs them as commands */
static void console_task(void *arg)
{
    (void)arg;
    char line[128];
    int  len = 0;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&cfg);
#else
    if (!uart_is_driver_installed(CONSOLE_UART))
        uart_driver_install(CONSOLE_UART, 256, 0, 0, NULL, 0);
#endif
    printf("\nPLC debugger ready - type help\nplc> ");
    fflush(stdout);
    for (;;) {
        int c = console_getc();
        if (c < 0) continue;
        if (c == '\r' || c == '\n') {
            if (!len && c == '\n') continue;          /* CR LF: ignore the LF */
            line[len] = 0;
            printf("\n");
            if (!strncmp(line, "mb", 2) && (line[2] == 0 || line[2] == ' ')) {
                mb_console_command(line);            /* Modbus: blocking, so NOT under the PLC lock */
                len = 0;
                xSemaphoreTake(s_lock, portMAX_DELAY);
                printf("%s", plc_debug_prompt());
                xSemaphoreGive(s_lock);
                fflush(stdout);
                continue;
            }
            xSemaphoreTake(s_lock, portMAX_DELAY);
            bool resumed = plc_debug_command(line);
            xSemaphoreGive(s_lock);
            len = 0;
            if (resumed) {
                /* the program carries on: if it stops again it prints "(break) " itself */
                vTaskDelay(pdMS_TO_TICKS(50));
                xSemaphoreTake(s_lock, portMAX_DELAY);
                bool stopped = plc_debug_stopped();
                xSemaphoreGive(s_lock);
                if (!stopped) printf("plc> ");
            } else {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                printf("%s", plc_debug_prompt());
                xSemaphoreGive(s_lock);
            }
        } else if (c == 8 || c == 127) {             /* backspace */
            if (len) { len--; printf("\b \b"); }
        } else if (c >= 32 && len < (int)sizeof(line) - 1) {
            line[len++] = (char)c;
            putchar(c);                              /* echo */
        }
        fflush(stdout);
    }
}
#endif

void plc_link_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    plc_reset();
    plc_link_load_params();
#if PLC_DEBUG
    plc_debug_init(wait_while_stopped, dbg_now_ms);
    if (xTaskCreatePinnedToCore(console_task, "plc_dbg", 4096, NULL, 2, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "debugger console NOT started: out of internal RAM");
    else
        ESP_LOGW(TAG, "PLC debugger is ON (force/write available) - turn off for production");
#endif
    if (xTaskCreatePinnedToCore(plc_task, "plc", 4096, NULL, 5, NULL, 0) != pdPASS) {  /* core 0 */
        ESP_LOGE(TAG, "PLC task NOT started: out of internal RAM");
        return;
    }
    ESP_LOGI(TAG, "PLC task started (%d ms scan, simulated I/O)", SCAN_MS);
}
