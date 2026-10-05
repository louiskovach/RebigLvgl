/* plc.c - S7-200 runtime, see plc.h */
#include <string.h>
#include "plc.h"

static uint8_t s_i[PLC_IO_BYTES], s_q[PLC_IO_BYTES], s_m[PLC_M_BYTES], s_s[PLC_S_BYTES];
static PLC_PSRAM uint8_t s_v[PLC_V_BYTES];
static PLC_PSRAM uint8_t s_sm[PLC_SM_BYTES];
static uint8_t s_ai[PLC_AI_BYTES], s_aq[PLC_AQ_BYTES];

typedef struct { uint32_t ms; int16_t acc; uint8_t bit; uint8_t timing; } plc_timer_t;
typedef struct { int16_t count; uint8_t bit; uint8_t prev_cu; } plc_counter_t;
static PLC_PSRAM plc_timer_t   s_t[PLC_TIMERS];
static PLC_PSRAM plc_counter_t s_c[PLC_COUNTERS];
static PLC_PSRAM uint8_t       s_edge[PLC_EDGES];

static uint32_t s_stack;          /* logic stack: bit 0 = top */
static uint32_t s_last_ms, s_dt;  /* time since the previous scan */
static bool     s_first = true, s_power_up = true;
static uint32_t s_errors;

#if PLC_DEBUG
static plc_hooks_t s_hooks;
void     plc_set_hooks(const plc_hooks_t *h) { if (h) s_hooks = *h; else memset(&s_hooks, 0, sizeof(s_hooks)); }
void     plc_at(int network, int line, const char *stl) { if (s_hooks.at) s_hooks.at(network, line, stl); }
uint32_t plc_stack_bits(void) { return s_stack; }
#endif

static void plc_error(const char *msg, char area, int addr)
{
    s_errors++;
#if PLC_DEBUG
    if (s_hooks.error) s_hooks.error(msg, area, addr);
#else
    (void)msg; (void)area; (void)addr;
#endif
}
uint32_t plc_error_count(void) { return s_errors; }

/* ------------------------------------------------------------------ memory */
static uint8_t *area_base(char a, int *size)
{
    switch (a) {
    case 'I': *size = PLC_IO_BYTES; return s_i;
    case 'Q': *size = PLC_IO_BYTES; return s_q;
    case 'M': *size = PLC_M_BYTES;  return s_m;
    case 'V': *size = PLC_V_BYTES;  return s_v;
    case 'S': *size = PLC_S_BYTES;  return s_s;
    case 'X': *size = PLC_SM_BYTES; return s_sm;
    case 'A': *size = PLC_AI_BYTES; return s_ai;
    case 'O': *size = PLC_AQ_BYTES; return s_aq;
    default:  *size = 0;            return NULL;
    }
}

bool plc_valid(char a, int byte, int size)
{
    int n;
    uint8_t *b = area_base(a, &n);
    return b && byte >= 0 && byte + size <= n;
}

static uint8_t *ptr(char a, int byte)
{
    int n;
    uint8_t *b = area_base(a, &n);
    if (!b || byte < 0 || byte >= n) { plc_error("address out of range", a, byte); return NULL; }
    return b + byte;
}

int plc_rd_bit(char a, int byte, int bit)
{
    uint8_t *p = ptr(a, byte);
    return p ? (*p >> bit) & 1 : 0;
}

void plc_wr_bit(char a, int byte, int bit, int v)
{
    uint8_t *p = ptr(a, byte);
    if (!p) return;
    if (v) *p |= (uint8_t)(1u << bit);
    else   *p &= (uint8_t)~(1u << bit);
}

uint8_t plc_rd_byte(char a, int byte)            { uint8_t *p = ptr(a, byte); return p ? *p : 0; }
void    plc_wr_byte(char a, int byte, uint8_t v) { uint8_t *p = ptr(a, byte); if (p) *p = v; }

int16_t plc_rd_word(char a, int byte)
{
    return (int16_t)((plc_rd_byte(a, byte) << 8) | plc_rd_byte(a, byte + 1));
}
void plc_wr_word(char a, int byte, int16_t v)
{
    plc_wr_byte(a, byte, (uint8_t)((uint16_t)v >> 8));
    plc_wr_byte(a, byte + 1, (uint8_t)v);
}
int32_t plc_rd_dword(char a, int byte)
{
    return (int32_t)(((uint32_t)(uint16_t)plc_rd_word(a, byte) << 16) | (uint16_t)plc_rd_word(a, byte + 2));
}
void plc_wr_dword(char a, int byte, int32_t v)
{
    plc_wr_word(a, byte, (int16_t)((uint32_t)v >> 16));
    plc_wr_word(a, byte + 2, (int16_t)v);
}

static bool t_ok(int n) { if (n >= 0 && n < PLC_TIMERS) return true;   plc_error("timer number out of range", 'T', n); return false; }
static bool c_ok(int n) { if (n >= 0 && n < PLC_COUNTERS) return true; plc_error("counter number out of range", 'C', n); return false; }

int     plc_t_bit(int n)   { return t_ok(n) ? s_t[n].bit : 0; }
int16_t plc_t_acc(int n)   { return t_ok(n) ? s_t[n].acc : 0; }
int     plc_c_bit(int n)   { return c_ok(n) ? s_c[n].bit : 0; }
int16_t plc_c_count(int n) { return c_ok(n) ? s_c[n].count : 0; }

