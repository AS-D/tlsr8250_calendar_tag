/**
 * @file    font_u8g2.h
 * @brief   u8g2 字体数据声明（自动生成，见 tools/extract_u8g2_fonts.py）
 */
#ifndef __FONT_U8G2_H__
#define __FONT_U8G2_H__

#include <stdint.h>

/* 文泉驿点阵宋 Bold 12px：ASCII + 农历日历汉字子集，228 个字形 */
/* wqy9     文泉驿点阵宋 Bold 12px（正文/表头/农历），4315 字节 */
extern const uint8_t u8g2_font_wqy9_t_lunar[4315];
#define FONT_WQY9_SIZE 4315

/* wqy12    文泉驿点阵宋 Bold 16px（大标题备用），4830 字节 */
extern const uint8_t u8g2_font_wqy12_t_lunar[4830];
#define FONT_WQY12_SIZE 4830

/* helvB14  Helvetica Bold 14px（星期/小数字），287 字节 */
extern const uint8_t u8g2_font_helvB14_tn[287];
#define FONT_HELVB14_SIZE 287

/* helvB18  Helvetica Bold 18px（年份/日期大数字），389 字节 */
extern const uint8_t u8g2_font_helvB18_tn[389];
#define FONT_HELVB18_SIZE 389

#endif /* __FONT_U8G2_H__ */
