#include <stdio.h>
#include <string.h>
#include "mb_poll.h"
#include "plc.h"
void mb_poll_to_plc(const mb_map_t *m, const uint16_t *data);
void mb_poll_from_plc(const mb_map_t *m, uint16_t *data);
const char *const plc_program_file = "x"; void plc_main(void) {}
const mb_map_t g_mb_map[] = {
    { .slave = 1, .fc = 2,  .mb_addr = 0, .count = 12, .area = 'I', .byte = 2, .bit = 6 },   /* I2.6 - I4.1 */
    { .slave = 1, .fc = 4,  .mb_addr = 0, .count = 3,  .area = 'A', .byte = 0, .period_ms = 200 },
    { .slave = 2, .fc = 15, .mb_addr = 0, .count = 8,  .area = 'Q', .byte = 2 },
    { .slave = 3, .fc = 6,  .mb_addr = 9, .count = 1,  .area = 'V', .byte = 600, .period_ms = 1000 },
    { .slave = 1, .fc = 3,  .mb_addr = 0, .count = 2,  .area = 'Q', .byte = 4 },            /* bad: read into Q */
    { .slave = 0, .fc = 3,  .mb_addr = 0, .count = 1,  .area = 'V', .byte = 700 },          /* bad: slave 0 */
    { .slave = 1, .fc = 6,  .mb_addr = 0, .count = 2,  .area = 'V', .byte = 700 },          /* bad: fc6 count 2 */
    { .slave = 1, .fc = 4,  .mb_addr = 0, .count = 1,  .area = 'A', .byte = 64 },           /* bad: AIW64 */
    { 0 }
};
const int g_mb_map_count = 8;
static int fails; static void expect(const char *t, int ok){ printf("  %s %s\n", ok?"PASS":"FAIL", t); if(!ok) fails++; }
int main(void){
  plc_reset();
  uint16_t d[12] = {1,1,0,1,0,0,1,0,1,1,0,1};
  mb_poll_to_plc(&g_mb_map[0], d);
  expect("bits start at I2.6, cross bytes, end at I4.1", plc_rd_bit('I',2,6)==1 && plc_rd_bit('I',2,7)==1 && plc_rd_bit('I',3,0)==0 &&
         plc_rd_bit('I',3,1)==1 && plc_rd_bit('I',3,6)==1 && plc_rd_bit('I',4,0)==0 && plc_rd_bit('I',4,1)==1);
  expect("bits outside the range untouched", plc_rd_bit('I',2,5)==0 && plc_rd_bit('I',4,2)==0);
  uint16_t r[3] = {1234, 65535, 7}; mb_poll_to_plc(&g_mb_map[1], r);
  expect("registers -> AIW0, AIW2, AIW4", plc_rd_word('A',0)==1234 && plc_rd_word('A',2)==-1 && plc_rd_word('A',4)==7);
  plc_wr_byte('Q', 2, 0xA5); uint16_t o[8]; mb_poll_from_plc(&g_mb_map[2], o);
  expect("QB2 = 16#A5 -> coils 1,0,1,0,0,1,0,1", o[0]==1&&o[1]==0&&o[2]==1&&o[3]==0&&o[4]==0&&o[5]==1&&o[6]==0&&o[7]==1);
  plc_wr_word('V', 600, 1500); uint16_t w; mb_poll_from_plc(&g_mb_map[3], &w);
  expect("VW600 = 1500 -> register value 1500", w == 1500);
  mb_poll_start(); mb_poll_print();
  mb_poll_apply_inputs();
  expect("status bits written (all 0: no replies yet)", plc_rd_byte('V',500)==0);
  printf(fails ? "%d FAILED\n" : "all passed\n", fails); return fails; }
