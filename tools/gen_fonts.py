#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_fonts.py —— 生成日历用的两套字模

1) 日期数字：**经典 5x7 点阵**，2 倍放大成 10x14（笔画 2 像素）。
   点阵字是照着像素格设计的，不会有 TTF 阈值化那种不规则阶梯。
2) 右侧信息栏中文 12x12：从 msyh 2 倍超采样降采样（中文没法手绘点阵）。

产物：src/font_digit.h、src/font_cn12.h
每行一字节 / 两字节，bit(宽-1) = 该行最左，1 = 黑。
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

FONT_CN = r"C:/Windows/Fonts/msyh.ttc"
FONT_CN_BOLD = r"C:/Windows/Fonts/msyhbd.ttc"
FONT_HELV = r"C:/Windows/Fonts/arialbd.ttf"   # Arial Bold ≈ Helvetica Bold（u8g2 的 helvB）

GLYPHS = "一二三四五六七八九十日年月周丙午马正冬腊初廿闰"

# ---------------------------------------------------------------- 5x7 点阵数字
DIGITS_5X6 = {
    0: [".###.", "#...#", "#...#", "#...#", "#...#", ".###."],
    1: ["..#..", ".##..", "..#..", "..#..", "..#..", ".###."],
    2: [".###.", "#...#", "...#.", "..#..", ".#...", "#####"],
    3: ["####.", "....#", ".###.", "....#", "....#", "####."],
    4: ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#."],
    5: ["#####", "#....", "####.", "....#", "#...#", ".###."],
    6: ["..##.", ".#...", "####.", "#...#", "#...#", ".###."],
    7: ["#####", "....#", "...#.", "..#..", ".#...", ".#..."],
    8: [".###.", "#...#", ".###.", "#...#", "#...#", ".###."],
    9: [".###.", "#...#", ".####", "....#", "...#.", ".##.."],
}

SCALE = 2                      # 5x6 -> 10x12


def digits_bitmap():
    """返回 10 个 10x12 的数字（每行 2 字节，bit9 = 最左）"""
    out = []
    for d in range(10):
        rows = []
        for line in DIGITS_5X6[d]:
            v = 0
            for x, ch in enumerate(line):
                if ch == "#":
                    for k in range(SCALE):
                        v |= 1 << (9 - (x * SCALE + k))
            # 每个源行重复 SCALE 次
            for _ in range(SCALE):
                rows.append(v)
        out.append(rows)                 # 12 行
    return out


def render_cn(ch, font, cw, chh, thresh, ss=2):
    """中文：2 倍超采样再降采样，减轻阶梯"""
    W, H = cw * ss, chh * ss
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    bbox = d.textbbox((0, 0), ch, font=font)
    gw, gh = bbox[2] - bbox[0], bbox[3] - bbox[1]
    ox = (W - gw) // 2 - bbox[0]
    oy = (H - gh) // 2 - bbox[1]
    d.text((ox, oy), ch, font=font, fill=255)
    if ss > 1:
        img = img.resize((cw, chh), Image.BOX)
    px = img.load()
    rows = []
    for y in range(chh):
        v = 0
        for x in range(cw):
            if px[x, y] >= thresh:
                v |= 1 << (cw - 1 - x)
        rows.append(v)
    return rows


def emit(path, guard, name, cw, chh, data, wide, comment):
    lines = ["/* 自动生成，不要手改 —— tools/gen_fonts.py",
             " * %s" % comment,
             " * 每字 %d 行，bit%d = 该行最左，1 = 黑 */" % (chh, cw - 1),
             "#ifndef %s" % guard,
             "#define %s" % guard,
             "",
             "#define %s_W %d" % (name.upper(), cw),
             "#define %s_H %d" % (name.upper(), chh),
             ""]
    typ = "unsigned short" if wide else "unsigned char"
    fmt = "0x%04X" if wide else "0x%02X"
    lines.append("static const %s %s[%d][%d] = {" % (typ, name, len(data), chh))
    for i, rows in enumerate(data):
        lines.append("    { %s },  /* %d */" %
                     (",".join(fmt % r for r in rows), i))
    lines += ["};", "", "#endif /* %s */" % guard, ""]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


def ttf_digits(cw, chh, size, thresh, ss=3, path=FONT_CN_BOLD):
    """用微软雅黑的数字，ss 倍超采样再降采样 —— 曲线比点阵顺"""
    font = ImageFont.truetype(path, size * ss)
    out = []
    for d in range(10):
        out.append(render_cn(str(d), font, cw, chh, thresh, ss))
    return out


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    src = os.path.join(here, "..", "src")
    mode = sys.argv[1] if len(sys.argv) > 1 else "ttf"

    # 1) 数字
    if mode == "dot":
        dg = digits_bitmap()
        note = "日期数字 5x6 点阵 x2 = 10x12"
    elif mode == "helv":
        # Arial Bold 15px 原始尺寸直接渲染（开 hinting，不超采样）——
        # 和 u8g2 的 helvB14/18 点阵同一套生成方式：笔画对齐像素格，最干净
        dg = ttf_digits(10, 14, 15, 128, 1, FONT_HELV)
        note = "日期数字 ArialBold 15px hinting 直出 -> 10x14 (对标 u8g2 helvB)"
    else:
        dg = ttf_digits(10, 14, 15, 128, 3)
        note = "日期数字 雅黑粗体 15px 3x超采样 -> 10x14"
    emit(os.path.join(src, "font_digit.h"), "__FONT_DIGIT_H__", "font_digit",
         10, 14, dg, True, note)

    # 2) 中文：12x12 超采样
    fc = ImageFont.truetype(FONT_CN, 12 * 2)
    cn = [render_cn(c, fc, 12, 12, 110) for c in GLYPHS]
    emit(os.path.join(src, "font_cn12.h"), "__FONT_CN12_H__", "font_cn12",
         12, 12, cn, True, "信息栏中文 12x12：%s" % GLYPHS)

    print("wrote font_digit.h (%s) and font_cn12.h (12x12)" % note)

    print("--- 数字 2 / 8 ---")
    for row in range(len(dg[0])):
        a = "".join("#" if dg[2][row] & (0x200 >> x) else "." for x in range(10))
        b = "".join("#" if dg[8][row] & (0x200 >> x) else "." for x in range(10))
        print(a + "  " + b)


if __name__ == "__main__":
    main()
