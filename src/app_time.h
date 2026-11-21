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

/* 主循环每轮调用。
 * 没对时过：直接返回 0（时间冻结在起点，不累加、不判跨天）。
 * 对时过：按 clock_time 差值累加；返回 1 表示"跨天"，调用者应重画月历。
 * （对时后的首次重画由调用者直接刷屏完成，tick 不负责这件事）*/
int           app_time_tick(void);

#endif /* __APP_TIME_H__ */
