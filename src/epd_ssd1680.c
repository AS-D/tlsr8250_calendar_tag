/**
 * @file    epd_ssd1680.c
 * @brief   Minimal SSD1680-class 2.13" tri-color EPD driver, Telink TLSR825x
 *
 * Command sequence reconstructed from the Hema price-tag firmware Read.bin
 * (functions @0xAC60/0xAC70/0xACFC/0xB0DE) and cross-checked against the
 * open-source ATC_TLSR_Paper 2.13" BWR driver (SSD1675/SSD1680 mixed).
 *
 * Only depends on Telink SDK: gpio.h (gpio_* API) and WaitMs/WaitUs.
 */
/* Include the Telink SDK headers FIRST so their macros (WaitMs,
 * _attribute_ram_code_, gpio_* API) are defined before our shims.           */
#include "gpio.h"      /* Telink SDK: gpio_write / gpio_set_func / ... */
#include "timer.h"     /* Telink SDK: WaitMs / WaitUs macros               */
#include "lib/include/pm.h"  /* cpu_sleep_wakeup / pm_tim_recover / pm_get_32k_tick */
#include "epd_ssd1680.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Pin map                                                                    */
/* ------------------------------------------------------------------------- */
/* 引脚全部是 epd_conf.h 里的编译期宏（2026-11-18 从价签 PCB 实测出来的，
 * 完整 FPC pin -> chip pin -> GPIO 对照表见 epd_conf.h 头部）。
 * 不再有"运行时候选表"：原来那套是在盲扫引脚时留下的，现在只有一种接法。*/

/* BUSY wait tick counter -- how many ms the panel held BUSY active during the
 * last refresh.  main.c logs this: a plausible value (hundreds of ms) means
 * the controller IS responding; 0 or full-timeout means it is not.           */
static int g_busy_ticks = 0;
int EPD_GetBusyTicks(void) { return g_busy_ticks; }

/* ------------------------------------------------------------------------- */
/* Low-level software SPI (bit-bang), mirrors original @0x9D98/0x9E20         */
/* ------------------------------------------------------------------------- */

_attribute_ram_code_
static inline void EPD_CS_LOW(void)  { gpio_write(EPD_PIN_CS, 0); }
_attribute_ram_code_
static inline void EPD_CS_HIGH(void) { gpio_write(EPD_PIN_CS, 1); }
_attribute_ram_code_
static inline void EPD_DC_CMD(void)  { gpio_write(EPD_PIN_DC, 0); }
_attribute_ram_code_
static inline void EPD_DC_DATA(void) { gpio_write(EPD_PIN_DC, 1); }

_attribute_ram_code_
void EPD_SPI_WriteByte(uint8_t v)
{
    for (int i = 0; i < 8; i++) {
        gpio_write(EPD_PIN_CLK, 0);
        gpio_write(EPD_PIN_MOSI, (v & 0x80) ? 1 : 0);
        v <<= 1;
        EPD_DELAY_US(EPD_SPI_DELAY_US);
        gpio_write(EPD_PIN_CLK, 1);
        EPD_DELAY_US(EPD_SPI_DELAY_US);
    }
    gpio_write(EPD_PIN_CLK, 0);
}

_attribute_ram_code_
void EPD_WriteCmd(uint8_t cmd)
{
    EPD_DC_CMD();
    EPD_CS_LOW();
    EPD_SPI_WriteByte(cmd);
    EPD_CS_HIGH();
}

_attribute_ram_code_
void EPD_WriteData(uint8_t data)
{
    EPD_DC_DATA();
    EPD_CS_LOW();
    EPD_SPI_WriteByte(data);
    EPD_CS_HIGH();
}

_attribute_ram_code_
void EPD_WriteDataBuf(const uint8_t *buf, int len)
{
    EPD_DC_DATA();
    EPD_CS_LOW();
    for (int i = 0; i < len; i++)
        EPD_SPI_WriteByte(buf[i]);
    EPD_CS_HIGH();
}

