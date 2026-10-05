/* plc_debug.c - see plc_debug.h */
#include "plc.h"
#if PLC_DEBUG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <math.h>
#include "plc_debug.h"

#define MAX_FORCES    16
#define MAX_WATCHES   16
#define MAX_BREAKS    16
#define MAX_ERRORS    16
#define MAX_LINES     4096      /* lines of the .awl remembered as "has an instruction" */
#define MAX_NETS      1024
#define PRINT_ERRORS  10        /* errors printed as they happen; the rest only go in the log */

/* ================================================================== addresses */
typedef enum { K_BIT, K_BYTE, K_WORD, K_DWORD, K_TIMER, K_COUNTER } kind_t;
typedef enum { F_DEC, F_HEX, F_REAL, F_STR } fmt_t;
typedef struct {
    kind_t k;
    fmt_t  f;
    char   area;
    int    byte, bit;
    char   name[24];
} addr_t;

static const char *area_name(char a)
{
    switch (a) {
    case 'X': return "SM"; case 'A': return "AI"; case 'O': return "AQ";
    case 'I': return "I";  case 'Q': return "Q";  case 'M': return "M";
    case 'V': return "V";  case 'S': return "S";  case 'T': return "T";
    case 'C': return "C";  case 'E': return "edge";
    default:  return "?";
    }
}

static int size_of(kind_t k) { return k == K_WORD ? 2 : k == K_DWORD ? 4 : 1; }

static bool parse_addr(const char *in, addr_t *a)
{
    char buf[24];
    size_t n = 0;
    for (; in[n] && n < sizeof(buf) - 1; n++) buf[n] = (char)toupper((unsigned char)in[n]);
    buf[n] = 0;
    memset(a, 0, sizeof(*a));
    char *colon = strchr(buf, ':');
    if (colon) {
        if      (!strcmp(colon, ":X")) a->f = F_HEX;
        else if (!strcmp(colon, ":R")) a->f = F_REAL;
        else if (!strcmp(colon, ":S")) a->f = F_STR;
        else return false;
        *colon = 0;
    }
    snprintf(a->name, sizeof(a->name), "%s", buf);
    const char *p = buf;
    char *end;

    if ((p[0] == 'T' || p[0] == 'C') && isdigit((unsigned char)p[1])) {
        a->k = p[0] == 'T' ? K_TIMER : K_COUNTER;
        a->area = p[0];
        a->byte = (int)strtol(p + 1, &end, 10);
        return *end == 0 && a->byte >= 0 && a->byte < 256 && (a->f == F_DEC || a->f == F_HEX);
    }
    if      (!strncmp(p, "SM", 2)) { a->area = 'X'; p += 2; }
    else if (!strncmp(p, "AI", 2)) { a->area = 'A'; p += 2; if (*p != 'W') return false; }
    else if (!strncmp(p, "AQ", 2)) { a->area = 'O'; p += 2; if (*p != 'W') return false; }
    else if (*p && strchr("IQMVS", *p)) { a->area = *p; p++; }
    else return false;

    if      (*p == 'B') { a->k = K_BYTE;  p++; }
    else if (*p == 'W') { a->k = K_WORD;  p++; }
    else if (*p == 'D') { a->k = K_DWORD; p++; }
    else                  a->k = K_BIT;
    if (!isdigit((unsigned char)*p)) return false;
    a->byte = (int)strtol(p, &end, 10);
    if (a->k == K_BIT) {
        if (*end != '.' || end[1] < '0' || end[1] > '7' || end[2]) return false;
        a->bit = end[1] - '0';
    } else if (*end) {
        return false;
    }
    if (a->f == F_REAL && a->k != K_DWORD) return false;     /* :r needs a double word */
    if (a->f == F_STR && a->k != K_BYTE) return false;       /* :s needs a byte (length) */
    if (a->f == F_HEX && a->k == K_BIT) return false;
    return plc_valid(a->area, a->byte, a->f == F_STR ? 1 : size_of(a->k));
}

static void bad_addr(const char *s)
{
    printf("'%s' is not a valid address (e.g. I0.0 VB202 VW300 VD100:r T37 C0 SMB0)\n", s);
}

/* raw value: one integer that changes whenever the value changes */
static int64_t rd_raw(const addr_t *a)
{
    switch (a->k) {
    case K_BIT:     return plc_rd_bit(a->area, a->byte, a->bit);
    case K_BYTE:    return plc_rd_byte(a->area, a->byte);
    case K_WORD:    return plc_rd_word(a->area, a->byte);
    case K_DWORD:   return plc_rd_dword(a->area, a->byte);
    case K_TIMER:   return (int64_t)plc_t_acc(a->byte) * 2 + plc_t_bit(a->byte);
    case K_COUNTER: return (int64_t)plc_c_count(a->byte) * 2 + plc_c_bit(a->byte);
    }
    return 0;
}

static float raw_to_real(int64_t raw) { uint32_t u = (uint32_t)raw; float f; memcpy(&f, &u, 4); return f; }

/* numeric value, for conditions */
static double rd_num(const addr_t *a)
{
    int64_t raw = rd_raw(a);
    if (a->f == F_REAL) return raw_to_real(raw);
    if (a->k == K_TIMER || a->k == K_COUNTER) return (double)(raw >> 1);
    return (double)raw;
}

