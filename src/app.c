/**
 * @file    app.c
 * @brief   应用逻辑：开机初始化、走时、何时刷屏
 *
 * 时间只在 RAM 里：起点是 BUILD_EPOCH（编译时刻），手机对时后从新值开始累加。
 * **不碰 flash** —— 断电重启就回到编译时刻，需要准就再对一次时。
 *
 * 省电策略在 app_cfg.h 里开关（CFG_BOOT_REFRESH / CFG_LED_DEBUG / CFG_DAY_REFRESH）：
 * 默认：上电不刷屏、不闪灯；对时后刷一次；跨天再刷一次（保证"今天"跟着走）。
 */
#include "tl_common.h"
#include "drivers.h"
#include "epd_ssd1680.h"
#include "calendar.h"
#include "app_time.h"
#include "display.h"
#include "app_cfg.h"
#include "app.h"

/* 编译时注入的 Unix 时间；没注入时退回 2026-01-01 00:00:00 UTC */
#ifndef BUILD_EPOCH
#define BUILD_EPOCH     1767225600u
#endif

/* BLE 回调（中断上下文）只写它，主循环负责刷屏 */
static volatile unsigned int s_pending_epoch = 0;

/* 屏是否已经初始化过：上电不初始化（省电），第一次真要刷屏时才初始化 */
static unsigned char s_epd_inited = 0;

/* ─────────────────────────────────────────────────────────────────────── */
void app_on_time_from_ble(unsigned int epoch)
{
    if (epoch < 1600000000u) return;    /* 明显非法的时间，直接丢掉 */
    s_pending_epoch = epoch;
}

static void epd_ensure_init(void)
{
    if (s_epd_inited) return;
    s_epd_inited = 1;
    EPD_SetCandidate(0);
    EPD_Init();
}

/* 重画 + 刷屏（阻塞十几到二十几秒）*/
static void app_refresh(void)
{
    epd_ensure_init();
    calendar_draw(app_time_now());
    (void)display_present();
}

void app_init(void)
{
    /* 只把屏的引脚设成确定电平（避免浮空漏电），不发复位、不发初始化、更不刷屏。
     * 真正要刷的时候在 app_refresh() 里才 EPD_Init()。*/
    EPD_SetCandidate(0);
    EPD_InitGpio();

    /* 时间起点就是编译时刻；不读 flash、不写 flash */
    app_time_init(BUILD_EPOCH);

#if CFG_BOOT_REFRESH
    app_refresh();
#endif
}

void app_loop(void)
{
    unsigned int pend = s_pending_epoch;

    /* ① 刚被对时：设时间 -> 刷屏 */
    if (pend) {
        s_pending_epoch = 0;
        app_time_set(pend);
        (void)app_time_tick();      /* 清掉"刚对时要重画"标志，避免重复刷 */
        app_refresh();
        return;
    }

    /* ② 走时：跨天（或手动要求重画）时把当天的月历刷上去 */
    if (app_time_tick()) {
#if CFG_DAY_REFRESH
        app_refresh();
#endif
    }
}
