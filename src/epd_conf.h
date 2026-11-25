/**
 * @file    epd_conf.h
 * @brief   2.13" tri-color e-paper (SSD1680 / SSD1675 class) pin & panel config
 * @target  Telink TLSR8250 / TLSR8258 (TC32), Telink SDK (drivers.h)
 *
 * ============================================================================
 *  ★★★  PINOUT IS NOW MEASURED (2026-11-18) — DO NOT GUESS AGAIN ★★★
 * ============================================================================
 *  The 6 EPD signal wires were traced on the actual tag PCB by the user and
 *  every one of them was cross-checked against the TLSR8250F512ET32 datasheet
 *  Table 1-2 (Figure 1-4).  All 6 are legal QFN32 GPIOs:
 *
 *      FPC pin   signal   chip pin   GPIO     datasheet pin name
 *        9       BUSY         3      PA0      PWM0_N/UART_RX/PA<0>
 *       10       RES          4      PA1      PA<1>
 *       11       D/C         32      PD3      PWM1_N/PD<3>
 *       12       CS          31      PD2      SPI_CN/PWM3/PD<2>
 *       13       SCL          2      PD7      SPI_CK/PD<7>
 *       14       SDA         17      PB7      SPI_DO/UART_RX/PB<7>
 *
 *  NOTE: the chip exposes only 17 GPIOs on QFN32.  Every earlier build used
 *  illegal pins (PB3/PB2/PB0/PA6 do not exist on this package) — that was the
 *  root cause of all blank-screen builds.  This config uses ONLY measured pins.
 *
 *  PWR was traced 2026-11-25: PB1 --R10--> Q3 gate.
 *  Q3 = A1SHB (PW2301A), a P-channel MOSFET; source to battery+, drain feeds
 *  the on-board boost (L150/D150/D151 + caps).  Gate LOW turns the panel
 *  supply ON, gate HIGH turns it OFF.  PB1 is a legal QFN32 GPIO (pin 6) and
 *  was previously unused, so we can now cut the boost rail completely in
 *  standby instead of letting it run 24/7.
 *
 *  BS1 was NOT traced; standard 24-pin modules hard-wire 4-wire SPI, so
 *  GPIO control of BS1 stays disabled.
 * ============================================================================
 */
#ifndef __EPD_CONF_H__
#define __EPD_CONF_H__

/* 标记：本工程覆盖版已生效（tag_app.c 里有编译期检查） */
#define EPD_CONF_LOCAL_OVERRIDE 1

/*---------------------------------------------------------------------------*/
/* 1. PANEL GEOMETRY                                                          */
/*---------------------------------------------------------------------------*/
/*  2.13" tri-color panels are either 122x250 or 128x250.
 *  The Reverse-engineered init stream uses X end = 0x0F (=> 128 columns),
 *  so 128x250 is the default.  If your image looks horizontally compressed or
 *  the right edge is cut, try EPD_WIDTH 122.                                  */
#define EPD_WIDTH   128
#define EPD_HEIGHT  250

/* Number of bytes per plane (1 bit per pixel) */
#define EPD_PLANE_BYTES  ((EPD_WIDTH * EPD_HEIGHT) / 8)

/*---------------------------------------------------------------------------*/
/* 1b. 玻璃基板在控制器 RAM 里的偏移（2026-11-18 实测）                        */
/*---------------------------------------------------------------------------*/
/* 这块 2.13" 面板用的是 128x296 的 SSD1680 控制器，但玻璃只有 122x250，
 * 而且玻璃贴在 RAM 的**远端**：
 *     玻璃行 0..249  = 控制器 Y 46..295
 *     玻璃列 0..121  = 控制器 X 6..127
 * 证据：296-250 = 46，128-122 = 6，实测画面正好整体裁掉左上、右下留一条
 * 46 行 × 6 列的空白带。
 *
 * 所以 RAM 窗口必须从 Y=46 开始；X 的 6 像素不是字节对齐的，靠软件平移。  */
