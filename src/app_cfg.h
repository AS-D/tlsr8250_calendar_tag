/**
 * @file    app_cfg.h
 * @brief   本工程**唯一**的编译期配置（替代 vendor/acl_peripheral_demo/app_config.h）
 *
 * 通过编译参数 `-include src/app_cfg.h` 强制预包含：
 * 这样 SDK 的每个 .c 也在编译前先拿到这些宏，不用改 SDK 源码，
 * 也不用再定义 __PROJECT_ACL_PER_DEMO__ 让 tl_common.h 去拉 demo 的配置。
 *
 * 想让固件换个行为，只改本文件里的宏，不需要动 SDK、也不需要动 .c。
 */
#ifndef __APP_CFG_H__
#define __APP_CFG_H__

/* ── 芯片与主频 ────────────────────────────────────────────────────────── */
#ifndef CHIP_TYPE
#define CHIP_TYPE               CHIP_TYPE_825x
#endif
#define CLOCK_SYS_CLOCK_HZ      32000000        /* 32MHz 晶振 */

/* ── BLE 连接规模与缓冲（只做从机，保留 1 个主机槽给协议栈用）────────── */
#define ACL_CENTRAL_MAX_NUM     1
#define ACL_PERIPHR_MAX_NUM     1

#define ACL_CONN_MAX_RX_OCTETS  27
#define ACL_MASTER_MAX_TX_OCTETS 27
#define ACL_SLAVE_MAX_TX_OCTETS 27
#define ACL_RX_FIFO_NUM         8               /* 必须是 2^n，且 >= 4 */
#define ACL_SLAVE_TX_FIFO_NUM   8               /* 必须是 2^n，且 >= 8 */
#define ATT_MTU_SLAVE_RX_MAX_SIZE  23
#define ATT_MTU_MASTER_RX_MAX_SIZE 23

/* ── 功率管理 ─────────────────────────────────────────────────────────── */
#define BLE_APP_PM_ENABLE               1       /* 允许 suspend 省电 */
#define PM_DEEPSLEEP_RETENTION_ENABLE   0       /* 不做 deep retention（我们不需要）*/

/* ── 本工程不需要的功能，全部关掉（省 flash、省电、少依赖）─────────────── */
#define BLE_OTA_SERVER_ENABLE           0
#define BLE_OTA_CLIENT_ENABLE           0
#define APP_BATT_CHECK_ENABLE           0
#define APP_FLASH_PROTECTION_ENABLE     0
#define UI_LED_ENABLE                   0
#define UI_KEYBOARD_ENABLE              0
#define UI_BUTTON_ENABLE                0

/* ── 调试输出全关 ─────────────────────────────────────────────────────── */
#define DEBUG_GPIO_ENABLE               0
#define UART_PRINT_DEBUG_ENABLE         0
#define UART_LOW_POWER_DEBUG_EN         0
#define APP_LOG_EN                      0
#define APP_SMP_LOG_EN                  0
#define APP_KEY_LOG_EN                  0
#define APP_CONTR_EVENT_LOG_EN          0
#define APP_HOST_EVENT_LOG_EN           0
#define APP_OTA_LOG_EN                  0
#define APP_FLASH_INIT_LOG_EN           0
#define APP_FLASH_PROT_LOG_EN           0
#define APP_BATT_CHECK_LOG_EN           0

/* ── 应用行为开关（省电就改这几个）─────────────────────────────────────── */
/* 上电是否立刻刷一次屏。
 * 0 = 上电只把屏控制器初始化好，一个像素都不刷（屏上是断电保持的旧画面），
 *     等手机对时后再刷 —— 最省电，也避免每次换电池都全刷 20 秒。
 * 1 = 老行为：上电画当月月历并刷屏。 */
#define CFG_BOOT_REFRESH        0

/* 调试灯（PB4 红 / PB5 绿 / PC4 蓝，共阳低电平点亮）。
 * 0 = 全部熄灭且不闪，省电；1 = 开机用闪灯报电压和刷屏结果。 */
#define CFG_LED_DEBUG           0

/* 跨天是否自动刷一次屏。
 * 1 = 每天 0 点后把当天月历刷上去（"今天"的红块才会跟着日期走，代价是一次三色全刷 ~20 秒）
 * 0 = 不刷，只在 BLE 对时后刷（更省电，但高亮会停在最后一次刷屏那天）*/
#define CFG_DAY_REFRESH         1

/* 刷屏时是否读一次电池电压显示在屏上（读一次约 1ms，几乎不耗电）*/
#define CFG_BATT_SHOW           1
/* 广播名（手机/网关按这个名字搜）。实际字符列表在 ble_app.c 的 ADV_NAME_CHARS，
 * 这个宏只给 README / PC 端脚本做对照，改名字要两处一起改。 */
#define CFG_ADV_NAME            "calendar_tag"

/* 广播间隔（单位 0.625ms，30 = 18.75ms）。越大越省电，但连接建立越慢。
 * 上线可改 160（100ms）或 320（200ms）。 */
#define CFG_ADV_INTERVAL        60

/* SDK 公共默认配置（boards、DBG_CHN 宏、PM 默认值等）*/
#include "default_config.h"

#endif /* __APP_CFG_H__ */
