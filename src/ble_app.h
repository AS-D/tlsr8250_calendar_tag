/**
 * @file    ble_app.h
 * @brief   本工程自己的 BLE 从机（不再依赖 vendor/acl_peripheral_demo）
 */
#ifndef __BLE_APP_H__
#define __BLE_APP_H__

/* 链路层 + 主机栈初始化、注册 ATT 表、打开广播。
 * 必须在 main() 的 user_init 里调用一次。 */
void ble_app_init(void);

/* 广播开关（对时完可以关掉省电，目前常开）*/
void ble_app_adv(int on);

#endif /* __BLE_APP_H__ */
