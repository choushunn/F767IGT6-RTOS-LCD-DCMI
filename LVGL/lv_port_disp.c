#include "lv_port_disp.h"

#include "lvgl.h"

#include "dma2d.h"
#include "ltdc.h"   /* hltdc */
#include "display_config.h"

/* ATK-7016 7 寸 RGB 屏：1024x600，RGB565。运行区域为左侧 424x600。 */
#define LV_PORT_DISP_HOR_RES   DISPLAY_LVGL_W
#define LV_PORT_DISP_VER_RES   DISPLAY_LVGL_H

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
  uint32_t screen_width;
  uint32_t bytes;
  uint32_t dst;

  width = (uint32_t)(area->x2 - area->x1 + 1);
  height = (uint32_t)(area->y2 - area->y1 + 1);
  screen_width = (uint32_t)lv_display_get_horizontal_resolution(disp);
  bytes = width * height * 2U;
  dst = (uint32_t)LV_PORT_DISP_FB_ADDR +
        ((((uint32_t)area->y1 * screen_width) + (uint32_t)area->x1) * 2U);

  /* DMA2D 不参与 D-Cache 一致性维护：像素是 CPU 刚写进 s_drawBuf 的，
   * 若此时 D-Cache 打开，脏行可能还没写回 SRAM，DMA2D 会读到旧像素。
   * 这里按 32B 行清理一次（Cache 关闭时是空操作，保留以便将来开启 Cache）。 */
  SCB_CleanDCache_by_Addr((uint32_t *)px_map, (int32_t)bytes);
  __DSB();

  lv_port_dma2d_wait();

  DMA2D->CR = DMA2D_M2M;
  DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;
  DMA2D->OOR = screen_width - width;
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
  uint16_t oldW;

  if (s_disp == NULL)
  {
    return;
  }

  oldW = (uint16_t)lv_display_get_horizontal_resolution(s_disp);

  /* 行跨度（LTDC CFBLR 的 pitch）马上就要变成 w，而帧缓冲里还是按旧跨度
   * 排布的像素。先把整屏刷黑，避免切换瞬间看到一帧斜着的乱码；
   * 随后 LVGL 会把新界面整屏重绘。只在缩小时做，启动扩屏时 SDRAM 本来就是黑的。 */
  if (w < oldW)
  {
    lv_port_disp_clear_area(DISPLAY_PANEL_W, DISPLAY_PANEL_H);
  }

  lv_port_disp_ltdc_area(w, h);

  lv_display_set_resolution(s_disp, w, h);
  lv_display_set_buffers(s_disp,
                         s_drawBuf,
                         NULL,
                         sizeof(s_drawBuf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

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

  /* 内部 SRAM 局部缓冲：渲染与 LTDC 扫描解耦，减少 FMC 争用。 */
  lv_display_set_buffers(disp,
                         s_drawBuf,
                         NULL,
                         sizeof(s_drawBuf),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_display_set_flush_cb(disp, lv_port_disp_flush_cb);
  lv_display_set_default(disp);
}
