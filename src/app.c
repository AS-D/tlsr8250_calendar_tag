/**
 * @file    app.c
 * @brief   应用逻辑：开机初始化、走时、何时刷屏
 *
 * 时间线：
 *   上电 → 设起点 BUILD_EPOCH，**但时间冻结不动**（app_time_tick 直接返回 0）
 *   对时 → 从手机写进来的值开始每秒累加，从此才判跨天
 *   **不碰 flash**，断电重启回到编译时刻
 *
 * 省电策略在 app_cfg.h 里开关（CFG_BOOT_REFRESH / CFG_LED_DEBUG / CFG_DAY_REFRESH）：
 * 默认：上电不刷屏、不闪灯；对时后刷一次；之后每天跨天再刷一次。
 */
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"   /* blc_sdk_main_loop：注册成刷屏等待期的空闲钩子 */
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

#if CFG_LED_HEARTBEAT
static unsigned int  s_hb_last;
static unsigned char s_hb_state;

/* 诊断心跳：PB5 绿灯每秒翻转一次。
 * 计时源用 clock_time()，它已经被主循环里的 pm_tim_recover 保持连续了，
 * 所以睡眠期间的时间也会被算进去，看门狗式地反映主循环是否还活着。*/
static void led_heartbeat(void)
{
    unsigned int t = clock_time();

    if ((unsigned int)(t - s_hb_last) >= 16000000u) {   /* 16MHz -> 1 秒 */
        s_hb_last += 16000000u;
        s_hb_state = (unsigned char)(!s_hb_state);
        gpio_write(GPIO_PB5, s_hb_state ? 0 : 1);       /* 共阳：低电平点亮 */
    }
}
#endif

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
    EPD_Init();
}

/* 重画 + 刷屏（阻塞十几到二十几秒）*/
static void app_refresh(void)
{
    epd_ensure_init();
    calendar_draw(app_time_now());
    (void)display_present();   /* 内部已等 BUSY 释放，可以立刻发深睡命令 */

    /* 刷完立刻让屏控进 deep sleep，切断屏侧 DC/DC，否则待机电流一直下不去。
     * 必须同时清标志：下次刷屏要重新 EPD_Init() 把屏从深睡里唤醒，
     * 否则屏收不到命令，表现是"刷过一次之后再刷白屏"。*/
    EPD_DeInit();
    s_epd_inited = 0;
}

void app_init(void)
{
    /* 只把屏的引脚设成确定电平（避免浮空漏电），不发复位、不发初始化、更不刷屏。
     * 真正要刷的时候在 app_refresh() 里才 EPD_Init()。*/
    EPD_InitGpio();

    /* 屏 VDD 是硬件常供的，控制器上电后停在正常模式、空载也吃几十 µA。
     * 开机不刷屏时没人给它发 0x10，这里补一次复位 + 深睡。*/
    EPD_Reset();
    EPD_DeInit();

    /* 把链路层主循环注册成屏驱动"等 BUSY"期间的空闲钩子：刷屏那 20 秒
     * 每 50ms 醒一次就推一下 BLE 栈，广播照常按 200ms 发出去。*/
    EPD_SetIdleHook(blc_sdk_main_loop);

    /* 时间起点就是编译时刻；不读 flash、不写 flash */
    app_time_init(BUILD_EPOCH);

#if CFG_BOOT_REFRESH
    app_refresh();
#endif
}

void app_loop(void)
{
    unsigned int pend = s_pending_epoch;

#if CFG_LED_HEARTBEAT
    led_heartbeat();
#endif

    /* ① 刚被对时：设时间（从此开始走时）-> 刷屏 */
    if (pend) {
        s_pending_epoch = 0;
        app_time_set(pend);
        app_refresh();
        return;
    }

    /* ② 只有对时过之后，这里才可能返回 1（跨天）；没对时过永远不刷屏 */
    if (app_time_tick()) {
#if CFG_DAY_REFRESH
        app_refresh();
#endif
    }
}