static void fmt_raw(const addr_t *a, int64_t raw, char *out, size_t len)
{
    switch (a->k) {
    case K_TIMER: case K_COUNTER:
        snprintf(out, len, "%ld (bit %d)", (long)(raw >> 1), (int)(raw & 1));
        return;
    default: break;
    }
    if (a->f == F_REAL) { snprintf(out, len, "%g", (double)raw_to_real(raw)); return; }
    if (a->f == F_HEX) {
        int digits = size_of(a->k) * 2;
        uint32_t mask = a->k == K_DWORD ? 0xFFFFFFFFu : (1u << (digits * 4)) - 1;
        snprintf(out, len, "16#%0*lX", digits, (unsigned long)((uint32_t)raw & mask));
        return;
    }
    snprintf(out, len, "%ld", (long)raw);
}

static void fmt_value(const addr_t *a, char *out, size_t len)
{
    if (a->f == F_STR) {
        int n = plc_rd_byte(a->area, a->byte);
        size_t p = 0;
        out[p++] = '\'';
        for (int i = 0; i < n && p + 3 < len; i++) {
            if (!plc_valid(a->area, a->byte + 1 + i, 1)) break;
            char c = (char)plc_rd_byte(a->area, a->byte + 1 + i);
            out[p++] = isprint((unsigned char)c) ? c : '.';
        }
        out[p++] = '\'';
        out[p] = 0;
        return;
    }
    fmt_raw(a, rd_raw(a), out, len);
}

static bool parse_int(const char *s, int64_t *v)
{
    char *end;
    if      (!strncmp(s, "16#", 3)) *v = (int64_t)strtoul(s + 3, &end, 16);
    else if (!strncmp(s, "2#", 2))  *v = (int64_t)strtoul(s + 2, &end, 2);
    else                            *v = strtoll(s, &end, 10);
    return end != s && *end == 0;
}

static bool parse_num(const char *s, const addr_t *a, double *v)
{
    if (a->f == F_REAL) { char *end; *v = strtod(s, &end); return end != s && *end == 0; }
    int64_t i;
    if (!parse_int(s, &i)) return false;
    *v = (double)i;
    return true;
}

/* write a value given as text; returns false with a message on error */
static bool wr_text(const addr_t *a, const char *s)
{
    if (a->k == K_TIMER || a->k == K_COUNTER) { printf("%s can't be written; change its input instead\n", a->name); return false; }
    if (a->f == F_STR) {
        size_t n = strlen(s);
        if (n < 2 || s[0] != '\'' || s[n - 1] != '\'') { printf("a string value is written 'TEXT'\n"); return false; }
        n -= 2;
        if (n > 254 || !plc_valid(a->area, a->byte, (int)n + 1)) { printf("string too long\n"); return false; }
        plc_wr_byte(a->area, a->byte, (uint8_t)n);
        for (size_t i = 0; i < n; i++) plc_wr_byte(a->area, a->byte + 1 + (int)i, (uint8_t)s[1 + i]);
        return true;
    }
    if (a->f == F_REAL) {
        char *end; float f = strtof(s, &end);
        if (end == s || *end) { printf("'%s' is not a number\n", s); return false; }
        uint32_t u; memcpy(&u, &f, 4);
        plc_wr_dword(a->area, a->byte, (int32_t)u);
        return true;
    }
    int64_t v;
    if (!parse_int(s, &v)) { printf("'%s' is not a value (123, -5, 16#FF, 2#1010)\n", s); return false; }
    switch (a->k) {
    case K_BIT:   if (v != 0 && v != 1) { printf("a bit is 0 or 1\n"); return false; }
                  plc_wr_bit(a->area, a->byte, a->bit, (int)v); break;
    case K_BYTE:  plc_wr_byte(a->area, a->byte, (uint8_t)v); break;
    case K_WORD:  plc_wr_word(a->area, a->byte, (int16_t)v); break;
    case K_DWORD: plc_wr_dword(a->area, a->byte, (int32_t)v); break;
    default: break;
    }
    return true;
}

/* ================================================================== state */
typedef struct { addr_t a; char text[260]; } force_t;
typedef struct { addr_t a; int64_t last; bool first; } watch_t;

typedef enum { BP_LINE, BP_NET, BP_CHANGE } bp_kind_t;
typedef enum { OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE } op_t;
typedef struct {
    bool      used;
    bp_kind_t kind;
    int       line, net;
    addr_t    a;            /* change: watched address */
    bool      has_to;
    double    to;
    int64_t   last;
    bool      has_cond;
    addr_t    ca;
    op_t      op;
    double    cv;
    uint32_t  hits;
} bp_t;

typedef struct {
    char     msg[40];
    char     area;
    int      addr, line, net;
    const char *text;
    uint32_t count;
} err_t;

static PLC_PSRAM force_t s_force[MAX_FORCES];
static int               s_nforce;
static PLC_PSRAM watch_t s_watch[MAX_WATCHES];
static int               s_nwatch;
static PLC_PSRAM bp_t    s_bp[MAX_BREAKS];
static PLC_PSRAM err_t   s_err[MAX_ERRORS];
static int               s_nerr;
static uint32_t          s_err_printed;
static PLC_PSRAM uint8_t s_line_seen[MAX_LINES / 8];
static PLC_PSRAM uint8_t s_net_seen[MAX_NETS / 8];
static int               s_net_first_line[MAX_NETS];
static int               s_max_line;