/* Returns the sampling pin level (raw). Used by the scan firmware to log. */
int EPD_ReadBusyRaw(void)
{
    return gpio_read(EPD_PIN_BUSY) ? 1 : 0;
}

/* 刷屏等待期间的空闲钩子：应用层注册成 blc_sdk_main_loop 之后，每 50ms
 * 醒来都会把 BLE 栈推一次，让广播在刷屏这 20 秒里不断档。*/
static void (*s_idle_hook)(void);

void EPD_SetIdleHook(void (*fn)(void)) { s_idle_hook = fn; }
void EPD_Pump(void) { if (s_idle_hook) s_idle_hook(); }

/* blt_sleep_process：SDK 的完整睡眠入口，醒来会走 blt_pm_process_sleep_wakeup
 * 把射频/时钟重新配好。刷屏等待必须用它，不能直接用 cpu_sleep_wakeup —— 
 * 后者只睡不恢复射频，醒来后广播根本发不出去（表现为刷屏期间手机扫不到）。*/
extern int blt_sleep_process(void);

void EPD_WaitBusy(int max_ms)
{
    g_busy_ticks = 0;
    while (EPD_BUSY_ACTIVE_HIGH ? gpio_read(EPD_PIN_BUSY)
                              : !gpio_read(EPD_PIN_BUSY)) {
        unsigned int t0 = clock_time();
        unsigned int dt;

        /* 屏自己在刷这 20 秒，MCU 没什么事做。交给 SDK 的睡眠流程：
         * 它会睡到下一个 BLE 事件（广播间隔约 200ms），醒来把射频重新配好，
         * 广播继续发 —— 这样刷屏期间手机照样扫得到、也连得上。*/
        blt_sleep_process();
        pm_tim_recover(pm_get_32k_tick());

        dt = (clock_time() - t0) / 16000u;      /* 16MHz tick -> ms */
        if (dt == 0) dt = 1;
        g_busy_ticks += (int)dt;
        max_ms -= (int)dt;
        if (max_ms <= 0) break;
    }
}

/* ------------------------------------------------------------------------- */
/* GPIO setup                                                                 */
/* ------------------------------------------------------------------------- */
void EPD_InitGpio(void)
{
    gpio_set_func(EPD_PIN_CS,   AS_GPIO);
    gpio_set_func(EPD_PIN_CLK,  AS_GPIO);
    gpio_set_func(EPD_PIN_MOSI, AS_GPIO);
    gpio_set_func(EPD_PIN_DC,   AS_GPIO);
    gpio_set_func(EPD_PIN_RST,  AS_GPIO);
    gpio_set_func(EPD_PIN_BUSY, AS_GPIO);

    gpio_set_output_en(EPD_PIN_CS,   1);
    gpio_set_output_en(EPD_PIN_CLK,  1);
    gpio_set_output_en(EPD_PIN_MOSI, 1);
    gpio_set_output_en(EPD_PIN_DC,   1);
    gpio_set_output_en(EPD_PIN_RST,  1);
    gpio_set_output_en(EPD_PIN_BUSY, 0);
    gpio_set_input_en(EPD_PIN_BUSY,  1);
    /* Busy line must not float -- SDK uses a 1M pull-up. */
    gpio_setup_up_down_resistor(EPD_PIN_BUSY, PM_PIN_PULLUP_1M);

    gpio_write(EPD_PIN_CS,  1);   /* CS idle high */
    gpio_write(EPD_PIN_CLK, 0);
    gpio_write(EPD_PIN_MOSI, 0);

#if EPD_USE_BS1_PIN
    /* BS1 selects the interface (LOW = 4-wire SPI on SSD1680/SSD1675). */
    gpio_set_func(EPD_PIN_BS1, AS_GPIO);
    gpio_set_output_en(EPD_PIN_BS1, 1);
    gpio_set_input_en(EPD_PIN_BS1, 0);
    gpio_write(EPD_PIN_BS1, EPD_BS1_LEVEL);
#endif

#if EPD_USE_PWR_PIN
    gpio_set_func(EPD_PIN_PWR, AS_GPIO);
    gpio_set_output_en(EPD_PIN_PWR, 1);
    gpio_write(EPD_PIN_PWR, EPD_PWR_ACTIVE_LEVEL);
    EPD_DELAY_MS(5);
#endif
}

