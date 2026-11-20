/**
 * @file    gfx.h
 * @brief   墨水屏绘图层（128x250，双层：黑白 + 红）
 *
 * 这个模块**不依赖 Telink SDK**，只用到 string.h，
 * 所以同一份代码既能编进固件，也能在 PC 上跑（tools/gfx_sim.c）来预览。
 *
 * 位图语义：
 *   gfx_bw  位=1 -> 黑   位=0 -> 非黑
 *   gfx_red 位=1 -> 红   位=0 -> 非红
 *   白 = 两层都是 0
 *   每行 16 字节，bit7 是该行最左边那个像素（与 SSD1680 的 X 递增一致）
 */
#ifndef __GFX_H__
#define __GFX_H__

#define GFX_W           128
#define GFX_H           250
#define GFX_ROWB        (GFX_W / 8)
#define GFX_PLANE_BYTES (GFX_W * GFX_H / 8)

/* 颜色 */
#define GFX_WHITE 0
#define GFX_BLACK 1
#define GFX_RED   2

extern unsigned char gfx_bw [GFX_PLANE_BYTES];
extern unsigned char gfx_red[GFX_PLANE_BYTES];

void gfx_clear(int color);
void gfx_px(int x, int y, int color);
void gfx_fill(int x, int y, int w, int h, int color);
void gfx_frame(int x, int y, int w, int h, int thickness, int color);

/* 把整幅显存旋转 180°（行序反转 + 每行比特反转）。
 * 这块屏是倒装的：面板物理左上角对应控制器坐标的右下角，
 * 所以送屏前统一转 180°，绘制代码就可以一直按"正常朝上"的坐标写。 */
void gfx_flip180(void);

/* 只翻转行序（上下颠倒） */
void gfx_flip_y(void);

/* 只左右镜像 */
void gfx_flip_x(void);

/* 把整幅显存向"小 x"方向平移 n 个像素（n = 1..7）。
 * 面板玻璃在控制器 X 坐标里偏移了 6 列，而 X 不是字节对齐的，
 * 只能用移位来实现水平对齐。注意它必须在 gfx_flip180() 之后调用
 * （镜像会改变平移的方向），当前工程正是这个顺序。 */
void gfx_shift_left_bits(int n);

#endif /* __GFX_H__ */
