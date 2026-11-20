#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
u8g2_py.py —— 把 u8g2 字体（src/font_u8g2.c 里的字节数组）在 PC 上解码出来

只是为了 tools/preview.py 预览用：解码逻辑和 firmware 的 u8g2_font.c 一致
（BBX 模式 0、无 RLE 旋转、透明模式）。

字体数据是 u8g2 的“新格式”：
  头 24 字节（另外 start_pos_unicode 之后的 unicode 查找表）
  字形条目 = 编码(1B) + 长度(1B) + 数据   （编码 <= 255）
           = 编码(2B) + 长度(1B) + 数据   （编码 > 255）
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")
FONT_C = os.path.join(SRC, "font_u8g2.c")

_ARR_RE = re.compile(r"const\s+uint8_t\s+(\w+)\s*\[\s*\d+\s*\]\s*=\s*\{([^}]*)\}")


def load_font_arrays(path=FONT_C):
    text = open(path, encoding="utf-8").read()
    arrays = {}
    for m in _ARR_RE.finditer(text):
        name = m.group(1)
        body = m.group(2)
        arrays[name] = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    return arrays


class BitReader:
    """u8g2 的位流是“字节内低位在前”（decode_get_unsigned_bits 用 >> bit_pos）"""

    def __init__(self, data, bitpos=0):
        self.d = data
        self.pos = bitpos

    def u(self, n):
        v = 0
        for i in range(n):
            byte = self.d[self.pos >> 3]
            bit = (byte >> (self.pos & 7)) & 1
            v |= bit << i
            self.pos += 1
        return v

    def s(self, n):
        """符号位不是二进制补码，而是“偏移码”：v - 2^(n-1)"""
        v = self.u(n)
        if n:
            v -= 1 << (n - 1)
        return v


def _s8(v):
    return v - 256 if v > 127 else v


class Font:
    """一个 u8g2 字体"""

    def __init__(self, name, data):
        self.name = name
        self.data = data
        d = data
        self.glyph_cnt = d[0]
        self.bbx_mode = d[1]
        self.bits_per_0 = d[2]
        self.bits_per_1 = d[3]
        self.bits_per_char_width = d[4]
        self.bits_per_char_height = d[5]
        self.bits_per_char_x = d[6]
        self.bits_per_char_y = d[7]
        self.bits_per_delta_x = d[8]
        self.max_char_width = d[9]
        self.max_char_height = d[10]
        self.x_offset = _s8(d[11])
        self.y_offset = _s8(d[12])
        self.ascent_A = d[13]
        self.start_pos_upper_A = (d[17] << 8) | d[18]
        self.start_pos_lower_a = (d[19] << 8) | d[20]
        self.start_pos_unicode = (d[21] << 8) | d[22]
        self._cache = {}

    @property
    def ascent(self):
        """基线以上的高度（u8g2 定义：max_char_height + y_offset）"""
        return self.max_char_height + self.y_offset

    @property
    def height(self):
        return self.max_char_height

    # ---- 查找字形数据 ----
    def _glyph_ptr(self, enc):
        d = self.data
        p = 23
        if enc <= 255:
            if enc >= ord('a'):
                p += self.start_pos_lower_a
            elif enc >= ord('A'):
                p += self.start_pos_upper_A
            while True:
                if d[p + 1] == 0:
                    return None
                if d[p] == enc:
                    return p + 2
                p += d[p + 1]
        p += self.start_pos_unicode
        table = p
        while True:
            p += (d[table] << 8) | d[table + 1]
            e = (d[table + 2] << 8) | d[table + 3]
            table += 4
            if e >= enc:
                break
        while True:
            e = (d[p] << 8) | d[p + 1]
            if e == 0:
                return None
            if e == enc:
                return p + 3
            p += d[p + 2]          # 条目里的长度字节 = 紧随其后的字形数据长度
            # 注意：p 指向的是“数据起点”，长度字节在 p-1

    # ---- 解码 ----
    def glyph(self, enc):
        if enc in self._cache:
            return self._cache[enc]
        ptr = self._glyph_ptr(enc)
        if ptr is None:
            self._cache[enc] = None
            return None
        br = BitReader(self.data, ptr * 8)
        gw = br.u(self.bits_per_char_width)
        gh = br.u(self.bits_per_char_height)
        xo = br.s(self.bits_per_char_x)
        yo = br.s(self.bits_per_char_y)
        adv = br.s(self.bits_per_delta_x)

        # 目标点（相对基线）：字形左上角
        tx = xo
        ty = -(gh + yo)

        pixels = []
        gx = gy = 0

        def run(n, val):
            nonlocal gx, gy
            cnt = n
            while cnt > 0:
                rem = gw - gx
                if rem <= 0:
                    gx = 0
                    gy += 1
                    continue
                cur = cnt if cnt < rem else rem
                if val:
                    for i in range(cur):
                        pixels.append((tx + gx + i, ty + gy))
                gx += cur
                cnt -= cur
                if gx >= gw:
                    gx = 0
                    gy += 1

        # 逐行 RLE：每行先读一段“空”、再读一段“实”，行末用 1 bit 判断是否续行
        if gw > 0 and gh > 0:
            while gy < gh:
                a = br.u(self.bits_per_0)
                b = br.u(self.bits_per_1)
                while True:
                    run(a, 0)
                    run(b, 1)
                    if br.u(1) == 0:
                        break
        g = {"advance": adv, "pixels": pixels, "w": gw, "h": gh, "top": ty}
        self._cache[enc] = g
        return g

    def width(self, text):
        w = 0
        for ch in text:
            g = self.glyph(ord(ch))
            if g:
                w += g["advance"]
        return w

    def ink_extent(self, text):
        """返回 (min_y, max_y) 相对基线，用于垂直居中；无字形返回 (0, 0)"""
        lo, hi = 0, 0
        first = True
        for ch in text:
            g = self.glyph(ord(ch))
            if not g or not g["pixels"]:
                continue
            ys = [p[1] for p in g["pixels"]]
            if first:
                lo, hi = min(ys), max(ys)
                first = False
            else:
                lo, hi = min(lo, min(ys)), max(hi, max(ys))
        return lo, hi