/* ------------------------------------------------------------------ scan */
void plc_reset(void)
{
    memset(s_i, 0, sizeof(s_i)); memset(s_q, 0, sizeof(s_q)); memset(s_m, 0, sizeof(s_m));
    memset(s_v, 0, sizeof(s_v)); memset(s_s, 0, sizeof(s_s)); memset(s_sm, 0, sizeof(s_sm));
    memset(s_ai, 0, sizeof(s_ai)); memset(s_aq, 0, sizeof(s_aq));
    memset(s_t, 0, sizeof(s_t)); memset(s_c, 0, sizeof(s_c)); memset(s_edge, 0, sizeof(s_edge));
    s_stack = 0;
    s_errors = 0;
    s_first = true;
    s_power_up = true;
}

void plc_restart(void) { s_first = true; }

void plc_scan(void (*program)(void), uint32_t now_ms)
{
    s_dt = s_first ? 0 : now_ms - s_last_ms;
    s_last_ms = now_ms;
    plc_wr_bit('X', 0, 0, 1);                  /* SM0.0 always on */
    plc_wr_bit('X', 0, 1, s_first);            /* SM0.1 first scan */
    plc_wr_bit('X', 0, 3, s_power_up);         /* SM0.3 first scan after power-up */
    plc_wr_bit('X', 0, 5, (now_ms / 500) & 1); /* SM0.5 1 s clock */
    s_stack = 0;
    program();
    s_first = false;
    s_power_up = false;
}

/* ------------------------------------------------------------------ logic stack */
#define TOP (s_stack & 1u)
static void set_top(int v) { s_stack = (s_stack & ~1u) | (v ? 1u : 0u); }
static void push(int v)    { s_stack = (s_stack << 1) | (v ? 1u : 0u); }

void plc_ld(int v)  { push(v); }
void plc_ldn(int v) { push(!v); }
void plc_a(int v)   { set_top(TOP && v); }
void plc_an(int v)  { set_top(TOP && !v); }
void plc_o(int v)   { set_top(TOP || v); }
void plc_on(int v)  { set_top(TOP || !v); }
void plc_not(void)  { set_top(!TOP); }
void plc_ald(void)  { int a = TOP, b = (s_stack >> 1) & 1; s_stack >>= 1; set_top(a && b); }
void plc_old(void)  { int a = TOP, b = (s_stack >> 1) & 1; s_stack >>= 1; set_top(a || b); }
void plc_lps(void)  { push(TOP); }
void plc_lrd(void)  { set_top((s_stack >> 1) & 1); }
void plc_lpp(void)  { s_stack >>= 1; }

static bool edge_ok(int k) { if (k >= 0 && k < PLC_EDGES) return true; plc_error("too many EU/ED", 'E', k); return false; }
void plc_eu(int k) { if (!edge_ok(k)) return; int v = TOP; set_top(v && !s_edge[k]); s_edge[k] = (uint8_t)v; }
void plc_ed(int k) { if (!edge_ok(k)) return; int v = TOP; set_top(!v && s_edge[k]);  s_edge[k] = (uint8_t)v; }

void plc_out(char a, int byte, int bit) { plc_wr_bit(a, byte, bit, TOP); }

void plc_movb(int value, int vbyte) { if (TOP) plc_wr_byte('V', vbyte, (uint8_t)value); }
void plc_movw(int value, int vword) { if (TOP) plc_wr_word('V', vword, (int16_t)value); }

/* ------------------------------------------------------------------ timers (100 ms base) */
void plc_ton(int n, int preset)
{
    if (!t_ok(n)) return;
    plc_timer_t *t = &s_t[n];
    if (TOP) {
        if (t->ms < 3276700u) t->ms += s_dt;
        int32_t acc = (int32_t)(t->ms / 100);
        t->acc = (int16_t)(acc > 32767 ? 32767 : acc);
        t->bit = t->acc >= preset;
    } else {
        t->ms = 0; t->acc = 0; t->bit = 0;
    }
}

void plc_tof(int n, int preset)
{
    if (!t_ok(n)) return;
    plc_timer_t *t = &s_t[n];
    if (TOP) {                       /* input on: output on, nothing timing */
        t->bit = 1; t->timing = 1; t->ms = 0; t->acc = 0;
    } else if (t->timing) {          /* input off: time the off-delay */
        t->ms += s_dt;
        int32_t acc = (int32_t)(t->ms / 100);
        t->acc = (int16_t)(acc > 32767 ? 32767 : acc);
        if (t->acc >= preset) { t->bit = 0; t->timing = 0; t->acc = (int16_t)preset; }
    }
}

/* ------------------------------------------------------------------ counter */
/* STL: LD <count input>  LD <reset input>  CTU Cn, PV  -> reset on top, count input below it */
void plc_ctu(int n, int preset)
{
    if (!c_ok(n)) return;
    plc_counter_t *c = &s_c[n];
    int r = TOP, cu = (s_stack >> 1) & 1;
    if (r) {
        c->count = 0;
    } else if (cu && !c->prev_cu && c->count < 32767) {
        c->count++;
    }
    c->prev_cu = (uint8_t)cu;
    c->bit = !r && c->count >= preset;
}