static void (*s_wait)(void);
static uint32_t (*s_now)(void);

static bool     s_paused;
static int      s_scan_steps;            /* paused: scans to run */
static uint32_t s_scans;
static bool     s_scanned_once;
static uint32_t s_scan_last, s_scan_min = UINT32_MAX, s_scan_max;
static bool     s_req_reset, s_req_restart;

/* where the program is */
static bool        s_in_scan;
static int         s_cur_net, s_cur_line;
static const char *s_cur_text;
static int         s_prev_net, s_prev_line;     /* previous instruction in this scan, 0 = none */
static const char *s_prev_text;

/* stopping */
static volatile bool s_stopped;
static int      s_si;                    /* instructions to run before stopping again */
static bool     s_stop_in_scan;          /* this scan stopped somewhere (scan time not valid) */
static uint32_t s_stopped_ms;

/* trace */
static int  s_trace_net = -1;            /* -1 off, 0 all, >0 network: armed for the next scan */
static bool s_tracing;
static int  s_tr_net, s_tr_line;
static const char *s_tr_text;

/* ================================================================== helpers */
static void stack_text(char *out, size_t len)
{
    uint32_t st = plc_stack_bits();
    size_t p = 0;
    for (int i = 0; i < 9 && p + 3 < len; i++) {
        out[p++] = (st >> i) & 1 ? '1' : '0';
        out[p++] = ' ';
        if (i == 0) { out[p++] = '|'; out[p++] = ' '; }
    }
    out[p ? p - 1 : 0] = 0;
}

static void where(int line, int net, const char *text)
{
    printf("%s:%d (MAIN network %d): %s", plc_program_file, line, net, text ? text : "");
}

static bool cond_true(const bp_t *b)
{
    if (!b->has_cond) return true;
    double v = rd_num(&b->ca);
    switch (b->op) {
    case OP_EQ: return v == b->cv;
    case OP_NE: return v != b->cv;
    case OP_LT: return v <  b->cv;
    case OP_GT: return v >  b->cv;
    case OP_LE: return v <= b->cv;
    case OP_GE: return v >= b->cv;
    }
    return true;
}

static void refresh_changes(void)
{
    for (int i = 0; i < MAX_BREAKS; i++)
        if (s_bp[i].used && s_bp[i].kind == BP_CHANGE) s_bp[i].last = rd_raw(&s_bp[i].a);
}

static void apply_forces(void)
{
    for (int i = 0; i < s_nforce; i++) {
        addr_t *a = &s_force[i].a;
        if (a->f == F_STR || a->f == F_REAL) { wr_text(a, s_force[i].text); continue; }
        int64_t v;
        parse_int(s_force[i].text, &v);
        switch (a->k) {
        case K_BIT:   plc_wr_bit(a->area, a->byte, a->bit, (int)v); break;
        case K_BYTE:  plc_wr_byte(a->area, a->byte, (uint8_t)v); break;
        case K_WORD:  plc_wr_word(a->area, a->byte, (int16_t)v); break;
        case K_DWORD: plc_wr_dword(a->area, a->byte, (int32_t)v); break;
        default: break;
        }
    }
}

/* Stop here until "c" / "si" / "step": called from the PLC task, in the middle of a scan.
 * at_end = stopped after the last instruction of the scan. */
static void stop_now(bool at_end)
{
    char st[40];
    stack_text(st, sizeof(st));
    if (at_end) {
        printf("  stopped at the end of the scan\n");
    } else {
        printf("  stopped before ");
        where(s_cur_line, s_cur_net, s_cur_text);
        printf("\n  logic stack before it, top first: %s\n", st);
    }
    printf("(break) ");
    fflush(stdout);
    s_stop_in_scan = true;
    s_stopped = true;
    uint32_t t0 = s_now();
    while (s_stopped) s_wait();
    s_stopped_ms += s_now() - t0;
    refresh_changes();                   /* console writes while stopped aren't "changes" */
}

/* a changepoint fired: report who changed it */
static bool check_changes(bool at_end)
{
    bool stop = false;
    for (int i = 0; i < MAX_BREAKS; i++) {
        bp_t *b = &s_bp[i];
        if (!b->used || b->kind != BP_CHANGE) continue;
        int64_t v = rd_raw(&b->a);
        if (v == b->last) continue;
        int64_t old = b->last;
        b->last = v;
        bool to_ok = !b->has_to || (b->a.f == F_REAL ? raw_to_real(v) == b->to
                                    : (b->a.k == K_TIMER || b->a.k == K_COUNTER) ? (double)(v >> 1) == b->to
                                    : (double)v == b->to);
        if (!to_ok || !cond_true(b)) continue;
        b->hits++;
        char o[40], n[40];
        fmt_raw(&b->a, old, o, sizeof(o));
        fmt_raw(&b->a, v, n, sizeof(n));
        printf("Changepoint %d: %s changed from %s to %s\n", i + 1, b->a.name, o, n);
        if (s_prev_line) { printf("  changed by "); where(s_prev_line, s_prev_net, s_prev_text); printf("\n"); }
        else               printf("  changed between scans\n");
        stop = true;
    }
    if (stop) { s_si = 0; stop_now(at_end); }
    return stop;
}

