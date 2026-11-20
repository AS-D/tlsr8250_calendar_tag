/**
 * @file    calendar.h
 * @brief   月历绘制（直接画到 gfx 显存里，竖屏 122x250）
 *
 * 版式参照盒马原厂那版：7 列日历 + 年月周信息。
 * 原厂是横向 250x122；这里是竖屏，所以是"7 列 × 6 行"竖着排，
 * 标签正常竖着放就能看（不需要横过来）。
 */
#ifndef __CALENDAR_H__
#define __CALENDAR_H__

/* 时区偏移（秒）。时间基准是 UTC，显示时加这个。
 * 中国 +8 小时 = 28800。*/
#define CAL_TZ_OFFSET   28800u

/* 把 epoch（UTC 秒）换算成当地时间后画月历 */
void calendar_draw(unsigned int epoch);

#endif /* __CALENDAR_H__ */
