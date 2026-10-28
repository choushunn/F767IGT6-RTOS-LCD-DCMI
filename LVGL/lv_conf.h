/**
 * LVGL v9.6.0 配置（STM32F767IGT6 / LTDC 1024x600 RGB565 / FreeRTOS）
 * 未列出的选项使用 lv_conf_internal.h 的默认值。
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/*========================= 内存与标准库 =========================*/
#define LV_USE_STDLIB_MALLOC   LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING   LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_BUILTIN

/* 内置内存池大小（字节）。控件较多时 16KB 不够，故给 64KB */
#define LV_MEM_SIZE            ( 64 * 1024U )

/*========================= 操作系统 =========================*/
/* 单任务内联渲染：lv_timer_handler() 在 LVGL 任务上下文同步完成渲染 */
#define LV_USE_OS              LV_OS_NONE

/*========================= 渲染 =========================*/
/* 必须与 LTDC 图层像素格式一致（RGB565，16 位，2 字节/像素） */
#define LV_COLOR_FORMAT_DEFAULT  LV_COLOR_FORMAT_RGB565
#define LV_DEF_REFR_PERIOD       33
#define LV_DPI_DEF               130

/*========================= 绘制加速 =========================*/
#define LV_USE_DRAW_SW               1

/* DMA2D：先关闭，确认 LTDC/SDRAM 显示链路正常后再置 1 */
#define LV_USE_DRAW_DMA2D            0
#define LV_DRAW_DMA2D_HAL_INCLUDE    "stm32f7xx_hal.h"
#define LV_USE_DRAW_DMA2D_INTERRUPT  0

/*========================= 日志 =========================*/
#define LV_USE_LOG            0

/*========================= 字体 =========================*/
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

#endif /* LV_CONF_H */
