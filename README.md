# filter_hmi — ESP-IDF 5.5 + LVGL 9.3 (CrowPanel Advance 7", V1.4+)

## Build
Import into Espressif-IDE, delete any old `sdkconfig`, build. First boot after this update
resets saved settings to defaults (passcode 14789).

## Files
- `main/board_config.h`   pins, LCD timing, co-processor commands (backlight/buzzer), RTC address
- `main/board_periph.c`   buzzer beep task, PCF8563 RTC
- `main/settings.c`       everything saved in NVS (passcode, time zone, network, web server)
- `main/params.c`         the 33 parameters: names, defaults, ranges
- `main/ui/ui_sequence.c` Filter / Stopping / Revive step tables and which parameter sets each time
- `main/ui/ui_idle.c`     Idle screen values (placeholders)

## Navigation
Home: Start Filter -> Filter | Perform Maintenance -> Maintenance | Revive Media -> Revive
      (i) -> Idle | bell -> Alarms | swipe from status bar to top -> Diagnostics
Idle: Filter / Revive Media (shows the running sequence if one is running) | Menu, (i) -> Maintenance | bell -> Alarms
Filter: Stop -> Stopping Filter -> Home | Revive: Stop -> Home | (i) -> Idle (sequence keeps running)
Maintenance: Setup -> Passcode -> Setup | View Settings -> View Information | View Alarms -> Alarms
View Information: Information -> Information | Parameters -> Parameters
Setup: Parameters | Edit Passcode | Advanced | Date/Time | Network -> IP Address / Web Server
Parameters and Alarms: Exit returns to whichever screen opened them.

## PLC hooks (call with lvgl_port_lock held)
ui_filter_start(), ui_filter_stop(), ui_revive_start(), ui_filter_running(), ui_revive_running(),
ui_set_system_status(), ui_alarm_add(), ui_idle_set_analog(), ui_idle_set_output(), ui_sysinfo_set()

## Wi-Fi / Auto Network Time
idf.py menuconfig -> Filter HMI -> Wi-Fi SSID / password / NTP server.
Leave the SSID empty to keep Wi-Fi off. When "Auto Network Time" is checked the clock
is set from the NTP server and written to the RTC; the date/time wheels are locked.

## Sensors
main/sensors.c simulates the four analog inputs (set SENSORS_SIMULATE 0 and call
sensor_set_raw() when real sensors are connected). Scaling is edited under
Advanced (or Diagnostics) -> Analog Scaling and saved in NVS.

## Partitions
partitions.csv gives the app 6 MB (16 MB flash). NVS stays at the same address, so saved
settings are kept.

## Settings across updates
Settings are saved in NVS and reload on every boot. When adding a new setting, add it at
the END of settings_t in settings.h so older saved settings still load.

## TEMP test Wi-Fi
main/wifi_test_credentials.h forces "AtomicBomb II". Delete that file to remove it.

## Splash screen logos
    pip install pillow
    python tools/png_to_lvgl.py aquarevival.png logo_aquarevival --max-width 600
    python tools/png_to_lvgl.py kalamazoo.png   logo_customer    --max-width 500
Then rebuild. Timings are at the top of main/ui/ui_splash.c.

## PLC program (real logic)
- `main/plc/filter.awl`  the filter program (all fixes from the PLC project + HMI changes)
- `main/plc/program.c`   generated: `python tools/awl_to_c.py main/plc/filter.awl main/plc/program.c`
- `main/plc/plc.c/.h`    S7-200 runtime (logic stack, TON/TOF, CTU, edges, I/Q/M/V/SM)
- `main/plc/plc_link.c`  PLC task (10 ms scan, core 0), simulated I/O, screen commands,
  parameters -> timer presets. The full I/O and V-memory map is at the top of plc_link.h.

HMI changes made to the program: timer presets read from VW300-VW322 (written from the
parameters), networks 48-50 (bump selector) replaced by VW0 from parameter 20, and V250.0
(HMI pump on, idle only) added to the pump network for Drain / Rinse.

## PLC debugger
menuconfig -> **PLC runtime -> Debugger** (on by default). Serial monitor on the USB-C port
(COM port, 115200), type `help`. Turn it off for production builds.

| Command | Short | What it does |
|---|---|---|
| `help` | `?` | List the commands |
| `read ADDR...` | `r` | Show values (`:x` hex, `:r` real, `:s` string) |
| `write ADDR VALUE` | `w` | Change a value |
| `force ADDR VALUE` / `unforce ADDR`, `unforce all` / `forces` | | Hold values every scan |
| `watch ADDR...` / `watch off` | | Print values when they change |
| `trace NET [BLOCK]` / `trace all` / `trace off` | | Next scan: each instruction with the logic stack |
| `stack` | | Where the program is (at a breakpoint), and the logic stack |
| `pause` / `resume` / `step [N]` | `s` | Stop/restart scanning; run N scans while paused |
| `break LINE` | `b` | Breakpoint on a line of filter.awl |
| `break net N [BLOCK]` | | Breakpoint at the start of a network |
| `break change ADDR [to VALUE]` | | Stop right after the instruction that changes ADDR |
| `... if ADDR OP VALUE` | | Condition on any breakpoint (`== != < > <= >=`) |
| `breaks` / `delete N`, `delete all` | | List (with hit counts) / remove breakpoints |
| `c` / `continue` | | Carry on from a breakpoint |
| `si [N]` (or `step [N]` at a breakpoint) | | Run N instructions and stop again |
| `reset` / `restart` / `run` | | Power cycle / STOP->RUN / leave STOP |
| `scantime [reset]`, `errors [clear]`, `status` | | Information |

PLC time stands still while stopped at a breakpoint or paused, so timers don't jump.

## Modbus RTU master (RS-485)
`main/modbus_master.c/.h` - UART1 in hardware RS-485 mode: **IO4 = TX** (to DI), **IO5 = RX** (from RO),
**IO6 = enable** (DE + /RE, high while sending). Settings in menuconfig -> **Modbus master (RS-485)**
(default 9600 8N1, 200 ms timeout, 1 retry).

```c
uint16_t regs[10];
if (mb_read_holding_registers(1, 0, 10, regs) == MB_OK) { ... }   /* slave 1, 40001-40010 */
mb_write_register(1, 99, 1500);                                   /* 40100 = 1500 */
mb_write_coil(2, 0, true);                                        /* slave 2, coil 00001 on */
```
Functions 01, 02, 03, 04, 05, 06, 15, 16. Blocking and thread-safe; call from your own task
(not the LVGL task, not with the PLC lock held).

Serial console (PLC debugger on): `mb read 1 hr 0 10`, `mb write 1 hr 100 1500`,
`mb write 2 coil 0 1 0 1`, `mb stats`, `mb help`.

### Modbus <-> PLC exchange (`mb_poll` task)
`main/mb_map.c` lists what to exchange, one line per Modbus request (examples inside).
The **mb_poll** task (core 0, priority 3) does all bus traffic. The PLC task never waits:
- before each scan `mb_poll_apply_inputs()` copies the latest slave values into PLC memory,
  plus a communication-OK bit per line (V500.0 = line 0 ... V503.7 = line 31);
- after each scan `mb_poll_capture_outputs()` copies PLC memory for the write lines.
Reads poll every `period_ms`; writes go out at once when the PLC value changes and are re-sent
every `period_ms`. A failing line is retried every second and keeps its last values.
Console: `mb map` shows every line and its status.
