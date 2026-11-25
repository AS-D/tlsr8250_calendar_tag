/**
 * @file    display.c
 * @brief   gfx 显存 -> SSD1680 面板
 *
 * 这块价签的玻璃在控制器坐标里是镜像 + 有 6 列偏移的，所以送屏前统一做
 * 左右镜像 + 移位补偿 + BW 反相（实测参数，见 epd_conf.h）。
 */
#include "timer.h"          /* 先让 SDK 定义 _attribute_ram_code_ 等宏 */
#include "epd_ssd1680.h"
#include "gfx.h"
#include "display.h"

/* 实测配置，与之前的 epd_app 一致 */
#define GFX_FLIP_X      1
#define EPD_X_SHIFT     6
#define INVERT_BW       1
#define INVERT_RED      0

/* BUSY 必须连续低电平 2 秒才算刷完：三色全刷时 BUSY 会分段释放 */
#define SETTLE_CONFIRM_MS   2000
#define SETTLE_MAX_MS       30000

static int wait_refresh_settle(void)
{
    int total = 0, low_ms = 0;

    while (total < SETTLE_MAX_MS) {
        if (EPD_ReadBusyRaw()) {
            low_ms = 0;
        } else {
            low_ms++;
            if (low_ms >= SETTLE_CONFIRM_MS) break;
        }
        WaitMs(1);
        total++;
        /* 这 2 秒同样把 BLE 栈推一下，别让广播时序在这儿再断一次 */
        if ((total % 50) == 0) EPD_Pump();
    }
    return total;
}

int display_present(void)
{
    int i, t_in, settle, t_tail;

#if GFX_FLIP_X
    gfx_flip_x();
#endif
#if EPD_X_SHIFT > 0
    gfx_shift_left_bits(EPD_X_SHIFT);
#endif
#if INVERT_BW
    for (i = 0; i < GFX_PLANE_BYTES; i++) gfx_bw[i] = (unsigned char)~gfx_bw[i];
#endif
#if INVERT_RED
    for (i = 0; i < GFX_PLANE_BYTES; i++) gfx_red[i] = (unsigned char)~gfx_red[i];
#endif
    /* 红层优先：同一像素既要黑又要红时按屏的物理特性保留红 */
    for (i = 0; i < GFX_PLANE_BYTES; i++)
        gfx_bw[i] &= (unsigned char)~gfx_red[i];

    EPD_Display(gfx_bw, gfx_red);
    t_in   = EPD_GetBusyTicks();
    settle = wait_refresh_settle();
    t_tail = (settle > SETTLE_CONFIRM_MS) ? (settle - SETTLE_CONFIRM_MS) : 0;

    return t_in + t_tail;
}
