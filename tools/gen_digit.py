#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_digit.py —— 生成 6x12 窄数字字模（日历日期用）

8x16 的等宽数字两个就占满 17px 的格子，日期会挤成一团；
这套 6x12 只含 0-9，两个数字 12px，格子里还剩 5px 间距。

每字 12 字节，一字节一行，bit5 = 该行最左，1 = 黑。
"""
import os

from PIL import Image, ImageDraw, ImageFont

CELL_W, CELL_H = 6, 12
FONT = r"C:/Windows/Fonts/consola.ttf"
SIZE = 12
THRESH = 90


def glyph(ch, font):
    img = Image.new("L", (CELL_W, CELL_H), 0)
    d = ImageDraw.Draw(img)
    bbox = d.textbbox((0, 0), ch, font=font)
    gw, gh = bbox[2] - bbox[0], bbox[3] - bbox[1]
    ox = (CELL_W - gw) // 2 - bbox[0]
    oy = (CELL_H - gh) // 2 - bbox[1]
    d.text((ox, oy), ch, font=font, fill=255)
    px = img.load()
    rows = []
    for y in range(CELL_H):
        v = 0
        for x in range(CELL_W):
            if px[x, y] >= THRESH:
                v |= 1 << (5 - x)
        rows.append(v)
    return rows


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "src", "font_digit.h")
    font = ImageFont.truetype(FONT, SIZE)
    data = [glyph(str(i), font) for i in range(10)]

    lines = ["/* 自动生成，不要手改 —— tools/gen_digit.py",
             " * 6x12 窄数字 0-9，一字节一行，bit5 = 最左 */",
             "#ifndef __FONT_DIGIT_H__",
             "#define __FONT_DIGIT_H__",
             "",
             "#define DIGIT_W 6",
             "#define DIGIT_H 12",
             "",
             "static const unsigned char font_digit6x12[10][12] = {"]
    for i, rows in enumerate(data):
        body = ",".join("0x%02X" % r for r in rows)
        lines.append("    { %s },  /* %d */" % (body, i))
    lines += ["};", "", "#endif /* __FONT_DIGIT_H__ */", ""]

    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("wrote %s" % os.path.abspath(out))
    for i in (1, 2, 8):
        print("--- %d ---" % i)
        for r in data[i]:
            print("".join("#" if r & (0x20 >> x) else "." for x in range(6)))


if __name__ == "__main__":
    main()
