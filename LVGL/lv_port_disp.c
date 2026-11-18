#include "lv_port_disp.h"

#include "lvgl.h"

#include "ltdc.h"   /* hltdc */
#include "display_config.h"

/* ATK-7016 7 寸 RGB 屏：1024x600，RGB565。初始区域为左侧 544x600。 */
#define LV_PORT_DISP_HOR_RES   DISPLAY_LVGL_W
#define LV_PORT_DISP_VER_RES   DISPLAY_LVGL_H

/* 帧缓冲位于外部 SDRAM（FMC Bank1 基址），与 LTDC 图层 0 使用同一地址。
 * 按 1024x600 上限预留 1228800B（相机缓冲从 0xC0140000 起，互不重叠） */
#define LV_PORT_DISP_FB_ADDR   ( (void *)DISPLAY_FB_ADDR )

static lv_display_t *s_disp = NULL;

/* DIRECT 模式：像素由 LVGL 直接写入 LTDC 正在扫描的帧缓冲，flush 只做完成通知 */
static void lv_port_disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
  (void)area;
  (void)px_map;

  lv_display_flush_ready(disp);
}

/* 同步 LTDC 图层 0 的窗口/跨度到新的显示尺寸 */
static void lv_port_disp_ltdc_area(uint16_t w, uint16_t h)
{
  LTDC_LayerCfgTypeDef cfg = {0};

  cfg.WindowX0 = 0;
  cfg.WindowX1 = w;
  cfg.WindowY0 = 0;
  cfg.WindowY1 = h;
  cfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  cfg.Alpha = 255;
  cfg.Alpha0 = 0;
  cfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  cfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  cfg.FBStartAdress = (uint32_t)LV_PORT_DISP_FB_ADDR;
  cfg.ImageWidth = w;
  cfg.ImageHeight = h;

  (void)HAL_LTDC_ConfigLayer(&hltdc, &cfg, LTDC_LAYER_1);
  __HAL_LTDC_LAYER_ENABLE(&hltdc, LTDC_LAYER_1);
  __HAL_LTDC_VERTICAL_BLANKING_RELOAD_CONFIG(&hltdc);
}

void lv_port_disp_set_area(uint16_t w, uint16_t h)
{
  if (s_disp == NULL)
  {
    return;
  }

  lv_port_disp_ltdc_area(w, h);

  lv_display_set_resolution(s_disp, w, h);
  lv_display_set_buffers(s_disp,
                         LV_PORT_DISP_FB_ADDR,
                         NULL,
                         (uint32_t)w * (uint32_t)h * 2U,
                         LV_DISPLAY_RENDER_MODE_DIRECT);

  lv_obj_invalidate(lv_screen_active());
}

void lv_port_disp_init(void)
{
  lv_display_t *disp = lv_display_create(LV_PORT_DISP_HOR_RES, LV_PORT_DISP_VER_RES);
  if (disp == NULL)
  {
    return;
  }

  s_disp = disp;

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
