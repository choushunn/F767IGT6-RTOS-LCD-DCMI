#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#include <stdint.h>

/* LTDC + SDRAM 帧缓冲，LVGL PARTIAL 渲染模式；逻辑画布竖屏 600x1024。 */
void lv_port_disp_init(void);

/* 将 LTDC 图层 1 的可见窗口限制为逻辑区域；物理坐标由当前显示旋转映射。 */
void lv_port_disp_set_visible_area(int32_t x0,
                                   int32_t y0,
                                   int32_t x1,
                                   int32_t y1);

#endif /* LV_PORT_DISP_H */
