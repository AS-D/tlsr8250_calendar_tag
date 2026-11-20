/**
 * @file    lunar.h
 * @brief   公历 -> 农历换算（1900-2100，置闰表见 lunar_table.h）
 */
#ifndef __LUNAR_H__
#define __LUNAR_H__

typedef struct {
    int year;      /* 农历年（可能比公历小 1） */
    int month;     /* 1..12 */
    int day;       /* 1..30 */
    int is_leap;   /* 1 = 闰月 */
} lunar_date_t;

/* 公历 y-m-d -> 农历；超出 1900-2100 时返回全 0 */
void lunar_from_solar(int y, int m, int d, lunar_date_t *out);

#endif /* __LUNAR_H__ */
