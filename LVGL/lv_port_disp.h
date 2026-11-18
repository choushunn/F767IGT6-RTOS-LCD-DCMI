#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#include <stdint.h>

/* LTDC + SDRAM 帧缓冲，LVGL DIRECT 渲染模式 */
void lv_port_disp_init(void);

/* 运行时切换 LVGL 显示区域：同步调整 LTDC 图层 0 窗口与 LVGL 分辨率/缓冲。
 * 启动动画用 1024x600 全屏，加载完成后收回左栏 544x600 */
void lv_port_disp_set_area(uint16_t w, uint16_t h);

#endif /* LV_PORT_DISP_H */