/* ================================================================== hooks from plc.c */
static void trace_flush(void)
{
    if (!s_tr_text) return;
    char st[40];
    stack_text(st, sizeof(st));
    printf("  %s:%-4d N%-3d %-24s stack %s\n", plc_program_file, s_tr_line, s_tr_net, s_tr_text, st);
    s_tr_text = NULL;
}

static void at_hook(int net, int line, const char *stl)
{
    if (line > 0 && line < MAX_LINES) s_line_seen[line / 8] |= (uint8_t)(1u << (line % 8));
    if (line > s_max_line) s_max_line = line;
    bool new_net = net != s_cur_net || !s_prev_line;
    if (net > 0 && net < MAX_NETS && !(s_net_seen[net / 8] & (1u << (net % 8)))) {
        s_net_seen[net / 8] |= (uint8_t)(1u << (net % 8));
        s_net_first_line[net] = line;
    }
    s_cur_net = net; s_cur_line = line; s_cur_text = stl;

    if (s_tracing) {
        trace_flush();
        if (s_trace_net == 0 || net == s_trace_net) { s_tr_net = net; s_tr_line = line; s_tr_text = stl; }
    }

    /* changes made by the previous instruction (or between scans, before the first one) */
    bool stopped = check_changes(false);

    for (int i = 0; i < MAX_BREAKS && !stopped; i++) {
        bp_t *b = &s_bp[i];
        if (!b->used) continue;
        bool at = (b->kind == BP_LINE && b->line == line) || (b->kind == BP_NET && b->net == net && new_net);
        if (!at || !cond_true(b)) continue;
        b->hits++;
        if (b->kind == BP_LINE) printf("Breakpoint %d at line %d\n", i + 1, line);
        else                    printf("Breakpoint %d: start of MAIN network %d\n", i + 1, net);
        s_si = 0;
        stop_now(false);
        stopped = true;
    }
    /* stepping: count this instruction (unless we just stopped before it: then stepping
       starts with the next one) */
    if (!stopped && s_si > 0 && --s_si == 0) {
        printf("Step\n");
        stop_now(false);
    }
    s_prev_net = net; s_prev_line = line; s_prev_text = stl;
}

static void error_hook(const char *msg, char area, int addr)
{
    int line = s_in_scan ? s_cur_line : 0;
    for (int i = 0; i < s_nerr; i++)
        if (s_err[i].line == line && s_err[i].area == area && s_err[i].addr == addr && !strcmp(s_err[i].msg, msg)) {
            s_err[i].count++;
            return;
        }
    if (s_nerr < MAX_ERRORS) {
        err_t *e = &s_err[s_nerr++];
        snprintf(e->msg, sizeof(e->msg), "%s", msg);
        e->area = area; e->addr = addr; e->line = line; e->net = s_cur_net;
        e->text = s_in_scan ? s_cur_text : NULL;
        e->count = 1;
    }
    if (s_err_printed < PRINT_ERRORS) {
        printf("PLC error: %s (%s %d)", msg, area_name(area), addr);
        if (line) { printf(" at "); where(line, s_cur_net, s_cur_text); }
        printf("\n");
        if (++s_err_printed == PRINT_ERRORS) printf("PLC error: more errors only go in the error log (errors)\n");
    }
}

/* ================================================================== scan hooks */
void plc_debug_init(void (*wait)(void), uint32_t (*now_ms)(void))
{
    s_wait = wait;
    s_now = now_ms;
    static const plc_hooks_t hooks = { .at = at_hook, .error = error_hook };
    plc_set_hooks(&hooks);
}

bool plc_debug_stopped(void) { return s_stopped; }
const char *plc_debug_prompt(void) { return s_stopped ? "(break) " : "plc> "; }

uint32_t plc_debug_take_stopped_ms(void) { uint32_t v = s_stopped_ms; s_stopped_ms = 0; return v; }

void plc_debug_begin_scan(void)
{
    if (s_req_reset) {
        s_req_reset = s_req_restart = false;
        plc_reset();
        refresh_changes();
        printf("reset done: this scan is a first scan (SM0.1 and SM0.3 on)\n");
    } else if (s_req_restart) {
        s_req_restart = false;
        plc_restart();
        printf("restart done: this scan is a first scan (SM0.1 on)\n");
    }
}

bool plc_debug_should_scan(void)
{
    if (!s_paused) return true;
    if (s_scan_steps > 0) { s_scan_steps--; return true; }
    apply_forces();                      /* forcing outputs still works while paused */
    return false;
}

void plc_debug_before_scan(void)
{
    apply_forces();
    s_in_scan = true;
    s_prev_line = 0; s_prev_net = 0; s_prev_text = NULL;
    s_cur_net = 0;
    s_stop_in_scan = false;
    if (s_trace_net >= 0) {
        s_tracing = true;
        printf("--- trace of scan %lu: each instruction with the logic stack after it, top first ---\n",
               (unsigned long)s_scans + 1);
    }
}

