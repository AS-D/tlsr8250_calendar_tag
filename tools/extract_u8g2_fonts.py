#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
extract_u8g2_fonts.py —— 从 ink-tag 工程里抠出 4 个 u8g2 字体，生成 src/font_u8g2.c

来源：F:\\openSource\\ink-tag-da14585\\firmware\\common\\gui\\inktag_gui_fonts.c
（u8g2 字体数据本身是 U8g2 项目从文泉驿点阵宋 / Adobe Helvetica Bold 转出来的，
 字体文件许可证见 licenses/；这里只做格式转换，不改字形）

用法：
  python tools/extract_u8g2_fonts.py
"""
import os
import re
import sys

REF = r"F:\openSource\ink-tag-da14585\firmware\common\gui\inktag_gui_fonts.c"
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")

FONTS = [
    ("u8g2_font_wqy9_t_lunar",  "wqy9",   "文泉驿点阵宋 Bold 12px（正文/表头/农历）"),
    ("u8g2_font_wqy12_t_lunar", "wqy12",  "文泉驿点阵宋 Bold 16px（大标题备用）"),
    ("u8g2_font_helvB14_tn",    "helvB14", "Helvetica Bold 14px（星期/小数字）"),
    ("u8g2_font_helvB18_tn",    "helvB18", "Helvetica Bold 18px（年份/日期大数字）"),
]


def parse_c_string_literals(body):
    """把 'a' "\\344\\0..." 这种 C 字符串拼成 bytes"""
    out = bytearray()
    i = 0
    n = len(body)
    while i < n:
        ch = body[i]
        if ch == '"':
            i += 1
            while i < n and body[i] != '"':
                c = body[i]
                if c == '\\':
                    i += 1
                    c = body[i]
                    if c in '01234567':
                        oct_digits = c
                        i += 1
                        for _ in range(2):
                            if i < n and body[i] in '01234567':
                                oct_digits += body[i]
                                i += 1
                            else:
                                break
                        out.append(int(oct_digits, 8) & 0xFF)
                        continue
                    esc = {'n': 10, 'r': 13, 't': 9, '0': 0, '\\': 92, '"': 34, "'": 39,
                           'b': 8, 'f': 12, 'v': 11, 'a': 7, '?': 63}
                    out.append(esc.get(c, ord(c)))
                    i += 1
                else:
                    out.append(ord(c) & 0xFF)
                    i += 1
            i += 1
        else:
            i += 1
    return bytes(out)


def extract():
    text = open(REF, encoding="utf-8", errors="replace").read()
    # 只取默认内嵌分支（第一份出现的数组）
    result = {}
    for name, _short, _desc in FONTS:
        m = re.search(r"const\s+uint8_t\s+%s\[(\d+)\]\s*[^=]*=\s*(.*?)\s*;\s*\n"
                      % re.escape(name), text, re.S)
        if not m:
            raise SystemExit("没找到字体数组: " + name)
        size = int(m.group(1))
        data = parse_c_string_literals(m.group(2))
        # C 字符串字面量自带的结尾 '\0' 也算进数组长度，补一个即可
        if len(data) == size - 1:
            data += b"\x00"
        if len(data) != size:
            raise SystemExit("%s 长度不符: 声明 %d, 实际 %d" % (name, size, len(data)))
        result[name] = data
        print("%-26s %5d bytes" % (name, len(data)))
    return result


def font_metrics(data):
    """u8g2 字体头 24 字节，见 u8g2_font.h"""
    keys = ["glyph_cnt", "bbx_mode", "bits_per_0", "bits_per_1",
            "bits_per_char_width", "bits_per_char_height", "bits_per_char_x",
            "bits_per_char_y", "bits_per_delta_x", "max_char_width",
            "max_char_height", "x_offset", "y_offset", "ascent_A", "descent_g",
            "ascent_para", "descent_para"]
    d = {k: data[i] for i, k in enumerate(keys)}
    d["start_pos_upper_A"] = (data[17] << 16) | (data[18] << 8) | data[19]
    d["start_pos_lower_a"] = (data[20] << 16) | (data[21] << 8) | data[22]
    d["start_pos_unicode"] = (data[23] << 16) | (data[24] << 8) | data[25] if len(data) > 25 else 0
    return d


def emit_c(fonts):
    out = []
    out.append("""/**
 * @file    font_u8g2.c
 * @brief   u8g2 字体数据（由 tools/extract_u8g2_fonts.py 自动生成，别手改）
 *
 * 来源：ink-tag 工程 firmware/common/gui/inktag_gui_fonts.c
 *   wqy9 / wqy12  —— 文泉驿点阵宋体 Bold（-wenquanyi-wenquanyi bitmap song-bold）
 *   helvB14 / 18  —— Adobe Helvetica Bold
 * 这 4 个都是 u8g2 的“农历日历子集”，只带 ASCII + 农历/节气/干支/生肖等用字，
 * 体积小（合计约 9.7KB），正好够日历界面用。
 */
#include "font_u8g2.h"
""")
    for name, short, desc in FONTS:
        data = fonts[name]
        out.append("/* %s —— %s，%d 字节 */" % (name, desc, len(data)))
        out.append("const uint8_t %s[%d] = {" % (name, len(data)))
        per_line = 16
        for off in range(0, len(data), per_line):
            chunk = data[off:off + per_line]
            out.append("    " + ",".join("0x%02X" % b for b in chunk) + ",")
        out.append("};")
        out.append("")
    path = os.path.join(SRC, "font_u8g2.c")
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    print("wrote", os.path.abspath(path))

    h = ["""/**
 * @file    font_u8g2.h
 * @brief   u8g2 字体数据声明（自动生成，见 tools/extract_u8g2_fonts.py）
 */
#ifndef __FONT_U8G2_H__
#define __FONT_U8G2_H__

#include <stdint.h>

/* 文泉驿点阵宋 Bold 12px：ASCII + 农历日历汉字子集，228 个字形 */""" ]
    for name, short, desc in FONTS:
        size = len(fonts[name])
        h.append("/* %-8s %s，%d 字节 */" % (short, desc, size))
        h.append("extern const uint8_t %s[%d];" % (name, size))
        h.append("#define FONT_%s_SIZE %d" % (short.upper(), size))
        h.append("")
    h.append("#endif /* __FONT_U8G2_H__ */\n")
    path = os.path.join(SRC, "font_u8g2.h")
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(h))
    print("wrote", os.path.abspath(path))


def main():
    fonts = extract()
    for name, _s, _d in FONTS:
        m = font_metrics(fonts[name])
        print("%-26s maxwh=%2dx%-2d bbx=%d ascent=%2d descent=%2d glyphs=%d"
              % (name, m["max_char_width"], m["max_char_height"], m["bbx_mode"],
                 m["max_char_height"] + m["y_offset"], m["descent_g"], m["glyph_cnt"]))
    emit_c(fonts)


if __name__ == "__main__":
    main()
