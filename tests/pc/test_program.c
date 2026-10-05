#include <stdio.h>
#include "plc.h"
static uint32_t now; static int proof_ok = 1; static uint32_t q0_on_at;
static void presets(void){ int v[] = {50,50,60,50,50,50,50,50,50,50,20,30};
  for (int i=0;i<12;i++) plc_wr_word('V',300+2*i,(int16_t)v[i]); plc_wr_word('V',0,3); }
static void scan(void){
  int q0 = plc_rd_bit('Q',0,0);
  if (q0 && !q0_on_at) q0_on_at = now; if (!q0) q0_on_at = 0;
  plc_wr_bit('I',1,3, proof_ok && q0 && now - q0_on_at >= 1000);
  presets(); plc_scan(plc_main, now); now += 10; }
static int last=-1;
static void run(uint32_t until){ while (now < until){ scan(); int s=plc_rd_byte('V',202);
  if (s!=last){ printf("  %6.2fs state %2d  Q0=%02X Q1=%02X C0=%d\n", now/1000.0, s, plc_rd_byte('Q',0), plc_rd_byte('Q',1), plc_c_count(0)); last=s; } } }
static void pulse(char a,int by,int bi,int v){ plc_wr_bit(a,by,bi,v); run(now+200); plc_wr_bit(a,by,bi,!v); }
int main(void){
  plc_reset(); plc_wr_bit('I',1,0,1);
  printf("== start (precoat 300 s, short here)\n");
  plc_wr_word('V',304,60);  /* set 6 s precoat for the test */
  pulse('I',0,0,1); run(now+30000);
  printf("== bump while running (3 bumps) -> stop steps, bump, restart\n");
  pulse('I',0,4,1); run(now+80000);
  printf("== stop while running\n"); pulse('I',1,2,1); run(now+30000);
  printf("== bump from idle\n"); pulse('I',0,4,1); run(now+25000);
  printf("== manual pump V250.0\n"); plc_wr_bit('V',250,0,1); run(now+3000);
  printf("  Q0.0=%d I1.3=%d fault=%d\n", plc_rd_bit('Q',0,0), plc_rd_bit('I',1,3), plc_rd_bit('V',210,0));
  plc_wr_bit('V',250,0,0); run(now+2000); printf("  off: Q0.0=%d\n", plc_rd_bit('Q',0,0));
  printf("== pump fault (no proof)\n"); proof_ok=0; pulse('I',0,0,1); run(now+20000);
  printf("  fault V210.0=%d Q1.1=%d\n", plc_rd_bit('V',210,0), plc_rd_bit('Q',1,1));
  proof_ok=1; pulse('I',1,0,0); run(now+500); printf("  after reset: fault=%d\n", plc_rd_bit('V',210,0));
  printf("== stop during start-up (state 3)\n"); pulse('I',0,0,1); run(now+12000); pulse('I',1,2,1); run(now+20000);
  return 0; }
