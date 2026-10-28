#include "lv_port_disp.h"

#include "lvgl.h"

/* ATK-7016 7 寸 RGB 屏：1024x600，RGB565 */
#define LV_PORT_DISP_HOR_RES   1024
#define LV_PORT_DISP_VER_RES   600

/* 帧缓冲位于外部 SDRAM（FMC Bank1 基址），与 LTDC 图层 0 使用同一地址 */
#define LV_PORT_DISP_FB_ADDR   ( (void *)0xC0000000UL )

/* DIRECT 模式：像素由 LVGL 直接写入 LTDC 正在扫描的帧缓冲，flush 只做完成通知 */
static void lv_port_disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
  (void)area;
  (void)px_map;

  lv_display_flush_ready(disp);
}

void lv_port_disp_init(void)
{
  lv_display_t *disp = lv_display_create(LV_PORT_DISP_HOR_RES, LV_PORT_DISP_VER_RES);
  if (disp == NULL)
  {
    return;
  }

  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

  /* 单缓冲 DIRECT 模式：LVGL 与 LTDC 共用同一块 SDRAM 帧缓冲 */
  lv_display_set_buffers(disp,
                         LV_PORT_DISP_FB_ADDR,
                         NULL,
                         LV_PORT_DISP_HOR_RES * LV_PORT_DISP_VER_RES * 2U,
                         LV_DISPLAY_RENDER_MODE_DIRECT);

  lv_display_set_flush_cb(disp, lv_port_disp_flush_cb);
  lv_display_set_default(disp);
}
