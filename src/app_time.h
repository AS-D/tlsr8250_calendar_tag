/**
 * @file    app_time.h
 * @brief   软件走时（基于 24MHz 晶振派生的系统时钟）
 *
 * 时间基准：**标准 Unix 时间戳（UTC 秒）**。
 * 显示时由 calendar.c 加固定时区偏移换算成当地时间。
 * 断电归零，靠 BLE 对时或烧录时注入的 BUILD_EPOCH 校准。
 */
#ifndef __APP_TIME_H__
#define __APP_TIME_H__

void          app_time_init(unsigned int epoch);   /* 开机：设起点，但**先不走时**（等对时）*/
void          app_time_set(unsigned int epoch);    /* BLE 对时：重设时间，**从这一刻开始走时** */
unsigned int  app_time_now(void);                  /* 当前 Unix 秒（UTC）     */
int           app_time_is_synced(void);            /* 1 = 已经被对时过，时间在走 */

/* 主循环每轮调用。
 * 没对时过：直接返回 0（时间冻结在起点，不累加、不判跨天）。
 * 对时过：按 clock_time 差值累加；返回 1 表示"跨天"或"刚对时"，调用者应重画月历。*/
int           app_time_tick(void);

/* 请求立刻重画（对时后用）*/
void          app_time_force_redraw(void);

#endif /* __APP_TIME_H__ */
