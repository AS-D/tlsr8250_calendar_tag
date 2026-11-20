/**
 * @file    display.h
 * @brief   把显存送到墨水屏（含翻转/对位/反相 + 等 BUSY 收敛）
 */
#ifndef __DISPLAY_H__
#define __DISPLAY_H__

/* 刷一次屏（阻塞十几到二十几秒）。返回 BUSY 时长（ms），0 = 面板没响应 */
int display_present(void);

#endif /* __DISPLAY_H__ */
