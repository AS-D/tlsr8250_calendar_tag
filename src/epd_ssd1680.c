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
#include "epd_ssd1680.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Runtime pin map                                                            */
/* ------------------------------------------------------------------------- */
/* MEASURED pinout (2026-11-18) -- see epd_conf.h header for the full FPC pin
 * -> chip pin -> GPIO table, all cross-checked against the datasheet.  Only
 * ONE valid candidate exists now; the old blind-scan table is gone.          */
static int g_pin_cs, g_pin_clk, g_pin_mosi, g_pin_dc, g_pin_rst, g_pin_busy;
static int g_pin_bs1, g_pin_pwr;
static int g_pwr_active, g_bs1_level, g_busy_active_high;

/* BUSY wait tick counter -- how many ms the panel held BUSY active during the
 * last refresh.  main.c logs this: a plausible value (hundreds of ms) means
 * the controller IS responding; 0 or full-timeout means it is not.           */
static int g_busy_ticks = 0;
int EPD_GetBusyTicks(void) { return g_busy_ticks; }

typedef struct {
    int cs, ck, mosi, dc, rst, busy, bs1, pwr;
    int pwr_act;   /* level that turns panel power ON (0 or 1) */
    int bs1_lv;    /* level selecting 4-wire SPI (usually 0)   */
} EPD_Candidate;

static const EPD_Candidate kCand[] = {
    /* 0: MEASURED from the tag PCB. CS=PD2 CK=PD7 DO=PB7 DC=PD3
     *    RST=PA1 BUSY=PA0. PWR/BS1 controlled by board hardware. */
    { EPD_PIN_CS, EPD_PIN_CLK, EPD_PIN_MOSI, EPD_PIN_DC,
      EPD_PIN_RST, EPD_PIN_BUSY, EPD_PIN_BS1, EPD_PIN_PWR,
      EPD_PWR_ACTIVE_LEVEL, EPD_BS1_LEVEL },
};
#define EPD_CAND_COUNT  ((int)(sizeof(kCand)/sizeof(kCand[0])))

void EPD_SetCandidate(int idx)
{
    if (idx < 0 || idx >= EPD_CAND_COUNT) idx = 0;
    const EPD_Candidate *c = &kCand[idx];
    g_pin_cs = c->cs;   g_pin_clk = c->ck;  g_pin_mosi = c->mosi;
    g_pin_dc = c->dc;   g_pin_rst = c->rst; g_pin_busy = c->busy;
    g_pin_bs1 = c->bs1; g_pin_pwr = c->pwr;
    g_pwr_active = c->pwr_act; g_bs1_level = c->bs1_lv;
    g_busy_active_high = EPD_BUSY_ACTIVE_HIGH;
}

/* Default = candidate 0, so normal builds behave exactly as before. */
static int s_candidate_ready = 0;
static void EPD_EnsureCandidate(void)
{
    if (!s_candidate_ready) { EPD_SetCandidate(0); s_candidate_ready = 1; }
}

/* ------------------------------------------------------------------------- */
/* Low-level software SPI (bit-bang), mirrors original @0x9D98/0x9E20         */
/* ------------------------------------------------------------------------- */

_attribute_ram_code_
static inline void EPD_CS_LOW(void)  { gpio_write((GPIO_PinTypeDef)g_pin_cs, 0); }
_attribute_ram_code_
static inline void EPD_CS_HIGH(void) { gpio_write((GPIO_PinTypeDef)g_pin_cs, 1); }
_attribute_ram_code_
static inline void EPD_DC_CMD(void)  { gpio_write((GPIO_PinTypeDef)g_pin_dc, 0); }
_attribute_ram_code_
static inline void EPD_DC_DATA(void) { gpio_write((GPIO_PinTypeDef)g_pin_dc, 1); }

