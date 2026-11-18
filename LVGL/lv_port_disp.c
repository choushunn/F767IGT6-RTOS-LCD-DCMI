#include "lv_port_disp.h"

#include "lvgl.h"

#include "dma2d.h"
#include "ltdc.h"   /* hltdc */
#include "display_config.h"

/* ATK-7016 7 寸 RGB 屏：原生 1024x600，RGB565；逻辑竖屏 600x1024。
 * 实机装配方向与顺时针 90° 相反，因此使用 270°。 */
#define LV_PORT_DISP_PHYS_W    DISPLAY_PANEL_W
#define LV_PORT_DISP_PHYS_H    DISPLAY_PANEL_H
#define LV_PORT_DISP_ROTATION  LV_DISPLAY_ROTATION_270

/* 帧缓冲位于外部 SDRAM（FMC Bank1 基址），与 LTDC 图层 0 使用同一地址。
 * 按 1024x600 上限预留 1228800B（相机缓冲从 0xC0140000 起，互不重叠） */
#define LV_PORT_DISP_FB_ADDR   ( (void *)DISPLAY_FB_ADDR )

/* LVGL 在内部 SRAM 渲染，再由 DMA2D 按区域写入 SDRAM。
 * 1024x32 RGB565 缓冲足以覆盖启动动画全宽和运行界面的多条扫描线。 */
#define LV_PORT_DRAW_BUF_PIXELS  (DISPLAY_PANEL_W * 32U)

/* DMA2D 一次搬运的等待上限（ms）。全屏 1024x600 也只需要 ~25ms。 */
#define LV_PORT_FLUSH_TIMEOUT_MS 50U

static lv_display_t *s_disp = NULL;

/* 32B 对齐：DMA2D 从这块 SRAM 取数，同时便于将来打开 D-Cache 后做 cache clean */
static uint16_t s_drawBuf[LV_PORT_DRAW_BUF_PIXELS] LV_ATTRIBUTE_MEM_ALIGN
    __attribute__((aligned(32)));

/* 等待 DMA2D 空闲；超时则强制停止，避免下一次配置和残留传输打架 */
static void lv_port_dma2d_wait(void)
{
  uint32_t startTick = HAL_GetTick();

  while ((DMA2D->CR & DMA2D_CR_START) != 0U)
  {
    if ((HAL_GetTick() - startTick) > LV_PORT_FLUSH_TIMEOUT_MS)
    {
      DMA2D->CR &= ~DMA2D_CR_START;
      break;
    }
  }

  DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE |
                DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;
}

/* 用 DMA2D 把指定区域刷成黑色（RGB565 0x0000），用于切换显示区域时避免残留画面 */
static void lv_port_disp_clear_area(uint16_t w, uint16_t h)
{
  DMA2D->IFCR = DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE |
                DMA2D_FLAG_CAE | DMA2D_FLAG_CTC | DMA2D_FLAG_TW;
  DMA2D->CR = DMA2D_R2M;
  DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;
  DMA2D->OCOLR = 0U;
  DMA2D->OMAR = (uint32_t)LV_PORT_DISP_FB_ADDR;
  DMA2D->OOR = 0U;
  /* NLR: [15:0] = 行数，[29:16] = 每行像素数。 */
  DMA2D->NLR = ((uint32_t)w << DMA2D_NLR_PL_Pos) |
               ((uint32_t)h << DMA2D_NLR_NL_Pos);
  DMA2D->CR = DMA2D_R2M | DMA2D_CR_START;

  lv_port_dma2d_wait();
}

