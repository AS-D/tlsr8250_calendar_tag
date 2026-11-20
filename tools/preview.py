#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
preview.py —— 在 PC 上复刻 calendar.c 的横向版式（250x122），烧录前先看

字体和固件里用的是同一份 u8g2 字体（src/font_u8g2.c，见 tools/u8g2_py.py）：
  wqy9  文泉驿点阵宋 Bold 12px —— 表头/干支/农历/周
  wqy12 文泉驿点阵宋 Bold 16px —— 月字（大一点）
  helvB14 Helvetica Bold 14px —— 日期/周数
  helvB18 Helvetica Bold 18px —— 年份/公历月

用法：
  python tools/preview.py
  python tools/preview.py --date 2026-11-19 --out out\\pv.png
"""
import argparse
import datetime
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_lunar import to_lunar            # noqa: E402
from u8g2_py import get_fonts, draw_text  # noqa: E402

CAL_W, CAL_H = 250, 122
MARGIN = 3
CELL_W, ROW_H, GRID_X, GRID_TOP, GRID_Y = 24, 17, 3, 3, 19
HEAD_H = 16
SEP_X, PANEL_L = 173, 175
PANEL_W = CAL_W - MARGIN - PANEL_L       # 72
LINE_W = CAL_W - MARGIN - SEP_X
TZ = 28800
WHITE, BLACK, RED = (255, 255, 255), (0, 0, 0), (190, 30, 30)

F9, F12, F14, F18 = (None,) * 4


def load_fonts():
    global F9, F12, F14, F18
    f = get_fonts()
    F9, F12, F14, F18 = f["wqy9"], f["wqy12"], f["helvB14"], f["helvB18"]


class Canvas:
    def __init__(self):
        self.im = Image.new("RGB", (CAL_W, CAL_H), WHITE)
        self.d = ImageDraw.Draw(self.im)

    def px(self, x, y, c):
        if 0 <= x < CAL_W and 0 <= y < CAL_H:
            self.d.point((x, y), c)

    def fill(self, x, y, w, h, c):
        self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=c)

    # ---- 文字 ----
    def text_c(self, font, cx, baseline, s, c, bold=False):
        """以 cx 为中心画一段 UTF-8 文本"""
        w = font.width(s)
        draw_text(self, font, cx - w // 2, baseline, s, c, bold)

    def text_l(self, font, x, baseline, s, c, bold=False):
        return draw_text(self, font, x, baseline, s, c, bold)


def days_in_month(y, m):
    nxt = datetime.date(y + (m == 12), 1 if m == 12 else m + 1, 1)
    return (nxt - datetime.date(y, m, 1)).days


def lunar_month_name(m):
    return ["", "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊"][m]


def lunar_day_name(d):
    if d <= 10:
        return "初" + "一二三四五六七八九十"[d - 1]
    if d < 20:
        return "十" + "一二三四五六七八九"[d - 11]
    if d == 20:
        return "二十"
    if d < 30:
        return "廿" + "一二三四五六七八九"[d - 21]
    return "三十"


def draw(epoch):
    local = epoch + TZ
    days = local // 86400
    dt = datetime.datetime(1970, 1, 1) + datetime.timedelta(days=days)
    y, m, d = dt.year, dt.month, dt.day

    # 右栏三段基线，与 src/calendar.c 的 BASE_* 一一对应（没有电池电压那段）
    base_year, base_ganzhi, line1 = 28, 42, 47
    mon_base, base_lunar, line2 = 72, 86, 92
    base_week = 110

    # 版式固定（与 src/calendar.c 完全一致）：表头 16px，公历月 18px + 16px 月字
    hdr_font, hdr_h, grid_y = F12, HEAD_H, GRID_Y
    mon_num, mon_yue = F18, F12

    ly_year, _, _, _ = to_lunar(datetime.date(y, m, d))

    c = Canvas()

    # ---------------- 表头：日/六 红底，其余黑底，白字 ----------------
    hdr = "日一二三四五六"
    hdr_base = GRID_TOP + hdr_h - 2
    for i in range(7):
        bg = RED if i in (0, 6) else BLACK
        cx = GRID_X + i * CELL_W
        c.fill(cx, GRID_TOP, CELL_W, hdr_h, bg)
        c.text_c(hdr_font, cx + CELL_W // 2, hdr_base, hdr[i], WHITE)

    # ---------------- 日期网格 ----------------
    ndays = days_in_month(y, m)
    lead = (datetime.date(y, m, 1).weekday() + 1) % 7
    rows = (lead + ndays + 6) // 7
    avail = CAL_H - grid_y - MARGIN            # 119 或 117
    y_off = (6 - rows) * ROW_H // 2
    for day in range(1, ndays + 1):
        idx = lead + day - 1
        r, col = idx // 7, idx % 7
        if r > 5:
            break
        cx, cy = GRID_X + col * CELL_W, grid_y + y_off + r * ROW_H
        color = RED if col in (0, 6) else BLACK
        if day == d:
            c.fill(cx, cy, CELL_W, ROW_H, RED)
            color = WHITE
        c.text_c(F14, cx + CELL_W // 2, cy + 15, str(day), color)

    # ---------------- 竖线 ----------------
    c.fill(SEP_X, MARGIN, 1, CAL_H - 2 * MARGIN, BLACK)

    # ---------------- 年份（红，Helvetica 18） ----------------
    c.text_c(F18, PANEL_L + PANEL_W // 2, base_year, str(y), RED)

    # ---------------- 干支生肖（黑，12px，按农历年推） ----------------
    STEM, BRANCH = "甲乙丙丁戊己庚辛壬癸", "子丑寅卯辰巳午未申酉戌亥"
    ZODIAC = "鼠牛虎兔龙蛇马羊猴鸡狗猪"
    ly = ly_year if ly_year else y
    k = (ly - 4) % 60
    gz = STEM[k % 10] + BRANCH[k % 12] + ZODIAC[k % 12] + "年"
    c.text_c(F9, PANEL_L + PANEL_W // 2, base_ganzhi, gz, BLACK)
    c.fill(SEP_X, line1, LINE_W, 1, BLACK)

    # ---------------- 公历月（红） ----------------
    num = str(m)
    nw, yw = mon_num.width(num), mon_yue.width("月")
    mw = nw + yw
    mx = PANEL_L + (PANEL_W - mw) // 2
    c.text_l(mon_num, mx, mon_base, num, RED)
    c.text_l(mon_yue, mx + nw, mon_base, "月", RED, True)

    # ---------------- 农历（黑，12px） ----------------
    ly, lm, ldd, leap = to_lunar(datetime.date(y, m, d))
    ls = ("闰" if leap else "") + lunar_month_name(lm) + "月" + lunar_day_name(ldd)
    lw = F9.width(ls)
    c.text_l(F9, PANEL_L + (PANEL_W - lw) // 2, base_lunar, ls, BLACK)
    c.fill(SEP_X, line2, LINE_W, 1, BLACK)

    # ---------------- 第几周 ----------------
    jan1 = days - (dt.timetuple().tm_yday - 1)
    wk = (days - jan1) // 7 + 1
    zhou = "周" + str(wk)
    zw = F9.width("周") + F14.width(str(wk))
    zx = PANEL_L + (PANEL_W - zw) // 2
    c.text_l(F9, zx, base_week, "周", BLACK)
    c.text_l(F14, zx + F9.width("周"), base_week, str(wk), BLACK)

    return c.im, (y, m, d, lm, ldd)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--epoch", type=int, default=None)
    ap.add_argument("--date", default=None)
    ap.add_argument("--out", default=None)
    a = ap.parse_args()

    load_fonts()
    if a.epoch is not None:
        epoch = a.epoch
    else:
        dt = datetime.datetime.strptime(a.date, "%Y-%m-%d") if a.date else datetime.datetime.now()
        epoch = int((dt - datetime.datetime(1970, 1, 1)).total_seconds()) - TZ

    out = a.out or os.path.join(here, "..", "out", "calendar_preview.png")
    im, info = draw(epoch)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    im.resize((CAL_W * 3, CAL_H * 3), Image.NEAREST).save(out)
    print("wrote", os.path.abspath(out))
    print("solar %d-%02d-%02d  lunar %02d-%02d" % info)


if __name__ == "__main__":
    main()
