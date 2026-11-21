/**
 * @file    epd_ssd1680.h
 * @brief   Minimal SSD1680-class 2.13" tri-color EPD driver for Telink TLSR825x
 */
#ifndef __EPD_SSD1680_H__
#define __EPD_SSD1680_H__

/* Type compatibility:
 * Telink TC32 SDK defines u8/u16/u32 in common/types.h and its <stdint.h> is
 * only a stub (no real types).  Detect the SDK via the CHIP_TYPE macro that
 * every Telink SDK build defines; otherwise fall back to host <stdint.h>.    */
#ifdef CHIP_TYPE
/* Telink SDK path: uint*_t are not provided, define them as plain types.     */
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
#else
#include <stdint.h>
#endif

#include "epd_conf.h"

/* ---- SDK compatibility shims -------------------------------------------- */
/* Telink SDK puts time-critical code in RAM with this attribute.  If your
 * SDK does not define it, an empty definition is fine for a test firmware.   */
#ifndef _attribute_ram_code_
#define _attribute_ram_code_
#endif

/* Delay hooks: override in compiler flags (-DEPD_DELAY_MS=my_delay_ms) if
 * your SDK does not provide WaitMs()/WaitUs().                                */
#ifndef EPD_DELAY_MS
#define EPD_DELAY_MS(ms)    WaitMs(ms)
#endif
#ifndef EPD_DELAY_US
#define EPD_DELAY_US(us)    WaitUs(us)
#endif

/* Telink SDK defines WaitMs()/WaitUs() as macros in drivers/B85/timer.h.
 * We do NOT redeclare them here (that would conflict with the SDK macros).
 * If your SDK lacks them, define EPD_DELAY_MS/EPD_DELAY_US in compiler flags. */

#ifdef __cplusplus
extern "C" {
#endif

/* 引脚是 epd_conf.h 的编译期宏，没有运行时重映射，也没有 EPD_SetCandidate。 */
int      EPD_ReadBusyRaw(void);
int      EPD_GetBusyTicks(void);   /* ms BUSY stayed active in last refresh */

void     EPD_Init(void);
void     EPD_InitGpio(void);   /* exposed for the LED-based diagnostic entry */
void     EPD_DeInit(void);
void     EPD_Reset(void);
void     EPD_Clear(void);

void     EPD_WriteCmd(uint8_t cmd);
void     EPD_WriteData(uint8_t data);
void     EPD_WriteDataBuf(const uint8_t *buf, int len);
void     EPD_WaitBusy(int max_ms);

/** Display two planes.
 *  @param bw   black/white plane: 1=black, 0=white   (NULL => all white)
 *  @param red  red plane:         1=red,   0=none    (NULL => all 0)
 */
void     EPD_Display(const uint8_t *bw, const uint8_t *red);

/* Test pattern generators (buffers must be >= EPD_PLANE_BYTES) */
void     EPD_MakePattern_TriBars(uint8_t *bw, uint8_t *red);
void     EPD_MakePattern_Checker(uint8_t *bw, uint8_t *red);
void     EPD_MakePattern_Invert(uint8_t *bw, uint8_t *red);

/* Test entry point implemented in main.c -- call this once from user_init(). */
void     EPD_TestMain(void);

#ifdef __cplusplus
}
#endif

#endif /* __EPD_SSD1680_H__ */
