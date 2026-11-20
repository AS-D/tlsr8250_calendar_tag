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

void          app_time_init(unsigned int epoch);   /* 开机：设置起点并开始计时 */
void          app_time_set(unsigned int epoch);    /* BLE 对时：重设时间      */
unsigned int  app_time_now(void);                  /* 当前 Unix 秒（UTC）     */

/* 由软定时器每秒调用一次。
 * 返回 1 表示"日期变了"，调用者应重画月历。*/
int           app_time_tick(void);

/* 请求立刻重画（对时后用）*/
void          app_time_force_redraw(void);

#endif /* __APP_TIME_H__ */
