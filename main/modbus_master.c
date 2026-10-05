/* modbus_master.c - see modbus_master.h */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "modbus_master.h"
#include "mb_poll.h"

/* ================================================================== protocol core */
uint16_t mb_crc16(const uint8_t *d, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

static size_t put_crc(uint8_t *p, size_t n)
{
    uint16_t c = mb_crc16(p, n);
    p[n] = (uint8_t)(c & 0xFF);       /* CRC is sent low byte first */
    p[n + 1] = (uint8_t)(c >> 8);
    return n + 2;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint16_t get16(const uint8_t *p)   { return (uint16_t)((p[0] << 8) | p[1]); }

static bool count_ok(uint8_t fc, uint16_t count)
{
    switch (fc) {
    case 1: case 2: return count >= 1 && count <= 2000;
    case 3: case 4: return count >= 1 && count <= 125;
    case 15:        return count >= 1 && count <= 1968;
    case 16:        return count >= 1 && count <= 123;
    case 5: case 6: return true;
    default:        return false;
    }
}

size_t mb_build_request(uint8_t *o, uint8_t slave, uint8_t fc, uint16_t addr, uint16_t count,
                        const uint8_t *bits, const uint16_t *regs)
{
    if (slave > 247 || !count_ok(fc, count)) return 0;
    if (slave == 0 && fc != 5 && fc != 6 && fc != 15 && fc != 16) return 0;   /* broadcast = writes */
    if ((uint32_t)addr + (fc == 5 || fc == 6 ? 1 : count) > 0x10000u) return 0;
    size_t n = 0;
    o[n++] = slave;
    o[n++] = fc;
    put16(o + n, addr); n += 2;
    switch (fc) {
    case 1: case 2: case 3: case 4:
        put16(o + n, count); n += 2;
        break;
    case 5:
        put16(o + n, bits && bits[0] ? 0xFF00 : 0x0000); n += 2;
        break;
    case 6:
        put16(o + n, regs ? regs[0] : 0); n += 2;
        break;
    case 15: {
        uint8_t bytes = (uint8_t)((count + 7) / 8);
        put16(o + n, count); n += 2;
        o[n++] = bytes;
        memset(o + n, 0, bytes);
        for (uint16_t i = 0; i < count; i++) if (bits[i]) o[n + i / 8] |= (uint8_t)(1u << (i % 8));
        n += bytes;
        break;
    }
    case 16:
        put16(o + n, count); n += 2;
        o[n++] = (uint8_t)(count * 2);
        for (uint16_t i = 0; i < count; i++) { put16(o + n, regs[i]); n += 2; }
        break;
    }
    return put_crc(o, n);
}

size_t mb_expected_reply_len(uint8_t fc, uint16_t count, const uint8_t *first3)
{
    (void)count;
    if (first3[1] & 0x80) return 5;                       /* exception reply */
    switch (fc) {
    case 1: case 2: case 3: case 4: return 3u + first3[2] + 2u;
    case 5: case 6: case 15: case 16: return 8;
    default: return 0;
    }
}

mb_err_t mb_parse_reply(const uint8_t *rx, size_t len, uint8_t slave, uint8_t fc, uint16_t addr,
                        uint16_t count, uint8_t *bits, uint16_t *regs, uint8_t *exception)
{
    if (len < 5) return MB_ERR_FRAME;
    if (mb_crc16(rx, len - 2) != (uint16_t)(rx[len - 2] | (rx[len - 1] << 8))) return MB_ERR_CRC;
    if (rx[0] != slave) return MB_ERR_FRAME;
    if (rx[1] == (fc | 0x80)) {
        if (exception) *exception = rx[2];
        return MB_ERR_EXCEPTION;
    }
    if (rx[1] != fc) return MB_ERR_FRAME;
    switch (fc) {
    case 1: case 2: {
        uint8_t bytes = (uint8_t)((count + 7) / 8);
        if (rx[2] != bytes || len != 5u + bytes) return MB_ERR_FRAME;
        for (uint16_t i = 0; i < count; i++) bits[i] = (rx[3 + i / 8] >> (i % 8)) & 1;
        return MB_OK;
    }
    case 3: case 4:
        if (rx[2] != count * 2 || len != 5u + count * 2u) return MB_ERR_FRAME;
        for (uint16_t i = 0; i < count; i++) regs[i] = get16(rx + 3 + 2 * i);
        return MB_OK;
    case 5: case 6:
        if (len != 8 || get16(rx + 2) != addr) return MB_ERR_FRAME;   /* echo of the request */
        return MB_OK;
    case 15: case 16:
        if (len != 8 || get16(rx + 2) != addr || get16(rx + 4) != count) return MB_ERR_FRAME;
        return MB_OK;
    }
    return MB_ERR_FRAME;
}

const char *mb_err_str(mb_err_t e)
{
    switch (e) {
    case MB_OK:            return "OK";
    case MB_ERR_NOT_INIT:  return "Modbus not started";
    case MB_ERR_ARG:       return "bad argument";
    case MB_ERR_TIMEOUT:   return "no reply (timeout)";
    case MB_ERR_CRC:       return "reply has a bad CRC";
    case MB_ERR_FRAME:     return "bad reply (wrong slave, function or length)";
    case MB_ERR_EXCEPTION: return "slave exception";
    case MB_ERR_UART:      return "UART error";
    }
    return "?";
}

const char *mb_exception_str(uint8_t c)
{
    switch (c) {
    case MB_EX_ILLEGAL_FUNCTION:    return "illegal function";
    case MB_EX_ILLEGAL_ADDRESS:     return "illegal data address";
    case MB_EX_ILLEGAL_VALUE:       return "illegal data value";
    case MB_EX_SLAVE_FAILURE:       return "slave device failure";
    case MB_EX_ACKNOWLEDGE:         return "acknowledge (busy with a long command)";
    case MB_EX_SLAVE_BUSY:          return "slave device busy";
    case MB_EX_GATEWAY_PATH:        return "gateway path unavailable";
    case MB_EX_GATEWAY_NO_RESPONSE: return "gateway target did not respond";
    default:                        return "unknown exception";
    }
}

/* ================================================================== ESP32 transport */
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

static const char *TAG = "modbus";

#define MB_UART        ((uart_port_t)CONFIG_MB_UART_NUM)
#define MB_RX_BUF      512

static SemaphoreHandle_t s_lock;
static bool       s_ready;
static uint32_t   s_t35_us;            /* 3.5 character silence between frames */
static uint32_t   s_char_us;
static int64_t    s_last_activity_us;
static uint8_t    s_last_ex;
static mb_stats_t s_stats;

bool mb_master_ready(void) { return s_ready; }
uint8_t mb_last_exception(void) { return s_last_ex; }

bool mb_master_init(void)
{
    if (s_ready) return true;
    uart_config_t cfg = {
        .baud_rate  = CONFIG_MB_BAUD,
        .data_bits  = UART_DATA_8_BITS,
#if CONFIG_MB_PARITY_EVEN
        .parity     = UART_PARITY_EVEN,
#elif CONFIG_MB_PARITY_ODD
        .parity     = UART_PARITY_ODD,
#else
        .parity     = UART_PARITY_DISABLE,
#endif
#if CONFIG_MB_STOP_BITS_2
        .stop_bits  = UART_STOP_BITS_2,
#else
        .stop_bits  = UART_STOP_BITS_1,
#endif
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(MB_UART, MB_RX_BUF, 0, 0, NULL, 0);
    if (err == ESP_OK) err = uart_param_config(MB_UART, &cfg);
    if (err == ESP_OK) err = uart_set_pin(MB_UART, CONFIG_MB_TX_GPIO, CONFIG_MB_RX_GPIO,
                                          CONFIG_MB_DE_GPIO, UART_PIN_NO_CHANGE);   /* RTS = DE */
    if (err == ESP_OK) err = uart_set_mode(MB_UART, UART_MODE_RS485_HALF_DUPLEX);   /* DE high only while sending */
    if (err == ESP_OK) err = uart_set_rx_timeout(MB_UART, 3);                       /* flush RX FIFO after 3 idle chars */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART%d setup failed: %s", CONFIG_MB_UART_NUM, esp_err_to_name(err));
        return false;
    }
    /* character = start + 8 data + parity + stop bits */
    int bits = 1 + 8 + (cfg.parity != UART_PARITY_DISABLE) + (cfg.stop_bits == UART_STOP_BITS_2 ? 2 : 1);
    s_char_us = (uint32_t)(1000000ull * bits / CONFIG_MB_BAUD);
    s_t35_us = CONFIG_MB_BAUD > 19200 ? 1750 : s_char_us * 7 / 2;   /* Modbus spec: 1.75 ms above 19200 */
    s_lock = xSemaphoreCreateMutex();
    s_ready = s_lock != NULL;
    ESP_LOGI(TAG, "RS-485 master on UART%d: TX IO%d, RX IO%d, enable IO%d, %d baud %s, timeout %d ms",
             CONFIG_MB_UART_NUM, CONFIG_MB_TX_GPIO, CONFIG_MB_RX_GPIO, CONFIG_MB_DE_GPIO, CONFIG_MB_BAUD,
             cfg.parity == UART_PARITY_EVEN ? "8E" : cfg.parity == UART_PARITY_ODD ? "8O" : "8N",
             CONFIG_MB_TIMEOUT_MS);
    return s_ready;
}

/* read exactly n bytes, waiting up to wait_ms for the first and a few character times after that */
static int read_n(uint8_t *buf, size_t n, uint32_t first_wait_ms)
{
    size_t got = 0;
    uint32_t gap_ms = (s_char_us * 4 + 999) / 1000 + 5;    /* > 3.5 characters + scheduling slack */
    while (got < n) {
        int r = uart_read_bytes(MB_UART, buf + got, n - got, pdMS_TO_TICKS(got ? gap_ms : first_wait_ms) + 1);
        if (r < 0) return -1;
        if (r == 0) break;
        got += (size_t)r;
    }
    return (int)got;
}

/* one request/reply exchange, no retries */
static mb_err_t transact(const uint8_t *tx, size_t txlen, uint8_t slave, uint8_t fc, uint16_t addr,
                         uint16_t count, uint8_t *bits, uint16_t *regs)
{
    /* the bus must have been quiet for 3.5 characters before a new frame */
    int64_t quiet = esp_timer_get_time() - s_last_activity_us;
    if (quiet < s_t35_us) esp_rom_delay_us((uint32_t)(s_t35_us - quiet));

    uart_flush_input(MB_UART);
    if (uart_write_bytes(MB_UART, tx, txlen) != (int)txlen) return MB_ERR_UART;
    uart_wait_tx_done(MB_UART, pdMS_TO_TICKS(100 + txlen * s_char_us / 1000));
    s_last_activity_us = esp_timer_get_time();

    if (slave == 0) {                                   /* broadcast: no reply, give slaves time */
        vTaskDelay(pdMS_TO_TICKS(CONFIG_MB_BROADCAST_DELAY_MS));
        return MB_OK;
    }

    uint8_t rx[260];
    int r = read_n(rx, 3, CONFIG_MB_TIMEOUT_MS);
    s_last_activity_us = esp_timer_get_time();
    if (r < 0) return MB_ERR_UART;
    if (r == 0) return MB_ERR_TIMEOUT;
    if (r < 3) return MB_ERR_FRAME;
    size_t want = mb_expected_reply_len(fc, count, rx);
    if (want < 5 || want > sizeof(rx)) return MB_ERR_FRAME;
    r = read_n(rx + 3, want - 3, 20);
    s_last_activity_us = esp_timer_get_time();
    if (r < 0) return MB_ERR_UART;
    if ((size_t)r != want - 3) return rx[0] == slave ? MB_ERR_FRAME : MB_ERR_TIMEOUT;
    return mb_parse_reply(rx, want, slave, fc, addr, count, bits, regs, &s_last_ex);
}

static mb_err_t request(uint8_t slave, uint8_t fc, uint16_t addr, uint16_t count,
                        uint8_t *bits, uint16_t *regs, const uint8_t *wbits, const uint16_t *wregs)
{
    if (!s_ready) return MB_ERR_NOT_INIT;
    uint8_t tx[260];
    size_t n = mb_build_request(tx, slave, fc, addr, count, wbits, wregs);
    if (!n) return MB_ERR_ARG;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    mb_err_t e = MB_ERR_TIMEOUT;
    for (int attempt = 0; attempt <= CONFIG_MB_RETRIES; attempt++) {
        if (attempt) s_stats.retries++;
        s_stats.requests++;
        e = transact(tx, n, slave, fc, addr, count, bits, regs);
        switch (e) {
        case MB_OK:            s_stats.ok++; break;
        case MB_ERR_TIMEOUT:   s_stats.timeouts++; break;
        case MB_ERR_CRC:       s_stats.crc_errors++; break;
        case MB_ERR_FRAME:     s_stats.frame_errors++; break;
        case MB_ERR_EXCEPTION: s_stats.exceptions++; break;
        default: break;
        }
        /* an answer (good or an exception) won't change by asking again */
        if (e == MB_OK || e == MB_ERR_EXCEPTION || e == MB_ERR_UART) break;
    }
    xSemaphoreGive(s_lock);
    return e;
}

mb_err_t mb_read_coils(uint8_t s, uint16_t a, uint16_t n, uint8_t *bits)            { return request(s, 1, a, n, bits, NULL, NULL, NULL); }
mb_err_t mb_read_discrete_inputs(uint8_t s, uint16_t a, uint16_t n, uint8_t *bits)  { return request(s, 2, a, n, bits, NULL, NULL, NULL); }
mb_err_t mb_read_holding_registers(uint8_t s, uint16_t a, uint16_t n, uint16_t *r)  { return request(s, 3, a, n, NULL, r, NULL, NULL); }
mb_err_t mb_read_input_registers(uint8_t s, uint16_t a, uint16_t n, uint16_t *r)    { return request(s, 4, a, n, NULL, r, NULL, NULL); }
mb_err_t mb_write_coil(uint8_t s, uint16_t a, bool on)              { uint8_t b = on;  return request(s, 5, a, 1, NULL, NULL, &b, NULL); }
mb_err_t mb_write_register(uint8_t s, uint16_t a, uint16_t v)       { return request(s, 6, a, 1, NULL, NULL, NULL, &v); }
mb_err_t mb_write_coils(uint8_t s, uint16_t a, uint16_t n, const uint8_t *b)       { return request(s, 15, a, n, NULL, NULL, b, NULL); }
mb_err_t mb_write_registers(uint8_t s, uint16_t a, uint16_t n, const uint16_t *r)  { return request(s, 16, a, n, NULL, NULL, NULL, r); }

void mb_get_stats(mb_stats_t *out) { *out = s_stats; }
void mb_reset_stats(void)          { memset(&s_stats, 0, sizeof(s_stats)); }

/* ================================================================== console: "mb ..." */
static void mb_help(void)
{
    printf("Modbus master (RS-485, UART%d: TX IO%d, RX IO%d, enable IO%d, %d baud):\n"
           "  mb read SLAVE coil|di|hr|ir ADDR [COUNT]   read (addresses from 0: 40001 = hr 0)\n"
           "  mb write SLAVE coil|hr ADDR VALUE...       write one value, or several in a row\n"
           "  mb stats [reset]                           request / error counters\n"
           "  mb map                                     the PLC exchange table (mb_map.c) and its status\n"
           "  SLAVE 1-247 (0 = broadcast, writes only); values: 123, 16#FF, -5\n"
           "  e.g.  mb read 1 hr 0 10     mb write 1 hr 100 1500     mb write 2 coil 0 1 0 1\n",
           CONFIG_MB_UART_NUM, CONFIG_MB_TX_GPIO, CONFIG_MB_RX_GPIO, CONFIG_MB_DE_GPIO, CONFIG_MB_BAUD);
}

static bool num(const char *s, long *v)
{
    char *end;
    *v = !strncmp(s, "16#", 3) ? strtol(s + 3, &end, 16) : strtol(s, &end, 0);
    return end != s && *end == 0;
}

static void report(mb_err_t e)
{
    if (e == MB_ERR_EXCEPTION) printf("  error: exception %u, %s\n", s_last_ex, mb_exception_str(s_last_ex));
    else                       printf("  error: %s\n", mb_err_str(e));
}

void mb_console_command(const char *line)
{
    char buf[200];
    char *arg[40];
    int n = 0;
    snprintf(buf, sizeof(buf), "%s", line);
    for (char *t = strtok(buf, " \t,"); t && n < 40; t = strtok(NULL, " \t,")) arg[n++] = t;
    if (n < 2 || !strcmp(arg[1], "help")) { mb_help(); return; }
    if (!strcmp(arg[1], "map")) { mb_poll_print(); return; }
    if (!s_ready) { printf("  Modbus isn't running (see the boot log)\n"); return; }

    if (!strcmp(arg[1], "stats")) {
        if (n > 2 && !strcmp(arg[2], "reset")) { mb_reset_stats(); printf("  counters reset\n"); return; }
        printf("  %lu requests: %lu ok, %lu timeouts, %lu CRC errors, %lu bad replies, %lu exceptions, %lu retries\n",
               (unsigned long)s_stats.requests, (unsigned long)s_stats.ok, (unsigned long)s_stats.timeouts,
               (unsigned long)s_stats.crc_errors, (unsigned long)s_stats.frame_errors,
               (unsigned long)s_stats.exceptions, (unsigned long)s_stats.retries);
        return;
    }
    bool rd = !strcmp(arg[1], "read"), wr = !strcmp(arg[1], "write");
    long slave, addr, count = 1;
    if ((!rd && !wr) || n < 5 || !num(arg[2], &slave) || !num(arg[4], &addr) ||
        slave < 0 || slave > 247 || addr < 0 || addr > 65535) { mb_help(); return; }
    const char *type = arg[3];

    if (rd) {
        if (n > 5 && (!num(arg[5], &count) || count < 1 || count > 125)) { printf("  COUNT is 1-125\n"); return; }
        uint16_t regs[125];
        uint8_t bits[125];
        mb_err_t e;
        bool is_bits = !strcmp(type, "coil") || !strcmp(type, "di");
        if      (!strcmp(type, "coil")) e = mb_read_coils((uint8_t)slave, (uint16_t)addr, (uint16_t)count, bits);
        else if (!strcmp(type, "di"))   e = mb_read_discrete_inputs((uint8_t)slave, (uint16_t)addr, (uint16_t)count, bits);
        else if (!strcmp(type, "hr"))   e = mb_read_holding_registers((uint8_t)slave, (uint16_t)addr, (uint16_t)count, regs);
        else if (!strcmp(type, "ir"))   e = mb_read_input_registers((uint8_t)slave, (uint16_t)addr, (uint16_t)count, regs);
        else { printf("  type is coil, di, hr or ir\n"); return; }
        if (e != MB_OK) { report(e); return; }
        for (long i = 0; i < count; i++) {
            if (is_bits) printf("  %s %ld = %u\n", type, addr + i, bits[i]);
            else         printf("  %s %ld = %u  (signed %d, 16#%04X)\n", type, addr + i, regs[i], (int16_t)regs[i], regs[i]);
        }
        return;
    }

    /* write */
    int nv = n - 5;
    if (nv < 1) { printf("  write needs at least one VALUE\n"); return; }
    uint16_t regs[35];
    uint8_t bits[35];
    for (int i = 0; i < nv; i++) {
        long v;
        if (!num(arg[5 + i], &v)) { printf("  '%s' is not a value\n", arg[5 + i]); return; }
        regs[i] = (uint16_t)v;
        bits[i] = v != 0;
    }
    mb_err_t e;
    if (!strcmp(type, "coil"))
        e = nv == 1 ? mb_write_coil((uint8_t)slave, (uint16_t)addr, bits[0])
                    : mb_write_coils((uint8_t)slave, (uint16_t)addr, (uint16_t)nv, bits);
    else if (!strcmp(type, "hr"))
        e = nv == 1 ? mb_write_register((uint8_t)slave, (uint16_t)addr, regs[0])
                    : mb_write_registers((uint8_t)slave, (uint16_t)addr, (uint16_t)nv, regs);
    else { printf("  you can write coil or hr\n"); return; }
    if (e != MB_OK) report(e);
    else printf("  written%s\n", slave == 0 ? " (broadcast: no reply expected)" : "");
}
#endif /* ESP_PLATFORM */
