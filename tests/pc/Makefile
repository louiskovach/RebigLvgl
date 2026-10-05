# PC tests for the PLC runtime, the filter program, the debugger and Modbus.
# Run from this folder:  make        (needs gcc)
M   = ../../main
CC  = gcc
CF  = -std=gnu11 -O2 -w -I$(M) -I$(M)/plc
BIN = bin

all: $(BIN) program debugger modbus mb_poll

$(BIN):
	mkdir -p $(BIN)

program:                       # the filter program's sequences (start, stop, bump, fault, Drain pump)
	$(CC) $(CF) -o $(BIN)/$@ test_program.c $(M)/plc/plc.c $(M)/plc/program.c && ./$(BIN)/$@

debugger:                      # breakpoints, changepoints, si, formats, reset, errors
	$(CC) $(CF) -DPLC_DEBUG=1 -o $(BIN)/$@ test_debugger.c $(M)/plc/plc.c $(M)/plc/program.c $(M)/plc/plc_debug.c && ./$(BIN)/$@

modbus:                        # frames vs. the Modbus spec examples, reply checking
	$(CC) $(CF) -o $(BIN)/$@ test_modbus.c $(M)/modbus_master.c && ./$(BIN)/$@

mb_poll:                       # mb_map line checks, PLC memory mapping
	$(CC) $(CF) -o $(BIN)/$@ test_mb_poll.c $(M)/mb_poll.c $(M)/modbus_master.c $(M)/plc/plc.c && ./$(BIN)/$@

clean:
	rm -rf $(BIN)

.PHONY: all program debugger modbus mb_poll clean
