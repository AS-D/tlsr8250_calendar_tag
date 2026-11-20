/**
 * @file    calendar.c
 * @brief   月历绘制 —— 横向 250x122 版式（对标盒马原厂）
 *
 * 面板物理上是竖屏 122x250，而版式是横的，所以这里用**横向坐标**绘制，
 * 每个像素经 lpx() 旋转 90° 写进竖屏显存。
 *
 * 横向版式（250x122）：
 *   3..171     日历网格：7 列 × 24px，表头 16px 高，最多 6 行日期每行 17px
 *   173        竖向分隔线
 *   175..247   右侧信息栏（全部居中）：
 *               2026（红，18px） / 丙午马年（黑 12px）
 *               11月（红，18px + 16px 月） / 十月十一（黑 12px）
 *               周47
 *
 * 字体：全部走 u8g2 字体（数据在 font_u8g2.c，移植自 ink-tag 工程）
 *   wqy9  (12px) 文泉驿点阵宋 Bold —— 表头、干支、农历、周
 *   wqy12 (16px) 同上              —— 表头、月字
 *   helvB14       Helvetica Bold   —— 日期、周数数字
 *   helvB18       Helvetica Bold   —— 年份、公历月数字
 * 用比例字宽后，居中要按实际字宽算（text_w），不能再按固定字宽累加。
 */
#include "gfx.h"
#include "u8g2_font.h"
#include "font_u8g2.h"
#include "calendar.h"
#include "lunar.h"

#define CAL_W     250
#define CAL_H     122

#define MARGIN    3                     /* 四周留白 */
#define CELL_W    24
#define GRID_X    MARGIN
#define GRID_TOP  3
#define HEAD_H    16                    /* 表头色块高度 */
#define GRID_Y    19                    /* 日期第一行顶端 */
#define ROW_H     17                    /* 6 行 × 17 = 102，19..121 */

#define SEP_X     173                  /* 竖向分隔线 */
#define PANEL_L   175
#define PANEL_W   (CAL_W - MARGIN - PANEL_L)   /* 68 */
#define LINE_W    (CAL_W - MARGIN - SEP_X)     /* 横线从竖线画到右边距 */

/* 字号别名 */
#define F_HEAD    u8g2_font_wqy12_t_lunar   /* 表头、月字 */
#define F_SMALL   u8g2_font_wqy9_t_lunar    /* 小字（12px） */
#define F_DIG     u8g2_font_helvB14_tn      /* 日期、小数字（14px） */
#define F_BIG     u8g2_font_helvB18_tn      /* 年份、月数字（18px） */

/* 各栏基线（y 坐标，字体按基线对齐） */
#define BASE_HEAD   17

/* 右栏三段：年份+干支 / 公历月+农历 / 周数，两根横线分开。
 * 不画电池：这块板是 2×CR2450 并联，放电平台极平（长期 3.0V），读数没信息量。 */
#define BASE_YEAR   28
#define BASE_GANZHI 42
#define LINE1_Y     47
#define BASE_MONTH  72
#define BASE_LUNAR  86
#define LINE2_Y     92
#define BASE_WEEK   110

/* ------------------------------------------------------------------------- */
/* 横向坐标 -> 竖屏显存（旋转 90°）                                           */
/* ------------------------------------------------------------------------- */
#define CAL_ROT   0

static void lpx(int lx, int ly, int color)
{
#if CAL_ROT == 0
    gfx_px(121 - ly, lx, color);
#else
    gfx_px(ly, 249 - lx, color);
#endif
}

static void lfill(int x, int y, int w, int h, int color)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            lpx(x + i, y + j, color);
}

/* ------------------------------------------------------------------------- */
/* u8g2 字体绘制                                                              */
/* ------------------------------------------------------------------------- */
static u8g2_font_t s_font;
static int s_bold;                  /* 1 = 右移 1px 叠印一次，给 16px 字加粗 */

/* u8g2 唯一的绘制出口：一段水平像素串，落进来直接走 lpx() */
static void epd_line_cb(u8g2_font_t *u8g2, int16_t x, int16_t y,
                        int16_t len, uint8_t dir, uint16_t color)
{
    int i;
    (void)u8g2;
    if (dir != 0 || len <= 0) return;
    for (i = 0; i < len; i++) lpx(x + i, y, (int)color);
}

static void font_set(const uint8_t *font)
{
    s_font.draw_hv_line = epd_line_cb;
    u8g2_SetFont(&s_font, font);
    u8g2_SetFontMode(&s_font, 1);           /* 1 = 透明，只画前景 */
    u8g2_SetFontDirection(&s_font, 0);      /* 0 = 横排 */
}