void EPD_Reset(void)
{
    gpio_write(EPD_PIN_RST, 0);
    EPD_DELAY_MS(EPD_RESET_PULSE_MS);
    gpio_write(EPD_PIN_RST, 1);
    EPD_DELAY_MS(EPD_RESET_PULSE_MS);
}

/* ------------------------------------------------------------------------- */
/* Controller initialization — mirrors original 0xAC60/0xAC70 init stream     */
/* ------------------------------------------------------------------------- */
void EPD_Init(void)
{
    EPD_InitGpio();
    EPD_Reset();

    /* Software reset */
    EPD_WriteCmd(0x12);
    EPD_DELAY_MS(10);
    EPD_WaitBusy(EPD_BUSY_TIMEOUT_MS);

    /* ------------------------------------------------------------------
     * 当前配置（v3）刻意保持与上一版逐条一致，只多了一个屏幕上的 V3 标记。
     * 目的：先判定"新固件到底跑没跑"——因为改窗口/指针后画面位置完全没变，
     * 这既可能是控制器不认，也可能是新镜像根本没执行、屏上还是旧的一帧。
     *
     * 若 V3 出现而画面仍偏移，下一轮再逐项改（0x01 第三参、0x11、0x45）。
     * ------------------------------------------------------------------ */


    /* Driver output control: MUX = gate lines */
    EPD_WriteCmd(0x01);
    EPD_WriteData(EPD_GATE_LINES_LO);
    EPD_WriteData(EPD_GATE_LINES_HI);
    EPD_WriteData(0x00);   /* ★ 原厂是 0x00；demo 写 0x01 会翻转扫描方向 */

    /* Data entry mode */
    EPD_WriteCmd(0x11);
    EPD_WriteData(EPD_DATA_ENTRY_MODE);

    /* RAM X range: 0 .. (WIDTH/8 - 1) */
    EPD_WriteCmd(0x44);
    EPD_WriteData(0x00);
    EPD_WriteData((uint8_t)(EPD_WIDTH / 8 - 1));   /* 128 -> 0x0F */

    /* RAM Y range: 与原厂一样写 4 字节（Y 递减，所以先写末行） */
    /* ★ 严格照抄原厂 0xAC70：F9 00 00 00。
     * 之前试过把窗口挪到 46..295（两种字节排布都试了），结果写入被整段丢弃，
     * 画面只更新了一小块 —— 这条路放弃，改用软件翻 Y。 */
    EPD_WriteCmd(0x45);
    EPD_WriteData(0xF9);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);

    /* Border waveform */
    EPD_WriteCmd(0x3C);
    EPD_WriteData(EPD_BORDER_WAVEFORM);

    /* Temperature sensor: use internal */
    EPD_WriteCmd(0x18);
    EPD_WriteData(0x80);


}

