/**
 * @file    batt.h
 * @brief   电池电压读取
 *
 * 2026-11-19：这块板子上内部 VBAT 通道读不到（恒 0），改用 SDK 对 825x 推荐的
 * "GPIO 驱动高电平再采样"法（电压 = VDD），实测脚借 PB7（EPD 的 SDA，空闲为高）。
 */
#ifndef __BATT_H__
#define __BATT_H__

/* 返回电池电压（mV）。0 表示"读不到"，界面显示 "--.-V" */
unsigned int batt_mv(void);

/* 诊断用：一次采两种方法，返回有效电压；pin_out=引脚法 VDD，ch_out=内部通道原始 mV */
unsigned int batt_measure(unsigned int *pin_out, unsigned int *ch_out);

/* 单独调用：batt_pin_mv() 引脚法 VDD；batt_vbat_ch_raw() 内部通道原始 mV（未乘 1/3） */
unsigned int batt_pin_mv(void);
unsigned int batt_vbat_ch_raw(void);

#endif /* __BATT_H__ */