/* 局部渲染模式：px_map 位于内部 SRAM，flush 由 DMA2D 按行写入 LTDC 帧缓冲。 */
static void lv_port_disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
  uint32_t width;
  uint32_t height;
  uint32_t bytes;
  uint32_t dst;
  uint32_t src_stride;
  uint32_t dst_stride;
  lv_area_t rotated_area;
  lv_display_rotation_t rotation;

  width = (uint32_t)(area->x2 - area->x1 + 1);
  height = (uint32_t)(area->y2 - area->y1 + 1);
  bytes = width * height * 2U;

  /* DMA2D 不参与 D-Cache 一致性维护：像素是 CPU 刚写进 s_drawBuf 的，
   * 若此时 D-Cache 打开，脏行可能还没写回 SRAM，DMA2D 会读到旧像素。
   * 这里按 32B 行清理一次（Cache 关闭时是空操作，保留以便将来开启 Cache）。 */
  SCB_CleanDCache_by_Addr((uint32_t *)px_map, (int32_t)bytes);
  __DSB();

  rotation = lv_display_get_rotation(disp);
  if (rotation != LV_DISPLAY_ROTATION_0)
  {
    /* LVGL 局部缓冲先在内部 SRAM 完成竖屏渲染，再按逻辑->物理映射
     * 旋转写入 SDRAM。DMA2D 不支持 90° 旋转，因此此路径由 CPU 写回。 */
    rotated_area = *area;
    lv_display_rotate_area(disp, &rotated_area);

    src_stride = width * 2U;
    dst_stride = LV_PORT_DISP_PHYS_W * 2U;
    dst = (uint32_t)LV_PORT_DISP_FB_ADDR +
          ((((uint32_t)rotated_area.y1 * LV_PORT_DISP_PHYS_W) +
            (uint32_t)rotated_area.x1) * 2U);

    lv_draw_rotate(px_map,
                   (void *)dst,
                   (int32_t)width,
                   (int32_t)height,
                   (int32_t)src_stride,
                   (int32_t)dst_stride,
                   rotation,
                   LV_COLOR_FORMAT_RGB565);
    __DSB();
    lv_display_flush_ready(disp);
    return;
  }

  dst = (uint32_t)LV_PORT_DISP_FB_ADDR +
        ((((uint32_t)area->y1 * LV_PORT_DISP_PHYS_W) + (uint32_t)area->x1) * 2U);
  lv_port_dma2d_wait();

  DMA2D->CR = DMA2D_M2M;
  DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;
  DMA2D->OOR = LV_PORT_DISP_PHYS_W - width;
  DMA2D->FGOR = 0;
  DMA2D->FGPFCCR = DMA2D_INPUT_RGB565;
  DMA2D->FGMAR = (uint32_t)px_map;
  DMA2D->OMAR = dst;
  /* NLR: [15:0] = 行数，[29:16] = 每行像素数。 */
  DMA2D->NLR = (width << DMA2D_NLR_PL_Pos) |
               (height << DMA2D_NLR_NL_Pos);
  DMA2D->CR = DMA2D_M2M | DMA2D_CR_START;

  lv_port_dma2d_wait();

  lv_display_flush_ready(disp);
}

void lv_port_disp_set_visible_area(int32_t x0,
                                   int32_t y0,
                                   int32_t x1,
                                   int32_t y1)
{
  LTDC_LayerCfgTypeDef cfg = {0};
  lv_area_t area;

  if (s_disp == NULL)
  {
    return;
  }

  area.x1 = x0;
  area.y1 = y0;
  area.x2 = x1;
  area.y2 = y1;
  lv_display_rotate_area(s_disp, &area);

  cfg.WindowX0 = area.x1;
  cfg.WindowX1 = area.x2 + 1;
  cfg.WindowY0 = area.y1;
  cfg.WindowY1 = area.y2 + 1;
  cfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  cfg.Alpha = 255;
  cfg.Alpha0 = 0;
  cfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  cfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  cfg.FBStartAdress = (uint32_t)LV_PORT_DISP_FB_ADDR;
  cfg.ImageWidth = LV_PORT_DISP_PHYS_W;
  cfg.ImageHeight = LV_PORT_DISP_PHYS_H;

  (void)HAL_LTDC_ConfigLayer(&hltdc, &cfg, LTDC_LAYER_1);
  __HAL_LTDC_LAYER_ENABLE(&hltdc, LTDC_LAYER_1);
  __HAL_LTDC_VERTICAL_BLANKING_RELOAD_CONFIG(&hltdc);
}

void lv_port_disp_init(void)
{
  lv_display_t *disp = lv_display_create(LV_PORT_DISP_PHYS_W, LV_PORT_DISP_PHYS_H);
  if (disp == NULL)
  {
    return;
  }

  s_disp = disp;

  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_rotation(disp, LV_PORT_DISP_ROTATION);

  /* 旋转后的局部区域由 CPU 直接写入，先清掉 SDRAM 中的旧启动画面。 */
  lv_port_disp_clear_area(LV_PORT_DISP_PHYS_W, LV_PORT_DISP_PHYS_H);

  /* 内部 SRAM 局部缓冲：渲染与 LTDC 扫描解耦，减少 FMC 争用。 */
  lv_display_set_buffers(disp,
                         s_drawBuf,
                         NULL,
                         sizeof(s_drawBuf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_display_set_flush_cb(disp, lv_port_disp_flush_cb);
  lv_display_set_default(disp);
}
