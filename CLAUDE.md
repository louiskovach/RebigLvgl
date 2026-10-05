# CLAUDE.md - Filter HMI

Touchscreen controller for a filter system ("AquaRevival" revive-media filter): an ESP32-S3 HMI
that runs the machine's original Siemens S7-200 PLC program (converted to C) and recreates the
original unit's screens.

## Hardware
- Elecrow CrowPanel Advance 7" (ESP32-S3, 16 MB flash, 8 MB octal PSRAM, 800x480 RGB, GT911 touch)
- Board co-processor at I2C 0x30 (backlight, buzzer), PCF8563 RTC at 0x51
- RS-485 Modbus: UART1, IO4 TX, IO5 RX, IO6 DE//RE. Pins in `main/board_config.h` and menuconfig
- Log + debug console: UART0 = the USB-C port (COM port), 115200

## Build
- ESP-IDF **5.5**, LVGL **9.3** via the component manager (`main/idf_component.yml`)
- `idf.py build` (or Espressif-IDE). After changing `sdkconfig.defaults`, **delete `sdkconfig`** so it regenerates
- Warnings are errors (IDF uses `-Werror=all`; `-Wformat-truncation` bites snprintf into small buffers)
- PC tests: `cd tests/pc && make` (gcc). Run them after touching `main/plc/*`, `modbus_master.c`, `mb_poll.c`

## Layout
```
main/
  main.c                 boot order: NVS -> display (backlight off) -> clock -> Wi-Fi -> UI -> first frame
                         -> backlight on -> PLC task -> Modbus master + poll task
  board_*, display_driver.*   panel, touch, backlight, buzzer, RTC
  lv_mem_psram.c         LVGL allocator -> PSRAM (CONFIG_LV_USE_CUSTOM_MALLOC); needs WHOLE_ARCHIVE in CMake
  params.*, settings.*   34 parameters + NVS settings (append new fields to settings_t, never reorder)
  sensors.*              analog inputs: NOT connected yet, screens show "--"
  net_time.*             Wi-Fi + SNTP (test credentials: wifi_test_credentials.example.h)
  modbus_master.*        Modbus RTU master (own code on the IDF UART driver, hardware RS-485 mode)
  mb_poll.*, mb_map.c    "mb_poll" task: Modbus slaves <-> PLC memory, table in mb_map.c (empty for now)
  plc/
    filter.awl           THE PROGRAM (S7-200 STL). Edit this, never program.c
    program.c            generated: python tools/awl_to_c.py main/plc/filter.awl main/plc/program.c
    plc.c/.h             S7-200 runtime: logic stack, TON/TOF (100 ms), CTU, EU/ED, I Q M V S SM AIW AQW
    plc_link.c/.h        PLC task (10 ms scan, core 0), simulated I/O, screen commands, snapshot, presets
    plc_debug.c/.h       serial debugger (menuconfig PLC runtime -> Debugger), type "help"
  ui/                    all screens (ui.c = styles, navigation; ui_sequence.c = PLC-driven sequences)
tools/   awl_to_c.py (STL -> C), png_to_lvgl.py (images -> LVGL C arrays)
tests/pc/   PC tests (program sequences, debugger, Modbus frames, mb_poll mapping)
```

## How the pieces talk (keep it this way)
- **PLC task** owns all PLC memory and holds `s_lock` for a whole scan.
- **Screens never touch PLC memory.** They read a 20-byte `plc_snap_t` via `plc_link_get()` (taken
  after each scan, under the lock) and send commands (`plc_link_cmd_start()` etc.: 200 ms input pulses,
  lock-free 32-bit writes). `follow_tick()` in ui_sequence.c polls every 100 ms.
- Step countdowns come from the PLC's own timers (`step_ms`) and the presets the program actually
  used (`plc_link_state_time_ms()`), never from a screen clock.
- Parameters -> timer presets VW300-VW322 and bump count VW0, rewritten every scan (`plc_link_load_params()`).
- **Modbus** runs only in the `mb_poll` task (and the console). The PLC task swaps data with it through
  a shadow copy (`mb_poll_apply_inputs()` before the scan, `mb_poll_capture_outputs()` after).
- Lock order: PLC task may take `s_img` while holding `s_lock`; nothing takes `s_lock` while holding `s_img`.
  The PLC task never takes the LVGL lock. Never call Modbus from the PLC or LVGL task.

## The program (filter.awl)
State in VB202: 0 idle, 1-5 start-up, 6 running (Filter Mode), 10-14 stop steps, 15-19 bump (revive).
I/O: I0.0 Start, I0.2 immediate stop, I0.4 bump, I1.0 fault reset (falling edge), I1.2 Stop, I1.3 pump
running; Q0.0 pump, Q0.1 regen valve, Q0.2 effluent valve, Q0.3 bump valve, Q1.0 running, Q1.1 pump fault.
HMI additions vs. the original: timer presets from VW300-VW322, bump count VW0 (old selector networks
48-50 removed), V250.0 = HMI pump on in idle (Drain/Rinse). Full map at the top of `plc/plc_link.h`.
Inputs are SIMULATED (buttons pulse inputs; I1.3 follows Q0.0 after 1 s) until real I/O exists.

## Gotchas learned the hard way
- **Internal RAM is tight.** Display bounce buffers + 2 x 32 KB LVGL draw buffers + Wi-Fi. Big tables go
  in PSRAM (`PLC_PSRAM` / `EXT_RAM_BSS_ATTR`), LVGL objects go in PSRAM (lv_mem_psram.c), tasks
  that start late (PLC, Modbus). Symptoms of running out: Wi-Fi `malloc buffer fail`, abort in
  `lock_init_generic`, tasks silently not created. Check the "free internal RAM" boot line.
- `file(GLOB ...)` in main/CMakeLists.txt has **no CONFIGURE_DEPENDS** (breaks IDF script mode):
  clean build after adding image files.
- Console must be UART0 (`CONFIG_ESP_CONSOLE_UART_DEFAULT`), not USB-Serial-JTAG: the board's USB-C is UART0.
- Edge memories (EU/ED) are numbered by the converter; the runtime has 256.
- Debugger line numbers refer to filter.awl. PLC time stands still while paused / at a breakpoint.

## Not done yet
Real I/O (GPIO or Modbus via mb_map.c), analog sensors and the health % (delta-P vs. parameter 5),
Service IO forcing, Modbus tested on a real bus.
