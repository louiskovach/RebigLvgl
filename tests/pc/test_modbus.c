#include <stdio.h>
#include <string.h>
#include "modbus_master.h"
static int fails;
static void hex(const char *t, const uint8_t *b, size_t n){ printf("%-34s", t); for(size_t i=0;i<n;i++) printf("%02X ", b[i]); printf("\n"); }
static void expect(const char *t, int ok){ printf("  %s %s\n", ok?"PASS":"FAIL", t); if(!ok) fails++; }
static size_t mkreply(uint8_t *r, const uint8_t *body, size_t n){ memcpy(r, body, n); uint16_t c=mb_crc16(r,n); r[n]=c&0xFF; r[n+1]=c>>8; return n+2; }
int main(void){
  uint8_t tx[260], rx[260]; size_t n;
  n = mb_build_request(tx, 1, 3, 0, 10, NULL, NULL); hex("FC03 slave 1, 40001 x10:", tx, n);
  expect("matches the standard frame 01 03 00 00 00 0A C5 CD", n==8 && !memcmp(tx,"\x01\x03\x00\x00\x00\x0A\xC5\xCD",8));
  uint16_t v=3; n = mb_build_request(tx, 1, 6, 1, 1, NULL, &v); hex("FC06 slave 1, reg 1 = 3:", tx, n);
  expect("matches 01 06 00 01 00 03 98 0B", n==8 && !memcmp(tx,"\x01\x06\x00\x01\x00\x03\x98\x0B",8));
  uint8_t on=1; n = mb_build_request(tx, 17, 5, 172, 1, &on, NULL); hex("FC05 slave 17, coil 172 on:", tx, n);
  expect("spec example 11 05 00 AC FF 00 4E 8B", n==8 && !memcmp(tx,"\x11\x05\x00\xAC\xFF\x00\x4E\x8B",8));
  uint8_t bits[10]={1,0,1,1,0,0,1,1, 1,0}; n = mb_build_request(tx, 17, 15, 19, 10, bits, NULL); hex("FC15 slave 17, coils 20-29:", tx, n);
  expect("data bytes CD 01 (spec example)", tx[6]==2 && tx[7]==0xCD && tx[8]==0x01);
  uint16_t w[2]={0x000A,0x0102}; n = mb_build_request(tx, 17, 16, 1, 2, NULL, w); hex("FC16 slave 17, 2 registers:", tx, n);
  expect("spec example 11 10 00 01 00 02 04 00 0A 01 02 C6 F0", n==13 && !memcmp(tx,"\x11\x10\x00\x01\x00\x02\x04\x00\x0A\x01\x02\xC6\xF0",13));
  expect("read with broadcast address refused", mb_build_request(tx, 0, 3, 0, 1, NULL, NULL)==0);
  expect("126 registers refused", mb_build_request(tx, 1, 3, 0, 126, NULL, NULL)==0);
  expect("slave 248 refused", mb_build_request(tx, 248, 3, 0, 1, NULL, NULL)==0);
  /* replies */
  uint16_t regs[3]; uint8_t ex=0;
  n = mkreply(rx, (const uint8_t*)"\x01\x03\x06\x02\x2B\x00\x00\x00\x64", 9);
  expect("expected length from first 3 bytes", mb_expected_reply_len(3, 3, rx)==n);
  expect("FC03 reply parsed", mb_parse_reply(rx,n,1,3,0,3,NULL,regs,&ex)==MB_OK && regs[0]==555 && regs[1]==0 && regs[2]==100);
  rx[4]^=1; expect("corrupted byte -> CRC error", mb_parse_reply(rx,n,1,3,0,3,NULL,regs,&ex)==MB_ERR_CRC); rx[4]^=1;
  expect("reply from another slave -> frame error", mb_parse_reply(rx,n,2,3,0,3,NULL,regs,&ex)==MB_ERR_FRAME);
  expect("wrong register count -> frame error", mb_parse_reply(rx,n,1,3,0,2,NULL,regs,&ex)==MB_ERR_FRAME);
  n = mkreply(rx, (const uint8_t*)"\x01\x83\x02", 3);
  expect("exception reply length 5", mb_expected_reply_len(3, 3, rx)==5);
  expect("exception 2 reported", mb_parse_reply(rx,n,1,3,0,3,NULL,regs,&ex)==MB_ERR_EXCEPTION && ex==2);
  uint8_t cb[10]; n = mkreply(rx, (const uint8_t*)"\x11\x01\x02\xCD\x01", 5);
  expect("FC01 reply bits unpacked", mb_parse_reply(rx,n,17,1,19,10,cb,NULL,&ex)==MB_OK && cb[0]==1&&cb[1]==0&&cb[2]==1&&cb[3]==1&&cb[8]==1&&cb[9]==0);
  n = mb_build_request(tx, 1, 6, 1, 1, NULL, &v);
  expect("FC06 echo reply accepted", mb_parse_reply(tx,n,1,6,1,1,NULL,NULL,&ex)==MB_OK);
  printf(fails ? "%d FAILED\n" : "all passed\n", fails); return fails; }