_FONTS = None


def get_fonts():
    """返回 {'wqy9': Font, 'wqy12': Font, 'helvB14': Font, 'helvB18': Font}"""
    global _FONTS
    if _FONTS is None:
        arrays = load_font_arrays()
        _FONTS = {
            "wqy9": Font("wqy9", arrays["u8g2_font_wqy9_t_lunar"]),
            "wqy12": Font("wqy12", arrays["u8g2_font_wqy12_t_lunar"]),
            "helvB14": Font("helvB14", arrays["u8g2_font_helvB14_tn"]),
            "helvB18": Font("helvB18", arrays["u8g2_font_helvB18_tn"]),
        }
    return _FONTS


def draw_text(canvas, font, x, baseline, text, color, bold=False):
    """canvas 需要有 .px(x, y, color)；bold = 右移 1px 叠印一次（加粗）"""
    pen = x
    for ch in text:
        g = font.glyph(ord(ch))
        if not g:
            continue
        for (px_, py_) in g["pixels"]:
            canvas.px(pen + px_, baseline + py_, color)
            if bold:
                canvas.px(pen + px_ + 1, baseline + py_, color)
        pen += g["advance"]
    return pen - x


if __name__ == "__main__":
    fs = get_fonts()
    for k, f in fs.items():
        print("%-8s glyphs=%3d maxwh=%dx%d ascentA=%d ascent=%d y_off=%d"
              % (k, f.glyph_cnt, f.max_char_width, f.max_char_height,
                 f.ascent_A, f.ascent, f.y_offset))
    for k in ("wqy9", "helvB14", "helvB18"):
        for s in ("日", "一", "二", "三", "十", "月", "丙", "午", "马", "年", "十月十一"):
            f = fs[k]
            g = f.glyph(ord(s[0]))
            if g:
                print("  %-6s %s w=%d h=%d adv=%d top=%d" % (k, s, g["w"], g["h"], g["advance"], g["top"]))
            else:
                print("  %-6s %s 缺字" % (k, s))
    print("wqy9 十月十一 width =", fs["wqy9"].width("十月十一"))
    print("wqy12 十月十一 width =", fs["wqy12"].width("十月十一"))
    for s in ("2026", "19", "30", "11"):
        print("helvB14 %-4s w=%d   helvB18 w=%d" % (s, fs["helvB14"].width(s), fs["helvB18"].width(s)))
        print("    ink extents 14 %s 18 %s" % (fs["helvB14"].ink_extent(s), fs["helvB18"].ink_extent(s)))
