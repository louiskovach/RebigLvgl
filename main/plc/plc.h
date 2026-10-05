/*
 * plc.h - S7-200 runtime
 *
 * Runs a Siemens S7-200 statement-list (STL) program that tools/awl_to_c.py has turned into
 * C calls (program.c), one call per instruction:
 *
 *     LD     I0.0          ->  LD(B('I', 0, 0));
 *     LDB=   VB202, 14     ->  LDB(VB(202), ==, 14);
 *     MOVB   0, VB202      ->  MOVB(0, 202);
 *     TON    T37, VW300    ->  TON(37, VW(300));
 *
 * Memory areas (area letters used in C):
 *   'I' inputs  'Q' outputs  'M' flags  'V' variable memory  'S' sequence (S0.0)
 *   'X' special memory SM    'A' analog inputs AIW   'O' analog outputs AQW
 * Words and double words are big-endian like the S7-200: VW0 = VB0 (high) + VB1 (low).
 * Timers T0-T255 count in 100 ms units (TON/TOF); counters C0-C255 (CTU).
 * SM0.0 always on, SM0.1 first scan, SM0.3 first scan after power-up, SM0.5 1 s clock,
 * SMW22/24/26 last/shortest/longest scan time in ms (written by plc_link).
 *
 * Not thread-safe by itself: plc_link.c holds a lock around every scan and access.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Debugger switch: menuconfig -> PLC runtime -> Debugger (or -DPLC_DEBUG=1 on the PC) */
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#ifdef CONFIG_PLC_DEBUG
#define PLC_DEBUG 1
#endif
#endif
#ifndef PLC_DEBUG
#define PLC_DEBUG 0
#endif

/* Big PLC tables go in PSRAM on the ESP32 (internal RAM is needed by the display and Wi-Fi) */
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define PLC_PSRAM EXT_RAM_BSS_ATTR
#else
#define PLC_PSRAM
#endif

#define PLC_IO_BYTES  16
#define PLC_M_BYTES   32
#define PLC_V_BYTES   10240     /* VB0 - VB10239 */
#define PLC_S_BYTES   32
#define PLC_SM_BYTES  550
#define PLC_AI_BYTES  64        /* AIW0 - AIW62 */
#define PLC_AQ_BYTES  64
#define PLC_TIMERS    256
#define PLC_COUNTERS  256
#define PLC_EDGES     256       /* EU / ED edge memories */

/* ---- runtime ---- */
void     plc_reset(void);                     /* power cycle: everything cleared, next scan first scan */
void     plc_restart(void);                   /* STOP -> RUN: memory kept, next scan first scan */
void     plc_scan(void (*program)(void), uint32_t now_ms);

/* ---- memory access ---- */
bool     plc_valid(char area, int byte, int size);   /* address exists (size in bytes) */
int      plc_rd_bit(char area, int byte, int bit);
void     plc_wr_bit(char area, int byte, int bit, int v);
uint8_t  plc_rd_byte(char area, int byte);
void     plc_wr_byte(char area, int byte, uint8_t v);
int16_t  plc_rd_word(char area, int byte);
void     plc_wr_word(char area, int byte, int16_t v);
int32_t  plc_rd_dword(char area, int byte);
void     plc_wr_dword(char area, int byte, int32_t v);
int      plc_t_bit(int n);
int16_t  plc_t_acc(int n);                    /* in 100 ms units */
int      plc_c_bit(int n);
int16_t  plc_c_count(int n);
uint32_t plc_error_count(void);               /* runtime errors since reset */

/* ---- instruction implementations ---- */
void plc_ld(int v);  void plc_ldn(int v);
void plc_a(int v);   void plc_an(int v);
void plc_o(int v);   void plc_on(int v);
void plc_not(void);  void plc_ald(void);  void plc_old(void);
void plc_lps(void);  void plc_lrd(void);  void plc_lpp(void);
void plc_eu(int edge);
void plc_ed(int edge);
void plc_out(char area, int byte, int bit);
void plc_movb(int value, int vbyte);
void plc_movw(int value, int vword);
void plc_ton(int t, int preset);
void plc_tof(int t, int preset);
void plc_ctu(int c, int preset);

/* ---- macros used by program.c ---- */
#define B(a, by, bi)  plc_rd_bit((a), (by), (bi))
#define TB(n)         plc_t_bit(n)
#define CB(n)         plc_c_bit(n)
#define RB(a, n)      ((int)plc_rd_byte((a), (n)))
#define RW(a, n)      ((int)plc_rd_word((a), (n)))
#define RD(a, n)      ((long)plc_rd_dword((a), (n)))
#define VB(n)         RB('V', (n))
#define VW(n)         RW('V', (n))

#define LD(v)   plc_ld(v)
#define LDN(v)  plc_ldn(v)
#define A(v)    plc_a(v)
#define AN(v)   plc_an(v)
#define O(v)    plc_o(v)
#define ON(v)   plc_on(v)
#define NOT()   plc_not()
#define ALD()   plc_ald()
#define OLD()   plc_old()
#define LPS()   plc_lps()
#define LRD()   plc_lrd()
#define LPP()   plc_lpp()
#define EU(k)   plc_eu(k)               /* k = this EU's own edge memory (numbered by the converter) */
#define ED(k)   plc_ed(k)
#define OUT(a, by, bi)   plc_out((a), (by), (bi))
#define LDB(x, op, y)    plc_ld((x) op (y))      /* byte compares (unsigned) */
#define AB(x, op, y)     plc_a((x) op (y))
#define OB(x, op, y)     plc_o((x) op (y))
#define LDW(x, op, y)    plc_ld((x) op (y))      /* word compares (signed) */
#define AW(x, op, y)     plc_a((x) op (y))
#define OW(x, op, y)     plc_o((x) op (y))
#define MOVB(v, vb)      plc_movb((v), (vb))
#define MOVW(v, vw)      plc_movw((v), (vw))
#define TON(t, pt)       plc_ton((t), (pt))
#define TOF(t, pt)       plc_tof((t), (pt))
#define CTU(c, pv)       plc_ctu((c), (pv))

/* Debug information in program.c: one PLC_AT before each instruction (network, line of the
 * .awl file, STL text). Compiles to nothing when the debugger is off. */
#if PLC_DEBUG
typedef struct {
    void (*at)(int network, int line, const char *stl);           /* before each instruction */
    void (*error)(const char *msg, char area, int addr);          /* runtime error */
} plc_hooks_t;
void     plc_set_hooks(const plc_hooks_t *hooks);
void     plc_at(int network, int line, const char *stl);
uint32_t plc_stack_bits(void);                                    /* logic stack, bit 0 = top */
#define PLC_AT(n, l, t)  plc_at((n), (l), (t))
#else
#define PLC_AT(n, l, t)  ((void)0)
#endif

extern const char *const plc_program_file;   /* program.c: the .awl it came from */
void plc_main(void);                          /* program.c */