void plc_debug_after_scan(uint32_t now_ms, uint32_t scan_us)
{
    if (s_tracing) {
        trace_flush();
        printf("--- end of trace ---\n");
        s_tracing = false;
        s_trace_net = -1;
    }
    check_changes(true);                 /* changed by the last instruction of the scan */
    s_in_scan = false;
    apply_forces();                      /* forced outputs win over the program */
    s_scans++;
    s_scanned_once = true;
    if (!s_stop_in_scan) {
        s_scan_last = scan_us;
        if (scan_us < s_scan_min) s_scan_min = scan_us;
        if (scan_us > s_scan_max) s_scan_max = scan_us;
    }
    for (int i = 0; i < s_nwatch; i++) {
        int64_t v = rd_raw(&s_watch[i].a);
        if (s_watch[i].first || v != s_watch[i].last) {
            char out[280];
            fmt_value(&s_watch[i].a, out, sizeof(out));
            printf("%9.2f s  %-10s = %s\n", now_ms / 1000.0, s_watch[i].a.name, out);
            s_watch[i].last = v;
            s_watch[i].first = false;
        }
    }
    refresh_changes();                   /* forces/inputs applied after this aren't program changes */
}

/* ================================================================== commands */
static const char *const help_lines[] = {
    "Values:",
    "  read ADDR...             show values: read VB202 Q0.0 T37 VD100:r VB100:s",
    "  write ADDR VALUE         change a value: write VW10 500, write VD0:r 1.5",
    "  force ADDR VALUE         hold a value every scan: force I0.0 1",
    "  unforce ADDR | all       release forced values",
    "  forces                   list forced values",
    "  watch ADDR...            print values whenever they change",
    "  watch                    list watched values;  watch off  stops watching",
    "",
    "Tracing:",
    "  trace NET [BLOCK]        next scan: each instruction of network NET (block",
    "                           MAIN by default) with the logic stack after it",
    "  trace all                next scan: every instruction with the stack",
    "  trace off                cancel a trace",
    "  stack                    where the program is, and the logic stack",
    "",
    "Scanning:",
    "  pause | resume           stop / restart scanning (outputs hold)",
    "  step [N]                 run N scans while paused (default 1)",
    "  run                      leave STOP mode",
    "  reset                    like a power cycle: memory cleared, next scan is a",
    "                           first scan (SM0.1 and SM0.3 on)",
    "  restart                  like STOP -> RUN: memory kept, SM0.1 on next scan",
    "",
    "Breakpoints (stop in the middle of a scan, before an instruction):",
    "  break LINE               stop at a line of the .awl file: break 21",
    "  break net N [BLOCK]      stop where network N starts: break net 3",
    "  break change ADDR        stop right after the instruction that changes",
    "                           ADDR, and show which one: break change VB202",
    "  break change ADDR to V   only when it changes to V: break change VB202 to 3",
    "  ... if ADDR OP VALUE     add to any break: only stop when this is true,",
    "                           OP is == != < > <= >=: break 21 if VB202 == 3",
    "  breaks                   list breakpoints and how often each was hit",
    "  delete N | all           remove breakpoints",
    "When stopped at a breakpoint (prompt: (break)):",
    "  c | continue             carry on with the scan",
    "  si [N] | step [N]        run N instructions (default 1) and stop again",
    "  (all other commands work too: read, write, stack, break ...)",
    "",
    "Information:",
    "  scantime [reset]         last / shortest / longest scan time",
    "  errors [clear]           error log, with the .awl line of each error",
    "  status                   RUN/STOP, paused, scans, errors, forces, breakpoints",
    "  help                     this list",
    "Short forms: r = read, w = write, b = break, c = continue, s = step, ? = help",
    "Modbus (RS-485): mb help",
    "",
    "Addresses: I0.0 Q0.1 V204.0 M0.0 S0.1 SM0.5 VB202 VW10 VD100 SMW22 AIW0 AQW0 T37 C0",
    "  add :r (real), :x (hex) or :s (string): read VD100:r, read VB100:s",
    "Values: 123  -5  16#FF  2#1010  1.5 (with :r)  'TEXT' (with :s)",
};

static void cmd_help(void)
{
    for (size_t k = 0; k < sizeof(help_lines) / sizeof(help_lines[0]); k++) printf("%s\n", help_lines[k]);
}

static void cmd_read(char **arg, int n)
{
    if (!n) { printf("read what? e.g. read VB202 T37\n"); return; }
    for (int i = 0; i < n; i++) {
        addr_t a;
        if (!parse_addr(arg[i], &a)) { bad_addr(arg[i]); continue; }
        char out[280];
        fmt_value(&a, out, sizeof(out));
        printf("  %-10s = %s\n", a.name, out);
    }
}

static void cmd_write(char **arg, int n, bool force)
{
    addr_t a;
    if (n != 2) { printf("usage: %s ADDR VALUE\n", force ? "force" : "write"); return; }
    if (!parse_addr(arg[0], &a)) { bad_addr(arg[0]); return; }
    if (!wr_text(&a, arg[1])) return;
    refresh_changes();
    if (!force) { printf("  %s written\n", a.name); return; }
    int i;
    for (i = 0; i < s_nforce; i++) if (!strcmp(s_force[i].a.name, a.name)) break;
    if (i == s_nforce) {
        if (s_nforce >= MAX_FORCES) { printf("too many forces (max %d)\n", MAX_FORCES); return; }
        s_nforce++;
    }
    s_force[i].a = a;
    snprintf(s_force[i].text, sizeof(s_force[i].text), "%s", arg[1]);
    printf("  %s forced to %s\n", a.name, arg[1]);
}