#define EPD_Y_OFFSET   0
#define EPD_X_SHIFT    6

/*---------------------------------------------------------------------------*/
/* 2. GPIO PIN MAPPING — MEASURED FROM THE TAG PCB (see header table)         */
/*---------------------------------------------------------------------------*/
/*  Telink enums use the "P" prefix: GPIO_PD2, NOT GPIO_D2.                   */
#define EPD_PIN_CS      GPIO_PD2    /* FPC pin12 CS  -> chip pin31           */
#define EPD_PIN_CLK     GPIO_PD7    /* FPC pin13 SCL -> chip pin2            */
#define EPD_PIN_MOSI    GPIO_PB7    /* FPC pin14 SDA -> chip pin17           */
#define EPD_PIN_DC      GPIO_PD3    /* FPC pin11 D/C -> chip pin32           */
#define EPD_PIN_RST     GPIO_PA1    /* FPC pin10 RES -> chip pin4 (active lo)*/
#define EPD_PIN_BUSY    GPIO_PA0    /* FPC pin9  BUSY-> chip pin3            */

/* Panel supply switch -- TRACED 2026-11-25.
 * PB1 -> R10 -> Q3 gate.  Q3 = A1SHB / PW2301A P-channel MOSFET:
 *   source = battery+, drain = boost input, gate low = ON, high = OFF.
 * PB1 is QFN32 pin 6 (PWM4/UART_TX/sar_aio), legal and previously unused.
 * Enabling this lets us kill the boost rail after each refresh: EPD_InitGpio
 * drives it low (ON) before init; EPD_DeInit drives it high (OFF) after sleep.
 * If a board turns out to differ, set EPD_USE_PWR_PIN back to 0 to revert.   */
#define EPD_USE_PWR_PIN   1
#define EPD_PIN_PWR       GPIO_PB1
#define EPD_PWR_ACTIVE_LEVEL  0     /* P-channel: LOW enables the rail        */

/* BS1 interface select: NOT traced.  Standard 24-pin 2.13" modules hard-wire
 * BS1 to select 4-wire SPI on the module itself.  Disable GPIO control.      */
#define EPD_USE_BS1_PIN    0
#define EPD_PIN_BS1        GPIO_PA6
#define EPD_BS1_LEVEL      0           /* 0 = select 4-wire SPI                */

/*---------------------------------------------------------------------------*/
/* 3. SIGNAL POLARITY                                                         */
/*---------------------------------------------------------------------------*/
/*  SSD1680 standard: BUSY is HIGH while refreshing.  We do NOT rely on this
 *  to advance the flow (main.c fixes the delay and logs the whole BUSY
 *  waveform instead), so either polarity still refreshes the panel correctly. */
#define EPD_BUSY_ACTIVE_HIGH   1

/*---------------------------------------------------------------------------*/
/* 4. TIMING / CLOCK                                                          */
/*---------------------------------------------------------------------------*/
#define EPD_SPI_DELAY_US    2      /* >1us; bit-bang half-period               */
#define EPD_RESET_PULSE_MS  10
#define EPD_BUSY_TIMEOUT_MS 3000     /* wait long enough for a full refresh   */

/* Panel driver output / VCOM. Default matches the reverse-engineered stream.
 * 0x28,0x01 -> 296 gate lines in ATC ref; 0x2C,0x00-> 44? see note in README. */
#define EPD_GATE_LINES_LO   0xF9
#define EPD_GATE_LINES_HI   0x00

/* Border waveform control (0x3C). Original firmware used 0x05 (floating).
 * Alternatives: 0x80 = VSS, 0x05 = floating, 0x30 = VDD.                     */
#define EPD_BORDER_WAVEFORM 0x05

/* Data entry mode (0x11): 0x01 = X increment, Y increment (matches original) */
#define EPD_DATA_ENTRY_MODE 0x01

#endif /* __EPD_CONF_H__ */
