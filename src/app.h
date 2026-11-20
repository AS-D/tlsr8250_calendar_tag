/**
 * @file    app.h
 * @brief   应用层（走时 / 刷屏决策）
 */
#ifndef __APP_H__
#define __APP_H__

/* 上电：初始化屏控制器、从 flash 读回时间（读不到就用编译时刻）*/
void app_init(void);

/* 主循环里调用（每轮都不阻塞，内部按 clock_time 判断）*/
void app_loop(void);

/* BLE 对时回调里调用：只把时间塞进变量，不做 flash/刷屏 */
void app_on_time_from_ble(unsigned int epoch);

#endif /* __APP_H__ */
