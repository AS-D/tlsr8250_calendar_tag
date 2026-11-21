/**
 * @file    lunar.c
 * @brief   公历 -> 农历换算
 *
 * 算法：以 1900-01-31（农历庚子年正月初一）为基准，先按"农历年长度"扣减，
 * 再按"农历月长度"扣减（闰月插在对应月份之后）。
 * 置闰表在 lunar_table.h（由 tools/gen_lunar.py 生成，已用 12 个春节 + 4 个中秋校验）。
 */
#include "lunar.h"
#include "lunar_table.h"

/* 公历 (y,m,d) -> 1970-01-01 起的天数（Howard Hinnant）。
 * 对外导出：calendar.c 算"第几周"也用它，避免同一个算法抄两份。*/
long lunar_days_from_civil(int y, int m, int d)
{
    long era;
    unsigned int yoe, doy, doe;

    y -= (m <= 2);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned int)(y - era * 400);
    doy = (153u * (unsigned int)((m > 2) ? (m - 3) : (m + 9)) + 2) / 5 + (unsigned int)d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static int leap_month(int y)
{
    return (int)(lunar_info[y - LUNAR_BASE_YEAR] & 0xF);
}

static int leap_days(int y)
{
    if (leap_month(y) == 0) return 0;
    return (lunar_info[y - LUNAR_BASE_YEAR] & 0x10000) ? 30 : 29;
}

static int month_days(int y, int m)
{
    return (lunar_info[y - LUNAR_BASE_YEAR] & (0x10000u >> m)) ? 30 : 29;
}

static int year_days(int y)
{
    unsigned int v = lunar_info[y - LUNAR_BASE_YEAR];
    int s = 348;                       /* 12 个月 × 29 天 */
    int i;
    for (i = 4; i <= 15; i++)          /* bits 4..15：大月各 +1 天 */
        if (v & (1u << i)) s++;
    return s + leap_days(y);
}

void lunar_from_solar(int y, int m, int d, lunar_date_t *out)
{
    long offset;
    int ly, lmleap, i;

    out->year = out->month = out->day = 0;
    out->is_leap = 0;

    if (y < LUNAR_BASE_YEAR || y > LUNAR_END_YEAR) return;

    /* 1900-01-31 是农历正月初一 */
    offset = lunar_days_from_civil(y, m, d) - lunar_days_from_civil(1900, 1, 31);

    ly = LUNAR_BASE_YEAR;
    while (ly < LUNAR_END_YEAR && offset >= year_days(ly)) {
        offset -= year_days(ly);
        ly++;
    }

    lmleap = leap_month(ly);
    for (i = 1; i <= 12; i++) {
        int md = month_days(ly, i);

        if (offset < md) {
            out->year = ly; out->month = i; out->day = (int)offset + 1;
            out->is_leap = 0;
            return;
        }
        offset -= md;

        if (lmleap == i) {
            int ld = leap_days(ly);
            if (offset < ld) {
                out->year = ly; out->month = i; out->day = (int)offset + 1;
                out->is_leap = 1;
                return;
            }
            offset -= ld;
        }
    }
}