_attribute_ram_code_
void EPD_SPI_WriteByte(uint8_t v)
{
    for (int i = 0; i < 8; i++) {
        gpio_write((GPIO_PinTypeDef)g_pin_clk, 0);
        gpio_write((GPIO_PinTypeDef)g_pin_mosi, (v & 0x80) ? 1 : 0);
        v <<= 1;
        EPD_DELAY_US(EPD_SPI_DELAY_US);
        gpio_write((GPIO_PinTypeDef)g_pin_clk, 1);
        EPD_DELAY_US(EPD_SPI_DELAY_US);
    }
    gpio_write((GPIO_PinTypeDef)g_pin_clk, 0);
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
    return gpio_read((GPIO_PinTypeDef)g_pin_busy) ? 1 : 0;
}

_attribute_ram_code_
void EPD_WaitBusy(int max_ms)
{
    g_busy_ticks = 0;
    while (g_busy_active_high ? gpio_read((GPIO_PinTypeDef)g_pin_busy)
                              : !gpio_read((GPIO_PinTypeDef)g_pin_busy)) {
        EPD_DELAY_MS(1);
        g_busy_ticks++;
        if (--max_ms <= 0) break;
    }
}

/* ------------------------------------------------------------------------- */
/* GPIO setup                                                                 */
/* ------------------------------------------------------------------------- */
void EPD_InitGpio(void)
{
    EPD_EnsureCandidate();
    gpio_set_func((GPIO_PinTypeDef)g_pin_cs,   AS_GPIO);
    gpio_set_func((GPIO_PinTypeDef)g_pin_clk,  AS_GPIO);
    gpio_set_func((GPIO_PinTypeDef)g_pin_mosi, AS_GPIO);
    gpio_set_func((GPIO_PinTypeDef)g_pin_dc,   AS_GPIO);
    gpio_set_func((GPIO_PinTypeDef)g_pin_rst,  AS_GPIO);
    gpio_set_func((GPIO_PinTypeDef)g_pin_busy, AS_GPIO);

    gpio_set_output_en((GPIO_PinTypeDef)g_pin_cs,   1);
    gpio_set_output_en((GPIO_PinTypeDef)g_pin_clk,  1);
    gpio_set_output_en((GPIO_PinTypeDef)g_pin_mosi, 1);
    gpio_set_output_en((GPIO_PinTypeDef)g_pin_dc,   1);
    gpio_set_output_en((GPIO_PinTypeDef)g_pin_rst,  1);
    gpio_set_output_en((GPIO_PinTypeDef)g_pin_busy, 0);
    gpio_set_input_en((GPIO_PinTypeDef)g_pin_busy,  1);
    /* Busy line must not float -- SDK uses a 1M pull-up. */
    gpio_setup_up_down_resistor((GPIO_PinTypeDef)g_pin_busy, PM_PIN_PULLUP_1M);

    gpio_write((GPIO_PinTypeDef)g_pin_cs,  1);   /* CS idle high */
    gpio_write((GPIO_PinTypeDef)g_pin_clk, 0);
    gpio_write((GPIO_PinTypeDef)g_pin_mosi, 0);

#if EPD_USE_BS1_PIN
    /* BS1 selects the interface (LOW = 4-wire SPI on SSD1680/SSD1675). */
    gpio_set_func((GPIO_PinTypeDef)g_pin_bs1, AS_GPIO);
    gpio_set_output_en((GPIO_PinTypeDef)g_pin_bs1, 1);
    gpio_set_input_en((GPIO_PinTypeDef)g_pin_bs1, 0);
    gpio_write((GPIO_PinTypeDef)g_pin_bs1, g_bs1_level);
#endif

#if EPD_USE_PWR_PIN
    gpio_set_func((GPIO_PinTypeDef)g_pin_pwr, AS_GPIO);
    gpio_set_output_en((GPIO_PinTypeDef)g_pin_pwr, 1);
    gpio_write((GPIO_PinTypeDef)g_pin_pwr, g_pwr_active);
    EPD_DELAY_MS(5);
#endif
}

void EPD_Reset(void)
{
    gpio_write((GPIO_PinTypeDef)g_pin_rst, 0);
    EPD_DELAY_MS(EPD_RESET_PULSE_MS);
    gpio_write((GPIO_PinTypeDef)g_pin_rst, 1);
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