static void cmd_unforce(char **arg, int n)
{
    if (!n) { printf("usage: unforce ADDR | all\n"); return; }
    if (n == 1 && !strcmp(arg[0], "all")) { s_nforce = 0; printf("  all forces released\n"); return; }
    for (int k = 0; k < n; k++) {
        addr_t a;
        if (!parse_addr(arg[k], &a)) { bad_addr(arg[k]); continue; }
        bool found = false;
        for (int i = 0; i < s_nforce; i++)
            if (!strcmp(s_force[i].a.name, a.name)) { s_force[i] = s_force[--s_nforce]; found = true; break; }
        printf(found ? "  %s released\n" : "  %s isn't forced\n", a.name);
    }
}

static void cmd_forces(void)
{
    if (!s_nforce) { printf("  nothing forced\n"); return; }
    for (int i = 0; i < s_nforce; i++) printf("  %-10s = %s\n", s_force[i].a.name, s_force[i].text);
}

static void cmd_watch(char **arg, int n)
{
    if (!n) {
        if (!s_nwatch) printf("  nothing watched\n");
        for (int i = 0; i < s_nwatch; i++) printf("  %s\n", s_watch[i].a.name);
        return;
    }
    if (n == 1 && !strcmp(arg[0], "off")) { s_nwatch = 0; printf("  watch off\n"); return; }
    for (int k = 0; k < n; k++) {
        addr_t a;
        if (!parse_addr(arg[k], &a)) { bad_addr(arg[k]); continue; }
        if (s_nwatch >= MAX_WATCHES) { printf("too many watches (max %d)\n", MAX_WATCHES); return; }
        s_watch[s_nwatch].a = a;
        s_watch[s_nwatch].first = true;
        s_nwatch++;
    }
}

static bool block_ok(const char *b)
{
    if (!b || !strcasecmp(b, "MAIN") || !strcasecmp(b, "OB1")) return true;
    printf("this program only has the MAIN block (its subroutines and interrupts are empty)\n");
    return false;
}

static void cmd_trace(char **arg, int n)
{
    if (n < 1 || n > 2) { printf("usage: trace NET [BLOCK] | trace all | trace off\n"); return; }
    if (!strcmp(arg[0], "off")) { s_trace_net = -1; printf("  trace off\n"); return; }
    if (!strcmp(arg[0], "all")) s_trace_net = 0;
    else {
        int v = atoi(arg[0]);
        if (v <= 0) { printf("'%s' is not a network number\n", arg[0]); return; }
        if (!block_ok(n == 2 ? arg[1] : NULL)) return;
        if (s_scanned_once && (v >= MAX_NETS || !(s_net_seen[v / 8] & (1u << (v % 8))))) {
            printf("network %d has no instructions\n", v);
            return;
        }
        s_trace_net = v;
    }
    printf("  tracing the next scan%s\n", s_paused && !s_stopped ? " (paused: use step)" : "");
}

static void cmd_stack(void)
{
    char st[40];
    stack_text(st, sizeof(st));
    if (!s_stopped) {
        printf("  between scans (stop at a breakpoint to see the program mid-scan)\n");
        return;
    }
    printf("  at ");
    where(s_cur_line, s_cur_net, s_cur_text);
    printf("\n  logic stack before it, top first: %s\n", st);
}

static bool parse_op(const char *s, op_t *op)
{
    if (!strcmp(s, "==") || !strcmp(s, "="))  { *op = OP_EQ; return true; }
    if (!strcmp(s, "!=") || !strcmp(s, "<>")) { *op = OP_NE; return true; }
    if (!strcmp(s, "<"))  { *op = OP_LT; return true; }
    if (!strcmp(s, ">"))  { *op = OP_GT; return true; }
    if (!strcmp(s, "<=")) { *op = OP_LE; return true; }
    if (!strcmp(s, ">=")) { *op = OP_GE; return true; }
    return false;
}

static const char *op_text(op_t op)
{
    static const char *t[] = { "==", "!=", "<", ">", "<=", ">=" };
    return t[op];
}

static void describe_bp(int i, const bp_t *b)
{
    printf("%d: ", i + 1);
    if (b->kind == BP_LINE) printf("line %d", b->line);
    else if (b->kind == BP_NET) printf("start of MAIN network %d", b->net);
    else {
        printf("when %s changes", b->a.name);
        if (b->has_to) printf(" to %g", b->to);
    }
    if (b->has_cond) printf(" if %s %s %g", b->ca.name, op_text(b->op), b->cv);
}

