#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_glyph16.py —— 生成 16x16 中文字模（给 C 端日历用）

只生成工程实际用到的那几个字，省 flash。
每个字 32 字节：16 行 × 2 字节，bit15 = 该行最左像素，1 = 黑。

用法：python tools/gen_glyph16.py
产物：src/font_cn16.h
"""
import os

from PIL import Image, ImageDraw, ImageFont

# 顺序必须和 calendar.c 里的索引一致（生成头文件时会同时输出宏）
GLYPHS = "日一二三四五六年月周丙午马"
NAMES = ["CN_RI", "CN_YI", "CN_ER", "CN_SAN", "CN_SI", "CN_WU", "CN_LIU",
         "CN_NIAN", "CN_YUE", "CN_ZHOU", "CN_BING", "CN_WU2", "CN_MA"]

FONT = r"C:/Windows/Fonts/msyh.ttc"
SIZE = 16
THRESH = 100


def glyph_rows(ch, font):
    img = Image.new("L", (16, 16), 0)
    d = ImageDraw.Draw(img)
    bbox = d.textbbox((0, 0), ch, font=font)
    gw, gh = bbox[2] - bbox[0], bbox[3] - bbox[1]
    ox = (16 - gw) // 2 - bbox[0]
    oy = (16 - gh) // 2 - bbox[1]
    d.text((ox, oy), ch, font=font, fill=255)

    px = img.load()
    rows = []
    for y in range(16):
        v = 0
        for x in range(16):
            if px[x, y] >= THRESH:
                v |= 1 << (15 - x)
        rows.append(v)
    return rows


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "src", "font_cn16.h")

    font = ImageFont.truetype(FONT, SIZE)
    data = [glyph_rows(ch, font) for ch in GLYPHS]

    lines = []
    lines.append("/* 自动生成，不要手改 —— tools/gen_glyph16.py")
    lines.append(" * 16x16 中文字模：%s" % GLYPHS)
    lines.append(" * 每字 32 字节，16 行 x 2 字节，bit15 = 该行最左，1 = 黑 */")
    lines.append("#ifndef __FONT_CN16_H__")
    lines.append("#define __FONT_CN16_H__")
    lines.append("")
    for i, n in enumerate(NAMES):
        lines.append("#define %-8s %d" % (n, i))
    lines.append("")
    lines.append("#define CN_GLYPH_COUNT %d" % len(data))
    lines.append("")
    lines.append("static const unsigned char font_cn16[%d][32] = {" % len(data))
    for i, rows in enumerate(data):
        lines.append("    {   /* %s */" % GLYPHS[i])
        for r in rows:
            lines.append("        0x%02X, 0x%02X," % ((r >> 8) & 0xFF, r & 0xFF))
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* __FONT_CN16_H__ */")
    lines.append("")

    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))

    print("wrote %s (%d glyphs)" % (os.path.abspath(out), len(data)))
    # 目测预览
    for i, rows in enumerate(data[:2]):
        print("--- %s ---" % GLYPHS[i])
        for r in rows:
            print("".join("#" if r & (1 << (15 - x)) else "." for x in range(16)))


if __name__ == "__main__":
    main()
