/* PC test: runs the real program with the debugger; commands queued with timestamps,
   and while stopped at a breakpoint wait() feeds the next queued command. */
#include <stdio.h>
#include <string.h>
#include "plc.h"
#include "plc_debug.h"
static uint32_t now, vt; static uint32_t q0_on;
static const char *queue[64]; static int qn, qi;
static uint32_t clk(void){ return now; }
static void run_cmd(const char *c){ printf("%s%s\n", plc_debug_prompt(), c); plc_debug_command(c); }
static void wait_stopped(void){ if (qi < qn) run_cmd(queue[qi++]); else { printf("(queue empty, continuing)\n"); plc_debug_command("c"); } now += 5; }
static void scan(void){
  plc_debug_begin_scan();
  int q0 = plc_rd_bit('Q',0,0); if(q0&&!q0_on) q0_on=vt?vt:1; if(!q0) q0_on=0;
  plc_wr_bit('I',1,3, q0 && vt-q0_on>=1000); plc_wr_bit('I',1,0,1);
  int v[]={50,50,60,50,50,50,50,50,50,50,20,30}; for(int i=0;i<12;i++) plc_wr_word('V',300+2*i,v[i]);
  plc_wr_word('V',0,1);
  if (plc_debug_should_scan()) { plc_debug_before_scan(); plc_scan(plc_main, vt); plc_debug_after_scan(vt, 40); }
  now += 10; vt += 10; vt -= 0*plc_debug_take_stopped_ms(); }
static void run(uint32_t ms){ uint32_t e=now+ms; while(now<e) scan(); }
int main(void){ plc_reset(); plc_debug_init(wait_stopped, clk);
  run(100);
  run_cmd("break change VB202 to 3");
  run_cmd("b 21");          /* line 21 = MOVB 1, VB202? */
  run_cmd("break net 30 if T47 >= 0");
  run_cmd("delete 3");
  run_cmd("breaks");
  queue[qn++]="stack"; queue[qn++]="r VB202 T37 QB0:x"; queue[qn++]="si"; queue[qn++]="si 2"; queue[qn++]="delete 2"; queue[qn++]="c";
  queue[qn++]="r T38"; queue[qn++]="c";
  run_cmd("force I0.0 1"); run(200); run_cmd("unforce all");
  run(12000);
  run_cmd("breaks");
  run_cmd("delete all");
  run_cmd("break 5"); run_cmd("break 9999");
  run_cmd("w VD100:r 1.5"); run_cmd("r VD100:r VD100:x"); run_cmd("w VB400:s 'HELLO WORLD'"); run_cmd("r VB400:s VB400");
  run_cmd("r SMW22 SM0.0 AIW0 S0.1 VB10239 VB10240"); run_cmd("r SM0");
  run_cmd("trace 26"); run(10);
  run_cmd("reset"); run(10); run_cmd("r VB202 SM0.3"); run(10); run_cmd("r SM0.3");
  run_cmd("errors"); run_cmd("status"); run_cmd("frob"); run_cmd("help");
  return 0; }