static void cmd_break(char **arg, int n)
{
    bp_t b;
    memset(&b, 0, sizeof(b));
    int k = 0;
    if (n < 1) { printf("usage: break LINE | break net N | break change ADDR [to V]  [if ADDR OP VALUE]\n"); return; }

    if (!strcmp(arg[0], "net")) {
        if (n < 2 || atoi(arg[1]) <= 0) { printf("usage: break net N [BLOCK]\n"); return; }
        b.kind = BP_NET;
        b.net = atoi(arg[1]);
        k = 2;
        if (k < n && strcmp(arg[k], "if")) { if (!block_ok(arg[k])) return; k++; }
        if (s_scanned_once && (b.net >= MAX_NETS || !(s_net_seen[b.net / 8] & (1u << (b.net % 8))))) {
            printf("network %d has no instructions\n", b.net);
            return;
        }
    } else if (!strcmp(arg[0], "change")) {
        if (n < 2 || !parse_addr(arg[1], &b.a)) { if (n >= 2) bad_addr(arg[1]); else printf("usage: break change ADDR\n"); return; }
        if (b.a.f == F_STR) { printf("changepoints don't work on strings\n"); return; }
        b.kind = BP_CHANGE;
        k = 2;
        if (k < n && !strcmp(arg[k], "to")) {
            if (k + 1 >= n || !parse_num(arg[k + 1], &b.a, &b.to)) { printf("usage: break change ADDR to VALUE\n"); return; }
            b.has_to = true;
            k += 2;
        }
        b.last = rd_raw(&b.a);
    } else {
        char *end;
        long line = strtol(arg[0], &end, 10);
        if (*end || line <= 0) { printf("'%s' is not a line number (break 21, break net 3, break change VB202)\n", arg[0]); return; }
        if (s_scanned_once && line > s_max_line) {
            printf("line %ld is past the end of the program in %s (last instruction: line %d)\n",
                   line, plc_program_file, s_max_line);
            return;
        }
        if (s_scanned_once && line < MAX_LINES && !(s_line_seen[line / 8] & (1u << (line % 8)))) {
            printf("line %ld of %s has no instruction (a Network or comment line?); break net N avoids this\n",
                   line, plc_program_file);
            return;
        }
        b.kind = BP_LINE;
        b.line = (int)line;
        k = 1;
    }

    if (k < n) {
        if (strcmp(arg[k], "if") || k + 4 != n) { printf("condition: ... if ADDR OP VALUE  (OP is == != < > <= >=)\n"); return; }
        if (!parse_addr(arg[k + 1], &b.ca)) { bad_addr(arg[k + 1]); return; }
        if (b.ca.f == F_STR) { printf("conditions don't work on strings\n"); return; }
        if (!parse_op(arg[k + 2], &b.op)) { printf("'%s' is not a comparison (== != < > <= >=)\n", arg[k + 2]); return; }
        if (!parse_num(arg[k + 3], &b.ca, &b.cv)) { printf("'%s' is not a value\n", arg[k + 3]); return; }
        b.has_cond = true;
    }

    for (int i = 0; i < MAX_BREAKS; i++) {
        if (s_bp[i].used) continue;
        b.used = true;
        s_bp[i] = b;
        printf("  %s ", b.kind == BP_CHANGE ? "changepoint" : "breakpoint");
        describe_bp(i, &b);
        if (b.kind == BP_CHANGE) {
            char now[40];
            fmt_raw(&b.a, b.last, now, sizeof(now));
            printf(" (now %s)", now);
        }
        printf("\n");
        return;
    }
    printf("too many breakpoints (max %d): delete some first\n", MAX_BREAKS);
}

static void cmd_breaks(void)
{
    bool any = false;
    for (int i = 0; i < MAX_BREAKS; i++) {
        if (!s_bp[i].used) continue;
        any = true;
        printf("  ");
        describe_bp(i, &s_bp[i]);
        printf("   hit %lu time%s\n", (unsigned long)s_bp[i].hits, s_bp[i].hits == 1 ? "" : "s");
    }
    if (!any) printf("  no breakpoints\n");
}

static void cmd_delete(char **arg, int n)
{
    if (n != 1) { printf("usage: delete N | all\n"); return; }
    if (!strcmp(arg[0], "all")) { memset(s_bp, 0, sizeof(s_bp)); printf("  all breakpoints deleted\n"); return; }
    int i = atoi(arg[0]) - 1;
    if (i < 0 || i >= MAX_BREAKS || !s_bp[i].used) { printf("there is no breakpoint %s (see breaks)\n", arg[0]); return; }
    s_bp[i].used = false;
    printf("  breakpoint %d deleted\n", i + 1);
}

static void cmd_errors(char **arg, int n)
{
    if (n == 1 && !strcmp(arg[0], "clear")) { s_nerr = 0; s_err_printed = 0; printf("  error log cleared\n"); return; }
    if (!s_nerr) { printf("  no errors\n"); return; }
    for (int i = 0; i < s_nerr; i++) {
        err_t *e = &s_err[i];
        printf("  %d. %s (%s %d)", i + 1, e->msg, area_name(e->area), e->addr);
        if (e->line) { printf(" at "); where(e->line, e->net, e->text); }
        printf(", %lu time%s\n", (unsigned long)e->count, e->count == 1 ? "" : "s");
    }
    if (s_nerr == MAX_ERRORS) printf("  (log full: only the first %d different errors are kept)\n", MAX_ERRORS);
}

static void cmd_status(void)
{
    int nb = 0;
    for (int i = 0; i < MAX_BREAKS; i++) nb += s_bp[i].used;
    printf("  RUN, %s, %lu scans, state VB202 = %d\n",
           s_stopped ? "STOPPED AT A BREAKPOINT" : s_paused ? "PAUSED" : "scanning",
           (unsigned long)s_scans, plc_rd_byte('V', 202));
    printf("  %lu errors, %d forced, %d watched, %d breakpoints\n",
           (unsigned long)plc_error_count(), s_nforce, s_nwatch, nb);
}

