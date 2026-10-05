/*
 * mb_poll - Modbus polling task <-> PLC memory
 *
 * The "mb_poll" task does all Modbus traffic (it's the only task that waits on the bus).
 * The PLC task never waits for Modbus: at every scan it swaps data with mb_poll through a
 * shadow copy, using two quick calls made by plc_link.c with the PLC lock held:
 *
 *   mb_poll_apply_inputs()     before the scan: latest values read from the slaves -> PLC memory,
 *                              plus a "communication OK" bit per table line
 *   mb_poll_capture_outputs()  after the scan:  PLC memory -> values to write to the slaves
 *
 * What is exchanged is set in mb_map.c (one line per Modbus request).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MB_MAP_MAX_LINES   32       /* lines in mb_map.c */
#define MB_MAP_MAX_COUNT   125      /* registers or bits per line */

typedef struct {
    uint8_t  slave;          /* 1-247 */
    uint8_t  fc;             /* read: 1 coils, 2 discrete inputs, 3 holding regs, 4 input regs
                                write: 5 one coil, 6 one register, 15 coils, 16 registers */
    uint16_t mb_addr;        /* first Modbus address, from 0 (40001 = 0) */
    uint16_t count;          /* how many registers / bits (5 and 6: always 1) */
    char     area;           /* PLC memory: 'I' 'Q' 'M' 'V' (bits or words), 'A' AIW, 'O' AQW (words) */
    uint16_t byte;           /* first PLC byte: I2 -> 2, VW600 -> 600, AIW4 -> 4 */
    uint8_t  bit;            /* first bit, for coils / discrete inputs (I2.0 -> 0) */
    uint16_t period_ms;      /* read: how often (0 = every cycle)
                                write: written at once when the PLC value changes, and also
                                re-sent every period_ms (0 = every cycle) */
} mb_map_t;

extern const mb_map_t g_mb_map[];
extern const int      g_mb_map_count;

void mb_poll_start(void);                 /* after mb_master_init(); does nothing if the table is empty */

/* called by the PLC task, PLC lock held (fast: memory copies only) */
void mb_poll_apply_inputs(void);
void mb_poll_capture_outputs(void);

void mb_poll_print(void);                 /* "mb map" console command */
