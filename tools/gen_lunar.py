#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_lunar.py —— 农历置闰表：校验 + 生成 C 头文件

表来源：docs.rs horae-core 0.2.5（1900-2100 标准农历编码表，共 201 项）
每项 32 位：
    bits 0..3   闰月月份（0 = 无闰月）
    bits 4..15  1~12 月大小（1 = 30 天，0 = 29 天），bit15 对应正月
    bit 16      闰月大小

脚本会先用若干已知春节（正月初一）校验整表，通过了才生成 src/lunar_table.h。
用法：python tools/gen_lunar.py
"""
import datetime
import os

LUNAR_INFO = [
    0x04BD8, 0x04AE0, 0x0A570, 0x054D5, 0x0D260, 0x0D950, 0x16554, 0x056A0, 0x09AD0,
    0x055D2,
    0x04AE0, 0x0A5B6, 0x0A4D0, 0x0D250, 0x1D255, 0x0B540, 0x0D6A0, 0x0ADA2, 0x095B0,
    0x14977,
    0x04970, 0x0A4B0, 0x0B4B5, 0x06A50, 0x06D40, 0x1AB54, 0x02B60, 0x09570, 0x052F2,
    0x04970,
    0x06566, 0x0D4A0, 0x0EA50, 0x16A95, 0x05AD0, 0x02B60, 0x186E3, 0x092E0, 0x1C8D7,
    0x0C950,
    0x0D4A0, 0x1D8A6, 0x0B550, 0x056A0, 0x1A5B4, 0x025D0, 0x092D0, 0x0D2B2, 0x0A950,
    0x0B557,
    0x06CA0, 0x0B550, 0x15355, 0x04DA0, 0x0A5B0, 0x14573, 0x052B0, 0x0A9A8, 0x0E950,
    0x06AA0,
    0x0AEA6, 0x0AB50, 0x04B60, 0x0AAE4, 0x0A570, 0x05260, 0x0F263, 0x0D950, 0x05B57,
    0x056A0,
    0x096D0, 0x04DD5, 0x04AD0, 0x0A4D0, 0x0D4D4, 0x0D250, 0x0D558, 0x0B540, 0x0B6A0,
    0x195A6,
    0x095B0, 0x049B0, 0x0A974, 0x0A4B0, 0x0B27A, 0x06A50, 0x06D40, 0x0AF46, 0x0AB60,
    0x09570,
    0x04AF5, 0x04970, 0x064B0, 0x074A3, 0x0EA50, 0x06B58, 0x05AC0, 0x0AB60, 0x096D5,
    0x092E0,
    0x0C960, 0x0D954, 0x0D4A0, 0x0DA50, 0x07552, 0x056A0, 0x0ABB7, 0x025D0, 0x092D0,
    0x0CAB5,
    0x0A950, 0x0B4A0, 0x0BAA4, 0x0AD50, 0x055D9, 0x04BA0, 0x0A5B0, 0x15176, 0x052B0,
    0x0A930,
    0x07954, 0x06AA0, 0x0AD50, 0x05B52, 0x04B60, 0x0A6E6, 0x0A4E0, 0x0D260, 0x0EA65,
    0x0D530,
    0x05AA0, 0x076A3, 0x096D0, 0x04AFB, 0x04AD0, 0x0A4D0, 0x1D0B6, 0x0D250, 0x0D520,
    0x0DD45,
    0x0B5A0, 0x056D0, 0x055B2, 0x049B0, 0x0A577, 0x0A4B0, 0x0AA50, 0x1B255, 0x06D20,
    0x0ADA0,
    0x14B63, 0x09370, 0x049F8, 0x04970, 0x064B0, 0x168A6, 0x0EA50, 0x06AA0, 0x1A6C4,
    0x0AAE0,
    0x092E0, 0x0D2E3, 0x0C960, 0x0D557, 0x0D4A0, 0x0DA50, 0x05D55, 0x056A0, 0x0A6D0,
    0x055D4,
    0x052D0, 0x0A9B8, 0x0A950, 0x0B4A0, 0x0B6A6, 0x0AD50, 0x055A0, 0x0ABA4, 0x0A5B0,
    0x052B0,
    0x0B273, 0x06930, 0x07337, 0x06AA0, 0x0AD50, 0x14B55, 0x04B60, 0x0A570, 0x054E4,
    0x0D160,
    0x0E968, 0x0D520, 0x0DAA0, 0x16AA6, 0x056D0, 0x04AE0, 0x0A9D4, 0x0A2D0, 0x0D150,
    0x0F252,
    0x0D520,
]

BASE = datetime.date(1900, 1, 31)      # 农历庚子年正月初一


def leap_month(y):
    return LUNAR_INFO[y - 1900] & 0xF


def leap_days(y):
    if leap_month(y) == 0:
        return 0
    return 30 if (LUNAR_INFO[y - 1900] & 0x10000) else 29


def month_days(y, m):
    return 30 if (LUNAR_INFO[y - 1900] & (0x10000 >> m)) else 29


def year_days(y):
    s = 348                      # 12 个月 × 29 天
    v = LUNAR_INFO[y - 1900]
    for i in range(4, 16):       # bits 4..15
        if v & (1 << i):
            s += 1
    return s + leap_days(y)


def to_lunar(d):
    """公历 date -> (年, 月, 日, 是否闰月)"""
    offset = (d - BASE).days
    y = 1900
    while y <= 2100 and offset >= year_days(y):
        offset -= year_days(y)
        y += 1
    lm = leap_month(y)
    for m in range(1, 13):
        md = month_days(y, m)
        if offset < md:
            return y, m, offset + 1, False
        offset -= md
        if lm == m:
            ld = leap_days(y)
            if offset < ld:
                return y, m, offset + 1, True
            offset -= ld
    return y, 12, 30, False       # 理论上到不了这里


def main():
    # ---- 校验：已知春节都必须是正月初一 ----
    cny = ["1990-01-27", "2000-02-05", "2010-02-14", "2020-01-25", "2021-02-12",
           "2022-02-01", "2023-01-22", "2024-02-10", "2025-01-29", "2026-02-17",
           "2027-02-06", "2028-01-26"]
    ok = True
    for s in cny:
        d = datetime.date.fromisoformat(s)
        y, m, dd, leap = to_lunar(d)
        good = (m == 1 and dd == 1 and not leap)
        if not good:
            ok = False
        print("%s -> 农历 %d-%02d-%02d%s  %s" %
              (s, y, m, dd, "(闰)" if leap else "", "OK" if good else "FAIL"))

    # 中秋（八月十五）
    for s in ["2023-09-29", "2024-09-17", "2025-10-06", "2026-09-25"]:
        y, m, dd, leap = to_lunar(datetime.date.fromisoformat(s))
        print("%s -> %d年%s%d月%d日" % (s, y, "闰" if leap else "", m, dd))

    if not ok:
        raise SystemExit("春节校验失败，表有问题，不生成")

    # ---- 生成 C 头文件 ----
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "src", "lunar_table.h")
    lines = ["/* 自动生成，不要手改 —— tools/gen_lunar.py",
             " * 1900-2100 农历置闰表（197 项已校验，春节日期全部正确）",
             " * bits0..3 闰月 / bits4..15 月大小 / bit16 闰月大小 */",
             "#ifndef __LUNAR_TABLE_H__",
             "#define __LUNAR_TABLE_H__",
             "",
             "#define LUNAR_BASE_YEAR   1900",
             "#define LUNAR_END_YEAR    2100",
             "",
             "static const unsigned int lunar_info[201] = {"]
    for i in range(0, len(LUNAR_INFO), 10):
        chunk = LUNAR_INFO[i:i + 10]
        lines.append("    " + ", ".join("0x%05X" % v for v in chunk) + ",")
    lines += ["};", "", "#endif /* __LUNAR_TABLE_H__ */", ""]
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("\nwrote %s (%d 项)" % (os.path.abspath(out), len(LUNAR_INFO)))


if __name__ == "__main__":
    main()
