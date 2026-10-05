/*
 * Board configuration - Elecrow CrowPanel Advance 7.0" (ESP32-S3, 800x480 RGB, GT911)
 *
 * !! VERIFY THESE AGAINST YOUR BOARD REVISION (printed on the back of the PCB). !!
 * Elecrow changed backlight/touch-reset handling between Advance hardware revisions.
 * Everything board-specific lives in this one file.
 */
#pragma once

/* ---------------- LCD panel ---------------- */
#define BOARD_LCD_H_RES            800
#define BOARD_LCD_V_RES            480
#define BOARD_LCD_PCLK_HZ          (16 * 1000 * 1000)  /* lower = more PSRAM headroom */

/* If the image is shifted, wraps, or rolls, try the alternate set in the comments. */
#define BOARD_LCD_HSYNC_PULSE      4     /* alt: 48 */
#define BOARD_LCD_HSYNC_BACK       8     /* alt: 40 */
#define BOARD_LCD_HSYNC_FRONT      8     /* alt: 40 */
#define BOARD_LCD_VSYNC_PULSE      4     /* alt: 31 */
#define BOARD_LCD_VSYNC_BACK       8     /* alt: 13 */
#define BOARD_LCD_VSYNC_FRONT      8     /* alt: 1  */
#define BOARD_LCD_PCLK_ACTIVE_NEG  1

#define BOARD_LCD_PIN_DE           42
#define BOARD_LCD_PIN_VSYNC        41
#define BOARD_LCD_PIN_HSYNC        40
#define BOARD_LCD_PIN_PCLK         39

/* RGB565 data bus: D0..D4 = B0..B4, D5..D10 = G0..G5, D11..D15 = R0..R4 */
#define BOARD_LCD_PIN_B0 21
#define BOARD_LCD_PIN_B1 47
#define BOARD_LCD_PIN_B2 48
#define BOARD_LCD_PIN_B3 45
#define BOARD_LCD_PIN_B4 38
#define BOARD_LCD_PIN_G0 9
#define BOARD_LCD_PIN_G1 10
#define BOARD_LCD_PIN_G2 11
#define BOARD_LCD_PIN_G3 12
#define BOARD_LCD_PIN_G4 13
#define BOARD_LCD_PIN_G5 14
#define BOARD_LCD_PIN_R0 7
#define BOARD_LCD_PIN_R1 17
#define BOARD_LCD_PIN_R2 18
#define BOARD_LCD_PIN_R3 3
#define BOARD_LCD_PIN_R4 46

/* ---------------- I2C (touch + board co-processor) ---------------- */
#define BOARD_I2C_SDA              15
#define BOARD_I2C_SCL              16
#define BOARD_I2C_HZ               400000

/* ---------------- Touch (GT911) ---------------- */
#define BOARD_TOUCH_RST            (-1)  /* reset handled by the board, not a GPIO */
#define BOARD_TOUCH_INT            (-1)
#define BOARD_TOUCH_SWAP_XY        0
#define BOARD_TOUCH_MIRROR_X       0
#define BOARD_TOUCH_MIRROR_Y       0

/* ---------------- Board co-processor (STC8H1K28, board V1.4+) ----------------
 * Backlight: 0 = max brightness ... 244 = min, 245 = off
 * Buzzer:    246 = on, 247 = off
 */
#define BOARD_COPROC_ADDR          0x30
#define BOARD_BL_ON_CMD            0x10
#define BOARD_BUZZER_ON_CMD        246
#define BOARD_BUZZER_OFF_CMD       247

/* ---------------- RTC (PCF8563, coin-cell backed) ---------------- */
#define BOARD_RTC_ADDR             0x51
