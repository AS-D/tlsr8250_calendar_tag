/**
 * @file    gfx.c
 * @brief   墨水屏绘图层实现（不依赖 SDK，可在 PC 上编译预览）
 */
#include "gfx.h"
#include <string.h>

unsigned char gfx_bw [GFX_PLANE_BYTES];
unsigned char gfx_red[GFX_PLANE_BYTES];

void gfx_clear(int color)
{
    memset(gfx_bw,  0, GFX_PLANE_BYTES);
    memset(gfx_red, 0, GFX_PLANE_BYTES);
    if (color == GFX_BLACK) memset(gfx_bw,  0xFF, GFX_PLANE_BYTES);
    if (color == GFX_RED)   memset(gfx_red, 0xFF, GFX_PLANE_BYTES);
}

void gfx_px(int x, int y, int color)
{
    int idx;
    unsigned char bit;
    if (x < 0 || x >= GFX_W || y < 0 || y >= GFX_H) return;
    idx = y * GFX_ROWB + (x >> 3);
    bit = (unsigned char)(0x80 >> (x & 7));
    switch (color) {
        case GFX_BLACK: gfx_bw[idx]  |=  bit; gfx_red[idx] &= (unsigned char)~bit; break;
        case GFX_RED:   gfx_bw[idx]  &= (unsigned char)~bit; gfx_red[idx] |=  bit; break;
        default:        gfx_bw[idx]  &= (unsigned char)~bit; gfx_red[idx] &= (unsigned char)~bit; break;
    }
}

void gfx_fill(int x, int y, int w, int h, int color)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            gfx_px(x + i, y + j, color);
}

void gfx_frame(int x, int y, int w, int h, int thickness, int color)
{
    gfx_fill(x, y, w, thickness, color);
    gfx_fill(x, y + h - thickness, w, thickness, color);
    gfx_fill(x, y, thickness, h, color);
    gfx_fill(x + w - thickness, y, thickness, h, color);
}

/* ------------------------------------------------------------------------- */
/* 180° 旋转                                                                  */
/* ------------------------------------------------------------------------- */
static unsigned char bitrev8(unsigned char b)
{
    b = (unsigned char)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = (unsigned char)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = (unsigned char)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

static void flip_plane(unsigned char *p)
{
    unsigned char tmp[GFX_ROWB];
    unsigned char *a, *b, *row;
    int y, i, half;

    /* 1. 行序反转 */
    for (y = 0; y < GFX_H / 2; y++) {
        a = p + y * GFX_ROWB;
        b = p + (GFX_H - 1 - y) * GFX_ROWB;
        memcpy(tmp, a, GFX_ROWB);
        memcpy(a, b, GFX_ROWB);
        memcpy(b, tmp, GFX_ROWB);
    }
    /* 2. 每行左右镜像：既要反转字节顺序，也要反转字节内的比特
     *    （只做比特反转会变成"每 8 像素一组的镜像"，是错的） */
    half = GFX_ROWB / 2;
    for (y = 0; y < GFX_H; y++) {
        row = p + y * GFX_ROWB;
        for (i = 0; i < half; i++) {
            unsigned char left  = bitrev8(row[i]);
            unsigned char right = bitrev8(row[GFX_ROWB - 1 - i]);
            row[i]              = right;
            row[GFX_ROWB - 1 - i] = left;
        }
        if (GFX_ROWB & 1) {
            row[half] = bitrev8(row[half]);
        }
    }
}

void gfx_flip180(void)
{
    flip_plane(gfx_bw);
    flip_plane(gfx_red);
}

/* 只翻转行序（上下颠倒），不动左右。
 * 原厂固件是"从最后一行往上写"，而我们的缓冲是自上而下的，
 * 所以用这个把 y 轴对齐，X 方向保持不动。 */
static void plane_flip_rows(unsigned char *p)
{
    unsigned char tmp[GFX_ROWB];
    unsigned char *a, *b;
    int y;
    for (y = 0; y < GFX_H / 2; y++) {
        a = p + y * GFX_ROWB;
        b = p + (GFX_H - 1 - y) * GFX_ROWB;
        memcpy(tmp, a, GFX_ROWB);
        memcpy(a, b, GFX_ROWB);
        memcpy(b, tmp, GFX_ROWB);
    }
}

void gfx_flip_y(void)
{
    plane_flip_rows(gfx_bw);
    plane_flip_rows(gfx_red);
}

/* 左右镜像：既要反转 16 个字节的顺序，也要反转每字节内的比特 */
static void plane_mirror_x(unsigned char *p)
{
    int y, i, half = GFX_ROWB / 2;
    for (y = 0; y < GFX_H; y++) {
        unsigned char *row = p + y * GFX_ROWB;
        for (i = 0; i < half; i++) {
            unsigned char left  = bitrev8(row[i]);
            unsigned char right = bitrev8(row[GFX_ROWB - 1 - i]);
            row[i]                = right;
            row[GFX_ROWB - 1 - i] = left;
        }
        if (GFX_ROWB & 1) row[half] = bitrev8(row[half]);
    }
}

void gfx_flip_x(void)
{
    plane_mirror_x(gfx_bw);
    plane_mirror_x(gfx_red);
}

/* 每行整体向小 x 方向移 n 位（跨字节搬移） */
static void plane_shift_left(unsigned char *p, int n)
{
    int y, i;
    for (y = 0; y < GFX_H; y++) {
        unsigned char *r = p + y * GFX_ROWB;
        for (i = 0; i < GFX_ROWB - 1; i++)
            r[i] = (unsigned char)((r[i] << n) | (r[i + 1] >> (8 - n)));
        r[GFX_ROWB - 1] = (unsigned char)(r[GFX_ROWB - 1] << n);
    }
}

void gfx_shift_left_bits(int n)
{
    if (n <= 0 || n >= 8) return;
    plane_shift_left(gfx_bw,  n);
    plane_shift_left(gfx_red, n);
}