/* 取下一个 UTF-8 码点（u8g2 的 DrawStr 只认单字节，中文得自己解） */
static uint16_t utf8_next(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    uint16_t e;

    if (*s == 0) return 0;
    if ((*s & 0x80) == 0) {
        e = *s;
        s += 1;
    } else if ((*s & 0xE0) == 0xC0) {
        e = (uint16_t)(((*s & 0x1F) << 6) | (s[1] & 0x3F));
        s += 2;
    } else {
        e = (uint16_t)(((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F));
        s += 3;
    }
    *ps = (const char *)s;
    return e;
}

/* 当前字体下这段文本的像素宽 */
static int text_w(const char *s)
{
    const char *p = s;
    uint16_t e;
    int w = 0;

    while ((e = utf8_next(&p)) != 0) {
        int8_t d = u8g2_GetGlyphWidth(&s_font, e);
        if (d > 0) w += d;
        else w += s_font.font_info.max_char_width;   /* 缺字给个占位宽度 */
    }
    return w;
}

/* 左对齐画一段文本，返回占用宽度 */
static int text_at(const char *s, int x, int baseline, int color)
{
    const char *p = s;
    uint16_t e;
    int pen = x;

    u8g2_SetForegroundColor(&s_font, (uint16_t)color);
    while ((e = utf8_next(&p)) != 0) {
        int16_t d = u8g2_DrawGlyph(&s_font, (int16_t)pen, (int16_t)baseline, e);
        if (s_bold)
            u8g2_DrawGlyph(&s_font, (int16_t)(pen + 1), (int16_t)baseline, e);
        pen += d;
    }
    return pen - x;
}

/* 以 cx 为水平中心画一段文本 */
static void text_center(const char *s, int cx, int baseline, int color)
{
    text_at(s, cx - text_w(s) / 2, baseline, color);
}


/* ---- 极简字符串工具（SDK 的 libc 里没有 strcat/strcpy/strlen） ---- */
static int s_len(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void s_copy(char *d, const char *s)
{
    while ((*d++ = *s++) != 0) { }
}

static void s_cat(char *d, const char *s)
{
    s_copy(d + s_len(d), s);
}

/* ------------------------------------------------------------------------- */
/* 数字 / 农历文字                                                            */
/* ------------------------------------------------------------------------- */

/* 十进制整数转字符串，digits=0 表示不补零 */
static void fmt_int(char *buf, int v, int digits)
{
    int n = 0, i, d[6];

    if (v < 0) v = 0;
    do {
        d[n++] = v % 10;
        v /= 10;
    } while (v && n < 6);
    while (digits > n && n < 6) d[n++] = 0;        /* 补前导零 */
    for (i = 0; i < n; i++) buf[i] = (char)('0' + d[n - 1 - i]);
    buf[n] = 0;
}

static const char *const LUNAR_MONTH[13] = {
    "", "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊"
};
static const char *const CN_NUM[10] = {
    "一", "二", "三", "四", "五", "六", "七", "八", "九", "十"
};

/* 农历日 -> “初一”“十一”“廿九”“三十” */
static void lunar_day_str(char *buf, int d)
{
    if (d <= 10) {
        s_copy(buf, "初");
        s_cat(buf, CN_NUM[d - 1]);
    } else if (d < 20) {
        s_copy(buf, "十");
        s_cat(buf, CN_NUM[d - 11]);
    } else if (d == 20) {
        s_copy(buf, "二十");
    } else if (d < 30) {
        s_copy(buf, "廿");
        s_cat(buf, CN_NUM[d - 21]);
    } else {
        s_copy(buf, "三十");
    }
}

/* 干支 / 生肖（按农历年推：甲子起于公元 4 年） */
static const char *const STEM[10]   = {"甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"};
static const char *const BRANCH[12] = {"子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"};
static const char *const ZODIAC[12] = {"鼠", "牛", "虎", "兔", "龙", "蛇", "马", "羊", "猴", "鸡", "狗", "猪"};

/* ------------------------------------------------------------------------- */
/* 日期换算                                                                   */
/* ------------------------------------------------------------------------- */
static void civil_from_days(long z, int *py, int *pm, int *pd)
{
    long era;
    unsigned int doe, yoe, doy, mp, dd, mm;
    int yy;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned int)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    yy  = (int)yoe + (int)era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp  = (5 * doy + 2) / 153;
    dd  = doy - (153 * mp + 2) / 5 + 1;
    mm  = (mp < 10) ? (mp + 3) : (mp - 9);

    *py = yy + (mm <= 2);
    *pm = (int)mm;
    *pd = (int)dd;
}

static long days_from_civil(int y, int m, int d)
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

static int days_in_month(int y, int m)
{
    static const unsigned char dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2) {
        int leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
        return leap ? 29 : 28;
    }
    return dim[m - 1];
}

/* ------------------------------------------------------------------------- */
/* 信息栏各条目                                                               */
/* ------------------------------------------------------------------------- */


/* 公历月：大号数字 + 16px 月字，整体居中 */
static void put_month(int m)
{
    char num[4];
    int nw, yw, x;

    fmt_int(num, m, 0);

    font_set(F_BIG);
    nw = text_w(num);
    font_set(F_HEAD);
    yw = text_w("月");

    x = PANEL_L + (PANEL_W - (nw + yw)) / 2;
    font_set(F_BIG);
    text_at(num, x, BASE_MONTH, GFX_RED);
    font_set(F_HEAD);
    s_bold = 1;
    text_at("月", x + nw, BASE_MONTH, GFX_RED);
    s_bold = 0;
}

