/**
 * @file    main.c
 * @brief   固件入口（替换 SDK 的 vendor/acl_peripheral_demo/main.c + vendor/common/main.c）
 *
 * 只做三件事：开机初始化 -> 开中断 -> 死循环里跑 BLE 协议栈和应用逻辑。
 * 刷屏、走时、落盘都在 app.c；BLE 在 ble_app.c；显示驱动在 epd_ssd1680.c。
 */
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "app_cfg.h"
#include "ble_app.h"
#include "app.h"

/* ─────────────────────────────────────────────────────────────────────── */
/* 中断入口：所有 BLE/RF/定时器中断都从这里进                              */
/* ─────────────────────────────────────────────────────────────────────── */
_attribute_ram_code_ void irq_handler(void)
{
    blc_sdk_irq_handler();
}

/* ─────────────────────────────────────────────────────────────────────── */
/* 三色灯：这块板是共阳，低电平点亮。不闪灯时全部输出高电平（灭），防止漏电 */
/* ─────────────────────────────────────────────────────────────────────── */
static void led_all_off(void)
{
    gpio_set_func(GPIO_PB4, AS_GPIO);
    gpio_set_output_en(GPIO_PB4, 1);
    gpio_write(GPIO_PB4, 1);

    gpio_set_func(GPIO_PB5, AS_GPIO);
    gpio_set_output_en(GPIO_PB5, 1);
    gpio_write(GPIO_PB5, 1);

    gpio_set_func(GPIO_PC4, AS_GPIO);
    gpio_set_output_en(GPIO_PC4, 1);
    gpio_write(GPIO_PC4, 1);
}

/* ─────────────────────────────────────────────────────────────────────── */
_attribute_no_inline_ static void user_init(void)
{
    led_all_off();
    wd_stop();

    /* 读 flash 容量、配置自定义扇区地址，然后加载 MAC/校准参数。
     * 顺序不能换：后者依赖前者算出来的扇区地址。*/
    blc_readFlashSize_autoConfigCustomFlashSector();
    blc_app_loadCustomizedParameters_normal();

    /* BLE 栈 + 广播（先开广播，后面刷屏时手机也能连上）*/
    ble_app_init();

    /* 屏 + 时间起点（默认不刷屏，见 app_cfg.h 的 CFG_BOOT_REFRESH）*/
    app_init();
}

int main(void)
{
#if (BLE_APP_PM_ENABLE)
    blc_pm_select_internal_32k_crystal();
#endif

    cpu_wakeup_init();
    clock_init(SYS_CLK_TYPE);
    rf_drv_ble_init();
    gpio_init(1);

    user_init();

    irq_enable();

    while (1) {
        blc_sdk_main_loop();        /* BLE 协议栈心跳，不阻塞 */
        app_loop();                 /* 走时 / 对时处理 / 刷屏决策 */
    }
    return 0;
}
