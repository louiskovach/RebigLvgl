/*
 * mb_map.c - what the Modbus polling task exchanges with the PLC program.
 *
 * One line per Modbus request. Reads put the slave's values into PLC memory before each scan;
 * writes send PLC memory to the slave (at once when it changes, and every period_ms).
 *
 * Communication status: bit n of VB500 onwards is 1 while line n (counting from 0) works,
 * e.g. V500.0 = line 0, V500.1 = line 1 ... V503.7 = line 31 (menuconfig: Modbus master ->
 * status byte). A read line that fails keeps its last values; use its status bit in the
 * program to react.
 *
 * Keep clear of what the program already uses:
 *   I0.x / I1.x  (simulated buttons and pump signal; a line here would override them)
 *   Q0.x / Q1.x  (program outputs: fine to WRITE to a slave, never READ into)
 *   VB0-VB1, VB202-VB250, VW300-VW323
 * Free for Modbus: I2+ / Q2+ / M / AIW / AQW / V from VB600 up, for example.
 *
 * Examples (remove the // to use, and match your devices):
 */
#include "mb_poll.h"

const mb_map_t g_mb_map[] = {
    /* slave 1: discrete inputs 10001-10008 -> I2.0-I2.7, every cycle */
    // { .slave = 1, .fc = 2,  .mb_addr = 0, .count = 8, .area = 'I', .byte = 2, .bit = 0 },

    /* slave 1: input registers 30001-30004 -> AIW0, AIW2, AIW4, AIW6, every 200 ms */
    // { .slave = 1, .fc = 4,  .mb_addr = 0, .count = 4, .area = 'A', .byte = 0, .period_ms = 200 },

    /* Q2.0-Q2.7 -> slave 2 coils 00001-00008, on change and every second */
    // { .slave = 2, .fc = 15, .mb_addr = 0, .count = 8, .area = 'Q', .byte = 2, .bit = 0, .period_ms = 1000 },

    /* VW600 -> slave 3 holding register 40001 (e.g. a drive's speed), on change and every second */
    // { .slave = 3, .fc = 6,  .mb_addr = 0, .count = 1, .area = 'V', .byte = 600, .period_ms = 1000 },

    { 0 }   /* (placeholder so the table isn't empty C; not counted) */
};
const int g_mb_map_count = (int)(sizeof(g_mb_map) / sizeof(g_mb_map[0])) - 1;
