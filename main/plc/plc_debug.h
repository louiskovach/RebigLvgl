/*
 * plc_debug - serial console debugger for the PLC program.
 * Turn it on/off in menuconfig: PLC runtime -> Debugger. Type "help" in the serial monitor.
 *
 * Values:      read, write, force, unforce, forces, watch
 * Tracing:     trace NET [BLOCK] | trace all | trace off, stack
 * Scanning:    pause, resume, step [N], run, reset, restart
 * Breakpoints: break LINE, break net N [BLOCK], break change ADDR [to V], ... if ADDR OP VALUE,
 *              breaks, delete N | all;  at a breakpoint (prompt "(break)"): c, si [N], step [N]
 * Information: scantime [reset], errors [clear], status, help
 * Short forms: r = read, w = write, b = break, c = continue, s = step, ? = help
 *
 * Turn it off for production: force and write must not be available on a running machine.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Platform hooks: wait() is called over and over while the program is stopped at a breakpoint
 * (in the middle of a scan); it must let the console task run commands (release the PLC lock,
 * sleep a little, take it back). now_ms() is a millisecond clock. */
void plc_debug_init(void (*wait)(void), uint32_t (*now_ms)(void));

/* Run one console line. Returns true when it let a stopped program carry on (c, si, step). */
bool plc_debug_command(const char *line);
bool plc_debug_stopped(void);               /* stopped at a breakpoint now */
const char *plc_debug_prompt(void);         /* "plc> " or "(break) " */

/* Around each scan, in this order (all with the PLC lock held): */
void plc_debug_begin_scan(void);            /* reset / restart requests, before the inputs */
bool plc_debug_should_scan(void);           /* false while paused (unless stepping) */
void plc_debug_before_scan(void);           /* forces, changes between scans, trace start */
void plc_debug_after_scan(uint32_t now_ms, uint32_t scan_us);  /* forces, watch, end-of-scan changes */
uint32_t plc_debug_take_stopped_ms(void);   /* ms spent stopped since the last call (PLC time is frozen) */
