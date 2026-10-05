/*
 * modbus_master - Modbus RTU master on RS-485 (UART1)
 *
 *   IO4 = TX (to the transceiver's DI)
 *   IO5 = RX (from the transceiver's RO)
 *   IO6 = enable (DE and /RE tied together): high while sending, low while listening
 *
 * Settings (baud, parity, stop bits, timeout, retries, pins) are in menuconfig under
 * "Modbus master (RS-485)". The UART's hardware RS-485 mode drives the enable pin.
 *
 * Addresses are the protocol addresses, starting at 0: holding register 40001 is address 0,
 * input register 30001 is 0, coil 00001 is 0, discrete input 10001 is 0.
 *
 * Every call is blocking (up to timeout x (retries + 1)) and thread-safe: calls from several
 * tasks queue up. Don't call it from the LVGL task or with the PLC lock held.
 * Slave 0 = broadcast (writes only, no reply).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    MB_OK = 0,
    MB_ERR_NOT_INIT,      /* mb_master_init() not called or failed */
    MB_ERR_ARG,           /* bad slave / count / address */
    MB_ERR_TIMEOUT,       /* no reply */
    MB_ERR_CRC,           /* reply with a bad checksum */
    MB_ERR_FRAME,         /* reply from the wrong slave, wrong function, wrong length */
    MB_ERR_EXCEPTION,     /* the slave answered with an exception (see mb_last_exception) */
    MB_ERR_UART,          /* UART driver error */
} mb_err_t;

/* Exception codes from the slave */
#define MB_EX_ILLEGAL_FUNCTION      1
#define MB_EX_ILLEGAL_ADDRESS       2
#define MB_EX_ILLEGAL_VALUE         3
#define MB_EX_SLAVE_FAILURE         4
#define MB_EX_ACKNOWLEDGE           5
#define MB_EX_SLAVE_BUSY            6
#define MB_EX_GATEWAY_PATH          10
#define MB_EX_GATEWAY_NO_RESPONSE   11

typedef struct {
    uint32_t requests, ok, timeouts, crc_errors, frame_errors, exceptions, retries;
} mb_stats_t;

bool        mb_master_init(void);                 /* from menuconfig settings; false on error */
bool        mb_master_ready(void);

/* FC01 / FC02: bits[] gets one 0/1 per coil/input (count 1-2000) */
mb_err_t    mb_read_coils(uint8_t slave, uint16_t addr, uint16_t count, uint8_t *bits);
mb_err_t    mb_read_discrete_inputs(uint8_t slave, uint16_t addr, uint16_t count, uint8_t *bits);
/* FC03 / FC04 (count 1-125) */
mb_err_t    mb_read_holding_registers(uint8_t slave, uint16_t addr, uint16_t count, uint16_t *regs);
mb_err_t    mb_read_input_registers(uint8_t slave, uint16_t addr, uint16_t count, uint16_t *regs);
/* FC05 / FC06 */
mb_err_t    mb_write_coil(uint8_t slave, uint16_t addr, bool on);
mb_err_t    mb_write_register(uint8_t slave, uint16_t addr, uint16_t value);
/* FC15 (count 1-1968) / FC16 (count 1-123) */
mb_err_t    mb_write_coils(uint8_t slave, uint16_t addr, uint16_t count, const uint8_t *bits);
mb_err_t    mb_write_registers(uint8_t slave, uint16_t addr, uint16_t count, const uint16_t *regs);

uint8_t     mb_last_exception(void);              /* exception code of the last MB_ERR_EXCEPTION */
const char *mb_err_str(mb_err_t e);
const char *mb_exception_str(uint8_t code);
void        mb_get_stats(mb_stats_t *out);
void        mb_reset_stats(void);

/* Serial console commands ("mb help"); prints its results with printf */
void        mb_console_command(const char *line);

/* ---- protocol core, no hardware (also used by the PC test) ---- */
uint16_t    mb_crc16(const uint8_t *data, size_t len);
/* Build a request ADU (with CRC) into out[256]; returns its length, 0 on bad arguments */
size_t      mb_build_request(uint8_t *out, uint8_t slave, uint8_t fc, uint16_t addr, uint16_t count,
                             const uint8_t *bits, const uint16_t *regs);
/* Length of the full reply once its first 3 bytes are known (0 = can't tell) */
size_t      mb_expected_reply_len(uint8_t fc, uint16_t count, const uint8_t *first3);
/* Check a reply against its request and copy out the data */
mb_err_t    mb_parse_reply(const uint8_t *rx, size_t len, uint8_t slave, uint8_t fc, uint16_t addr,
                           uint16_t count, uint8_t *bits, uint16_t *regs, uint8_t *exception);