/* 农历：闰 + 月名 + 月 + 日 */
static void put_lunar(const lunar_date_t *ld)
{
    char buf[32];

    buf[0] = 0;
    if (ld->is_leap) s_cat(buf, "闰");
    s_cat(buf, LUNAR_MONTH[ld->month]);
    s_cat(buf, "月");
    lunar_day_str(buf + s_len(buf), ld->day);

    font_set(F_SMALL);
    text_center(buf, PANEL_L + PANEL_W / 2, BASE_LUNAR, GFX_BLACK);
}

/* 本年第几周：周 + 大号数字 */
static void put_week(int wk)
{
    char num[4];
    int zw, nw, x;

    font_set(F_SMALL);
    zw = text_w("周");
    fmt_int(num, wk, 0);
    font_set(F_DIG);
    nw = text_w(num);

    x = PANEL_L + (PANEL_W - (zw + nw)) / 2;
    font_set(F_SMALL);
    text_at("周", x, BASE_WEEK, GFX_BLACK);
    font_set(F_DIG);
    text_at(num, x + zw, BASE_WEEK, GFX_BLACK);
}

/* ------------------------------------------------------------------------- */
/* 月历                                                                       */
/* ------------------------------------------------------------------------- */
void calendar_draw(unsigned int epoch)
{
    unsigned int local = epoch + CAL_TZ_OFFSET;
    long days = (long)(local / 86400u);
    int y, m, d, lead, ndays, day, idx, r, c, rows, y_off;
    int i;
    char num[6];
    lunar_date_t ld;

    civil_from_days(days, &y, &m, &d);

    gfx_clear(GFX_WHITE);

    /* ---------------- 表头：日/六 红底，其余黑底，白字 ---------------- */
    font_set(F_HEAD);
    for (i = 0; i < 7; i++) {
        static const char *const hdr[7] = {"日", "一", "二", "三", "四", "五", "六"};
        int bg = (i == 0 || i == 6) ? GFX_RED : GFX_BLACK;
        int cx = GRID_X + i * CELL_W;
        lfill(cx, GRID_TOP, CELL_W, HEAD_H, bg);
        text_center(hdr[i], cx + CELL_W / 2, BASE_HEAD, GFX_WHITE);
    }

    /* ---------------- 日期网格 ---------------- */
    ndays = days_in_month(y, m);
    lead = (int)(((days - (d - 1)) + 4) % 7);      /* 1 号是周几 */

    /* 本月实际占几行（4~6）。不足 6 行时把日期块垂直居中，
     * 否则 5 行的月份底下会空一整行，看着偏上。 */
    rows = (lead + ndays + 6) / 7;
    y_off = (6 - rows) * ROW_H / 2;

    font_set(F_DIG);
    for (day = 1; day <= ndays; day++) {
        int cx, cy, color;

        idx = lead + day - 1;
        r = idx / 7;
        c = idx % 7;
        if (r > 5) break;

        cx = GRID_X + c * CELL_W;
        cy = GRID_Y + y_off + r * ROW_H;
        color = (c == 0 || c == 6) ? GFX_RED : GFX_BLACK;

        if (day == d) {                            /* 今天：红块白字 */
            lfill(cx, cy, CELL_W, ROW_H, GFX_RED);
            color = GFX_WHITE;
        }
        fmt_int(num, day, 0);
        text_center(num, cx + CELL_W / 2, cy + 15, color);
    }

    /* ---------------- 右侧信息栏（全居中） ---------------- */
    lfill(SEP_X, MARGIN, 1, CAL_H - 2 * MARGIN, GFX_BLACK);   /* 竖向分隔线 */

    /* 年份（红，Helvetica Bold 18） */
    fmt_int(num, y, 4);
    font_set(F_BIG);
    text_center(num, PANEL_L + PANEL_W / 2, BASE_YEAR, GFX_RED);

    /* 干支生肖（黑小字，居中） */
    lunar_from_solar(y, m, d, &ld);
    {
        char gz[16];
        int ly = (ld.year > 0) ? ld.year : y;
        int k = ((ly - 4) % 60 + 60) % 60;

        gz[0] = 0;
        s_cat(gz, STEM[k % 10]);
        s_cat(gz, BRANCH[k % 12]);
        s_cat(gz, ZODIAC[k % 12]);
        s_cat(gz, "年");
        font_set(F_SMALL);
        text_center(gz, PANEL_L + PANEL_W / 2, BASE_GANZHI, GFX_BLACK);
    }
    lfill(SEP_X, LINE1_Y, LINE_W, 1, GFX_BLACK);

    /* 公历月（红） */
    put_month(m);

    /* 农历（黑色小字，居中） */
    if (ld.month) put_lunar(&ld);
    lfill(SEP_X, LINE2_Y, LINE_W, 1, GFX_BLACK);

    /* 本年第几周 */
    {
        long jan1 = days_from_civil(y, 1, 1);
        int wk = (int)((days - jan1) / 7) + 1;
        put_week(wk);
    }
}
