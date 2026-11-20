/**
 * @file    app_time.c
 * @brief   软件走时实现
 *
 * 计时源：SDK 的 clock_time()（reg_system_tick）。
 * 这颗芯片的系统定时器是 **16 个 tick 每微秒**（16MHz），
 * 依据是 SDK 里 clock_time_exceed(ref, us) == (clock_time()-ref) > us*16，
 * 以及 CLOCK_SYS_TIMER_CLK_1S = 16*1000*1000。
 *
 * 该 tick 由 24MHz 主晶振倍频而来，典型 ±20ppm ≈ 每天 1~2 秒，
 * 对月历完全够用。32 位 tick 约 268 秒回绕一次，用无符号减法自然处理。
 */
#include "timer.h"
#include "app_time.h"

#define TICKS_PER_SEC   16000000u

static unsigned int  s_epoch;      /* 当前 Unix 秒 */
static unsigned int  s_last_tick;  /* 上次采样 */
static unsigned int  s_frac;       /* 不足一秒的余量（tick） */
static unsigned int  s_last_day;   /* 上次的"第几天"，用于判断跨天 */
static unsigned char s_force;      /* 强制重画标志 */

void app_time_init(unsigned int epoch)
{
    s_epoch     = epoch;
    s_last_tick = clock_time();
    s_frac      = 0;
    s_last_day  = epoch / 86400u;
    s_force     = 0;
}

void app_time_set(unsigned int epoch)
{
    s_epoch     = epoch;
    s_last_tick = clock_time();
    s_frac      = 0;
    s_last_day  = epoch / 86400u;
    s_force     = 1;               /* 对时后立刻重画 */
}

unsigned int app_time_now(void)
{
    return s_epoch;
}

void app_time_force_redraw(void)
{
    s_force = 1;
}

int app_time_tick(void)
{
    unsigned int now, dt, secs, day;

    now = clock_time();
    dt  = now - s_last_tick;        /* 无符号减法，自动处理 32 位回绕 */
    s_last_tick = now;

    s_frac += dt;
    secs = s_frac / TICKS_PER_SEC;
    if (secs) {
        s_frac -= secs * TICKS_PER_SEC;
        s_epoch += secs;
    }

    day = s_epoch / 86400u;
    if (day != s_last_day) {
        s_last_day = day;
        s_force = 0;
        return 1;                   /* 跨天了 */
    }
    if (s_force) {
        s_force = 0;
        return 1;                   /* 对时要求重画 */
    }
    return 0;
}
