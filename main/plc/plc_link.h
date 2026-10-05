/*
 * plc_link - runs the PLC program in its own FreeRTOS task and connects it to the screens.
 *
 *  - Inputs are SIMULATED for now (no I/O hardware yet): screen buttons pulse the program's
 *    inputs, and the pump running signal I1.3 follows the pump output after 1 s.
 *  - The parameters are written into the program's timer presets every scan (V memory map
 *    below), so changing a parameter changes the real timing.
 *  - The screens never touch PLC memory directly: they send commands and read a snapshot.
 *
 * I/O (from the program):
 *   I0.0 Start   I0.2 Immediate stop   I0.4 Bump/Revive   I1.0 Fault reset (falling edge)
 *   I1.2 Stop    I1.3 Pump running (proof)
 *   Q0.0 Pump    Q0.1 Regen valve      Q0.2 Effluent valve   Q0.3 Bump valve
 *   Q0.4 Bump requested   Q0.5 Bumps complete   Q0.6 Return to running
 *   Q1.0 Running (Filter Mode)   Q1.1 Pump fault
 * State VB202: 0 idle, 1-5 start-up, 6 running, 10-14 stop steps, 15-19 bump (revive).
 *
 * HMI -> PLC memory (all presets in 0.1 s):
 *   VW300 T37 state 1 (fixed 5 s)        VW302 T38 state 2  <- P13 Pump Run Confirm Time
 *   VW304 T39 state 3  <- P14 Precoat (min)  VW306 T40 state 4 (fixed 5 s)
 *   VW308 T41 state 5  <- P19 V11 Close Delay  VW310 T42 stop 10 (fixed 5 s)
 *   VW312 T43 stop 11  <- P12 Eff. Close Delay VW314 T44 stop 12 <- P18 Pump Stop Delay
 *   VW316 T45 stop 13 (fixed 5 s)        VW318 T47 pump proof (fixed 5 s)
 *   VW320 T60 bump down <- P30 Cyl On    VW322 T61 bump up <- P29 Cyl Off
 *   VW0   C0 number of bumps <- P20 Revive Strokes
 *   V250.0 HMI pump on (Drain / Rinse, idle only)
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t state;           /* VB202 */
    uint8_t ib[2], qb[2];    /* IB0, IB1, QB0, QB1 */
    bool    pump_fault;      /* V210.0 */
    bool    manual_pump;     /* V250.0 */
    int16_t bumps_done;      /* C0 */
    int16_t bumps_total;     /* VW0 */
    uint32_t step_ms;        /* how far the current step's PLC timer has counted (T37..T45, T60, T61) */
    bool    halted;          /* debugger: paused or stopped at a breakpoint (program not scanning) */
} plc_snap_t;

void     plc_link_start(void);                  /* call once at boot, after settings_load() */
void     plc_link_get(plc_snap_t *out);         /* latest snapshot (thread-safe) */

/* commands (thread-safe, non-blocking) */
void     plc_link_cmd_start(void);              /* pulse I0.0 */
void     plc_link_cmd_stop(void);               /* pulse I1.2 */
void     plc_link_cmd_abort(void);              /* pulse I0.2: immediate stop, program back to idle */
void     plc_link_cmd_revive(void);             /* pulse I0.4 */
void     plc_link_cmd_fault_reset(void);        /* I1.0 high -> low */
void     plc_link_manual_pump(bool on);         /* V250.0 */

void     plc_link_load_params(void);            /* parameters -> presets (call after a change) */
void     plc_link_set_fast(bool fast);          /* Diagnostic Filter: start-up steps 1 s each */
uint32_t plc_link_state_time_ms(uint8_t state); /* step time of a state, 0 = not timed */

static inline bool plc_snap_bit(const plc_snap_t *s, char area, int byte, int bit)
{
    const uint8_t *b = area == 'I' ? s->ib : s->qb;
    return byte >= 0 && byte < 2 && ((b[byte] >> bit) & 1);
}