/* split a line into words; 'quoted text' stays one word */
static int split(char *s, char **arg, int max)
{
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ' || *s == '\t' || *s == ',' || *s == '\r' || *s == '\n') s++;
        if (!*s) break;
        arg[n++] = s;
        if (*s == '\'') {
            char *e = strchr(s + 1, '\'');
            s = e ? e + 1 : s + strlen(s);
        } else {
            while (*s && *s != ' ' && *s != '\t' && *s != ',' && *s != '\r' && *s != '\n') s++;
        }
        if (*s) *s++ = 0;
    }
    return n;
}

bool plc_debug_command(const char *line)
{
    char buf[300];
    char *arg[16];
    snprintf(buf, sizeof(buf), "%s", line);
    int n = split(buf, arg, 16);
    if (!n) return false;
    for (char *c = arg[0]; *c; c++) *c = (char)tolower((unsigned char)*c);
    for (int i = 1; i < n; i++)              /* keywords in lower case */
        if (!strcasecmp(arg[i], "all") || !strcasecmp(arg[i], "off") || !strcasecmp(arg[i], "reset") ||
            !strcasecmp(arg[i], "clear") || !strcasecmp(arg[i], "net") || !strcasecmp(arg[i], "change") ||
            !strcasecmp(arg[i], "to") || !strcasecmp(arg[i], "if"))
            for (char *c = arg[i]; *c; c++) *c = (char)tolower((unsigned char)*c);
    const char *cmd = arg[0];
    char **a = arg + 1;
    n--;

    if (!strcmp(cmd, "help") || !strcmp(cmd, "?"))               cmd_help();
    else if (!strcmp(cmd, "read") || !strcmp(cmd, "r"))          cmd_read(a, n);
    else if (!strcmp(cmd, "write") || !strcmp(cmd, "w"))         cmd_write(a, n, false);
    else if (!strcmp(cmd, "force"))                              cmd_write(a, n, true);
    else if (!strcmp(cmd, "unforce"))                            cmd_unforce(a, n);
    else if (!strcmp(cmd, "forces"))                             cmd_forces();
    else if (!strcmp(cmd, "watch"))                              cmd_watch(a, n);
    else if (!strcmp(cmd, "trace"))                              cmd_trace(a, n);
    else if (!strcmp(cmd, "stack"))                              cmd_stack();
    else if (!strcmp(cmd, "break") || !strcmp(cmd, "b"))         cmd_break(a, n);
    else if (!strcmp(cmd, "breaks"))                             cmd_breaks();
    else if (!strcmp(cmd, "delete"))                             cmd_delete(a, n);
    else if (!strcmp(cmd, "errors"))                             cmd_errors(a, n);
    else if (!strcmp(cmd, "status"))                             cmd_status();
    else if (!strcmp(cmd, "c") || !strcmp(cmd, "continue")) {
        if (!s_stopped) { printf("not stopped at a breakpoint\n"); return false; }
        s_si = 0;
        s_stopped = false;
        return true;
    }
    else if (!strcmp(cmd, "si") || ((!strcmp(cmd, "step") || !strcmp(cmd, "s")) && s_stopped)) {
        if (!s_stopped) { printf("si works when stopped at a breakpoint\n"); return false; }
        s_si = n ? atoi(a[0]) : 1;
        if (s_si < 1) s_si = 1;
        s_stopped = false;
        return true;
    }
    else if (!strcmp(cmd, "step") || !strcmp(cmd, "s")) {
        if (!s_paused) { printf("pause first (or use step at a breakpoint)\n"); return false; }
        s_scan_steps = n ? atoi(a[0]) : 1;
        if (s_scan_steps < 1) s_scan_steps = 1;
        printf("  running %d scan%s\n", s_scan_steps, s_scan_steps == 1 ? "" : "s");
    }
    else if (!strcmp(cmd, "pause"))   { s_paused = true;  printf("  paused (outputs hold)\n"); }
    else if (!strcmp(cmd, "resume"))  { s_paused = false; s_scan_steps = 0; printf("  scanning\n"); }
    else if (!strcmp(cmd, "run"))       printf("  already in RUN (this program has no STOP instruction)\n");
    else if (!strcmp(cmd, "reset"))   { s_req_reset = true;   printf("  reset at the start of the next scan\n"); }
    else if (!strcmp(cmd, "restart")) { s_req_restart = true; printf("  restart at the start of the next scan\n"); }
    else if (!strcmp(cmd, "scantime")) {
        if (n && !strcmp(a[0], "reset")) { s_scan_min = UINT32_MAX; s_scan_max = 0; printf("  scan times reset\n"); }
        else printf("  scan time: last %lu us, shortest %lu us, longest %lu us\n", (unsigned long)s_scan_last,
                    (unsigned long)(s_scan_min == UINT32_MAX ? 0 : s_scan_min), (unsigned long)s_scan_max);
    }
    else printf("unknown command '%s' (type help)\n", cmd);
    return false;
}
#endif /* PLC_DEBUG */