/* ------------------------------------------------------------------------- */
/* Full-screen refresh — mirrors original 0xB0DE / 0xACFC                     */
/* ------------------------------------------------------------------------- */
_attribute_ram_code_
void EPD_Display(const uint8_t *bw, const uint8_t *red)
{
    static const uint8_t zero_plane[EPD_PLANE_BYTES] = {0};

    /* --- black / white plane (0x24) --- */
    EPD_WriteCmd(0x4E); EPD_WriteData(0x00);                 /* RAM X addr */
    EPD_WriteCmd(0x4F);
    EPD_WriteData((uint8_t)((EPD_Y_OFFSET + EPD_HEIGHT - 1) >> 0));
    EPD_WriteData((uint8_t)((EPD_Y_OFFSET + EPD_HEIGHT - 1) >> 8));  /* RAM Y addr */
    EPD_WriteCmd(0x24);
    if (bw) EPD_WriteDataBuf(bw, EPD_PLANE_BYTES);
    else    EPD_WriteDataBuf(zero_plane, EPD_PLANE_BYTES);   /* all white */

    /* --- red plane (0x26) --- */
    EPD_WriteCmd(0x4E); EPD_WriteData(0x00);
    EPD_WriteCmd(0x4F);
    EPD_WriteData((uint8_t)((EPD_Y_OFFSET + EPD_HEIGHT - 1) >> 0));
    EPD_WriteData((uint8_t)((EPD_Y_OFFSET + EPD_HEIGHT - 1) >> 8));
    EPD_WriteCmd(0x26);
    if (red) EPD_WriteDataBuf(red, EPD_PLANE_BYTES);
    else     EPD_WriteDataBuf(zero_plane, EPD_PLANE_BYTES);  /* no red */

    /* --- activate full refresh: 0x22 = 0xF7, 0x20  (original @0xACFC) --- */
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xF7);
    EPD_WriteCmd(0x20);
    EPD_DELAY_MS(10);
    EPD_WaitBusy(EPD_BUSY_TIMEOUT_MS);
}

void EPD_Clear(void)
{
    EPD_Display(NULL, NULL);   /* white, no red */
}

void EPD_DeInit(void)
{
    EPD_WriteCmd(0x10);        /* deep sleep */
    EPD_WriteData(0x01);
}

/* ------------------------------------------------------------------------- */
/* Test patterns                                                              */
/* ------------------------------------------------------------------------- */

/* Three horizontal bars: white / black / red  -> proves all 3 colors work */
void EPD_MakePattern_TriBars(uint8_t *bw, uint8_t *red)
{
    memset(bw,  0x00, EPD_PLANE_BYTES);   /* default white, no red */
    memset(red, 0x00, EPD_PLANE_BYTES);

    int row_bytes = EPD_WIDTH / 8;
    int y0 = EPD_HEIGHT / 3;              /* white  : top third             */
    int y1 = 2 * EPD_HEIGHT / 3;          /* black  : middle third          */
                                          /* red    : bottom third          */
    for (int y = y0; y < y1; y++)         /* black bar in BW plane */
        memset(&bw[y * row_bytes], 0xFF, row_bytes);
    for (int y = y1; y < EPD_HEIGHT; y++) /* red bar in RED plane */
        memset(&red[y * row_bytes], 0xFF, row_bytes);
}

/* Checkerboard in B/W + red diagonal band -> also stresses RAM addressing */
void EPD_MakePattern_Checker(uint8_t *bw, uint8_t *red)
{
    memset(bw,  0x00, EPD_PLANE_BYTES);
    memset(red, 0x00, EPD_PLANE_BYTES);
    int row_bytes = EPD_WIDTH / 8;
    int cell = 16;                        /* 16px cells */
    for (int y = 0; y < EPD_HEIGHT; y++) {
        int cy = y / cell;
        for (int x = 0; x < EPD_WIDTH; x++) {
            int cx = x / cell;
            if ((cx + cy) & 1)
                bw[y * row_bytes + (x >> 3)] |=  (1 << (7 - (x & 7)));
            /* red diagonal stripe */
            if (x > y && x < y + 24)
                red[y * row_bytes + (x >> 3)] |= (1 << (7 - (x & 7)));
        }
    }
}

/* Invert a previously built pattern (handy to test multiple full refreshes) */
void EPD_MakePattern_Invert(uint8_t *bw, uint8_t *red)
{
    for (int i = 0; i < EPD_PLANE_BYTES; i++) {
        bw[i]  = ~bw[i];
        red[i] = ~red[i];
    }
}
