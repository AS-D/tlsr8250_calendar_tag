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
#include "app_cfg.h"
#include "app_time.h"

#define TICKS_PER_SEC   16000000u

/* 跨天必须按**当地日期**判：把 UTC 秒加上时区偏移再除以 86400。
 * 否则 UTC 0 点（北京时间早上 8 点）才跨天，而屏上按 +8 显示已经变了日期，
 * 两边就会差 8 小时不同步。时区只在 app_cfg.h 的 CFG_TZ_HOURS 定义一次。*/
#define TZ_OFFSET_SEC   ((unsigned int)(CFG_TZ_HOURS * 3600))
#define LOCAL_DAY(e)    (((e) + TZ_OFFSET_SEC) / 86400u)

static unsigned int  s_epoch;      /* 当前 Unix 秒 */
static unsigned int  s_last_tick;  /* 上次采样 */
static unsigned int  s_frac;       /* 不足一秒的余量（tick） */
static unsigned int  s_last_day;   /* 上次的"第几天"，用于判断跨天 */
static unsigned char s_synced;     /* 是否被对时过：0 = 时间冻结在起点 */

void app_time_init(unsigned int epoch)
{
    s_epoch     = epoch;
    s_last_tick = clock_time();
    s_frac      = 0;
    s_last_day  = LOCAL_DAY(epoch);
    s_synced    = 0;               /* 开机之后、对时之前，时间不走 */
}

void app_time_set(unsigned int epoch)
{
    s_epoch     = epoch;
    s_last_tick = clock_time();
    s_frac      = 0;
    s_last_day  = LOCAL_DAY(epoch);
    s_synced    = 1;               /* 从这一刻开始才走时、才判跨天 */
    /* 对时后的重画由 app.c 直接调 app_refresh() 完成，不在这里挂标志：
     * 原来那个 s_force 会被 app.c 紧接着的一次 tick 清掉，永远轮不到它生效。*/
}

unsigned int app_time_now(void)
{
    return s_epoch;
}

int app_time_tick(void)
{
    unsigned int now, dt, secs, day;

    now = clock_time();
    dt  = now - s_last_tick;        /* 无符号减法，自动处理 32 位回绕 */
    s_last_tick = now;

    /* 还没对过时：时间就冻结在起点，一秒都不加，也不判跨天。
     * （这样上电后屏上一直是同一个日期，不会自己"跨天重画"）*/
    if (!s_synced) return 0;

    s_frac += dt;
    secs = s_frac / TICKS_PER_SEC;
    if (secs) {
        s_frac -= secs * TICKS_PER_SEC;
        s_epoch += secs;
    }

    day = LOCAL_DAY(s_epoch);
    if (day != s_last_day) {
        s_last_day = day;
        return 1;                   /* 跨天了 */
    }
    return 0;
}
