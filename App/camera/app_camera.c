#include "app_camera.h"

#include "camera_util.h"
#include "ov5640.h"
#include "pcf8574.h"
#include "sccb.h"

#include "dcmi.h"
#include "ltdc.h"
#include "uart/app_uart.h"

/* 一帧 480x480 RGB565 = 460800B = 115200 个 32 位字。
 * NDTR 只有 16 位（<=65535），整帧长度会被静默截断，故按行搬运：
 * 一行 480 像素 = 240 个 32 位字，DMA 每次传 240 字，行完成中断把该行拷入帧缓冲。 */
#define CAM_LINE_WORDS      240U   /* 一行 480 像素 = 240 个 32 位字 */
#define CAM_FB_BYTES        (CAM_IMG_W * CAM_IMG_H * 2U)

/* 中断优先级：2 小于 RTOS 阈值 5（数值更小优先级更高），
 * 故 ISR 内禁止调用任何 FreeRTOS API */
#define CAM_DMA_IRQ_PRIO    2U
#define CAM_DCMI_IRQ_PRIO   2U

static DMA_HandleTypeDef s_hdmaCam;

/* 行双缓冲：DMA 交替写入两块行缓冲，行完成中断把刚写完的那块拷入帧缓冲 */
static uint32_t s_lineBuf0[CAM_LINE_WORDS];
static uint32_t s_lineBuf1[CAM_LINE_WORDS];

/* 发布包：bit[1:0] = 缓冲序号(0..2)，其余位 = 累计发布帧序号；ISR 单字写入，任务侧一次读取 */
static volatile uint32_t s_pubPacket = 0U;
static volatile uint32_t s_vsyncCnt = 0U;
static volatile uint32_t s_esyncCnt = 0U;
static volatile uint32_t s_eovrCnt = 0U;
static volatile uint32_t s_dmaErrCnt = 0U;
static volatile uint32_t s_ltdcFuCnt = 0U;    /* LTDC FIFO 下溢次数（带宽不足的迹象） */
static volatile uint32_t s_ltdcTerrCnt = 0U;  /* LTDC 取数传输错误次数 */
static volatile uint32_t s_lineCnt = 0U;   /* 当前正在写的帧内行号 0..479 */
static volatile uint8_t s_writeFb = 0U;    /* DMA 正在写入的帧缓冲序号 */

static uint8_t s_camReady = 0U;
static volatile uint8_t s_layerOn = 0U;
static uint32_t s_lastSeq = 0U;
static uint32_t s_lastStatSeq = 0U;
static uint32_t s_statTick = 0U;

static uint32_t cam_fb_addr(uint8_t fb)
{
  if (fb == 0U)
  {
    return CAM_FB0_ADDR;
  }
  if (fb == 1U)
  {
    return CAM_FB1_ADDR;
  }
  return CAM_FB2_ADDR;
}

/*=========================== 中断服务（不调用 RTOS API） ===========================*/

/* DCMI 帧结束中断：只做真实帧计数 */
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
  /* 必须先清帧标志再重开中断，否则中断无限重入占满 CPU */
  __HAL_DCMI_CLEAR_FLAG(hdcmi, DCMI_FLAG_FRAMERI);
  __HAL_DCMI_ENABLE_IT(hdcmi, DCMI_IT_FRAME);

  s_vsyncCnt++;
}

void DCMI_IRQHandler(void)
{
  HAL_DCMI_IRQHandler(&hdcmi);
}

/* DMA2_Stream1：按行双缓冲搬运（NDTR=240 字/行），每行完成把该行拷入当前帧缓冲，
 * 累计 480 行后发布该帧并切到另一块帧缓冲 */
void DMA2_Stream1_IRQHandler(void)
{
  if (__HAL_DMA_GET_FLAG(&s_hdmaCam, __HAL_DMA_GET_TC_FLAG_INDEX(&s_hdmaCam)) != RESET)
  {
    const uint32_t *src;
    uint32_t *dst;
    uint32_t i;

    __HAL_DMA_CLEAR_FLAG(&s_hdmaCam, __HAL_DMA_GET_TC_FLAG_INDEX(&s_hdmaCam));

    /* CT 位指示当前（下一个）传输目标，刚完成的必是另一块 */
    if ((DMA2_Stream1->CR & DMA_SxCR_CT) != 0U)
    {
      src = s_lineBuf0;
    }
    else
    {
      src = s_lineBuf1;
    }

    dst = (uint32_t *)(cam_fb_addr(s_writeFb) + (s_lineCnt * CAM_LINE_WORDS * 4U));
    for (i = 0U; i < CAM_LINE_WORDS; i++)
    {
      dst[i] = src[i];
    }

    s_lineCnt++;
    if (s_lineCnt >= CAM_IMG_H)
    {
      uint32_t seq = (s_pubPacket >> 2) + 1U;

      s_lineCnt = 0U;
      s_pubPacket = (seq << 2) | (uint32_t)s_writeFb;

      /* 帧完成即刻切换图层扫描地址（只改 CFBAR + VBR，垂直消隐期生效）。
       * 若等任务轮询再切，33ms 一帧期间 DMA 会写回正在被扫描的缓冲，表现为撕裂。 */
      if (s_layerOn != 0U)
      {
        LTDC_Layer2->CFBAR = cam_fb_addr(s_writeFb);
        __HAL_LTDC_VERTICAL_BLANKING_RELOAD_CONFIG(&hltdc);
      }

      /* 三缓冲轮换：写入目标永远不是刚发布的那块，也不是 LTDC 正在扫的那块 */
      s_writeFb = (uint8_t)((s_writeFb + 1U) % CAM_FB_NUM);
    }
  }

  /* 传输/直接模式/FIFO 错误只计数，不关闭采集（DCMI OVR/ERR 中断同样不开） */
  if (__HAL_DMA_GET_FLAG(&s_hdmaCam, __HAL_DMA_GET_TE_FLAG_INDEX(&s_hdmaCam)) != RESET)
  {
    __HAL_DMA_CLEAR_FLAG(&s_hdmaCam, __HAL_DMA_GET_TE_FLAG_INDEX(&s_hdmaCam));
    s_dmaErrCnt++;
  }
  if (__HAL_DMA_GET_FLAG(&s_hdmaCam, __HAL_DMA_GET_DME_FLAG_INDEX(&s_hdmaCam)) != RESET)
  {
    __HAL_DMA_CLEAR_FLAG(&s_hdmaCam, __HAL_DMA_GET_DME_FLAG_INDEX(&s_hdmaCam));
    s_dmaErrCnt++;
  }
  if (__HAL_DMA_GET_FLAG(&s_hdmaCam, __HAL_DMA_GET_FE_FLAG_INDEX(&s_hdmaCam)) != RESET)
  {
    __HAL_DMA_CLEAR_FLAG(&s_hdmaCam, __HAL_DMA_GET_FE_FLAG_INDEX(&s_hdmaCam));
    s_dmaErrCnt++;
  }
}

/*=========================== 初始化 ===========================*/

/* DCMI 运行时重配：上升沿采样、8 位并口、只开帧结束中断 */
static void cam_dcmi_config(void)
{
  __HAL_DCMI_DISABLE(&hdcmi);

  hdcmi.Init.SynchroMode = DCMI_SYNCHRO_HARDWARE;
  hdcmi.Init.PCKPolarity = DCMI_PCKPOLARITY_RISING;
  hdcmi.Init.VSPolarity = DCMI_VSPOLARITY_LOW;
  hdcmi.Init.HSPolarity = DCMI_HSPOLARITY_LOW;
  hdcmi.Init.CaptureRate = DCMI_CR_ALL_FRAME;
  hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
  hdcmi.Init.JPEGMode = DCMI_JPEG_DISABLE;
  hdcmi.Init.ByteSelectMode = DCMI_BSM_ALL;
  hdcmi.Init.ByteSelectStart = DCMI_OEBS_ODD;
  hdcmi.Init.LineSelectMode = DCMI_LSM_ALL;
  hdcmi.Init.LineSelectStart = DCMI_OELS_ODD;
  (void)HAL_DCMI_Init(&hdcmi);

  /* HAL_DCMI_Init 默认打开了行/VSYNC/ERR/OVR 中断，这里全部关掉：
   * F4/F7 HAL 在 OVR/ERR 中断里会 Abort DMA，改为任务侧读标志计数 */
  __HAL_DCMI_DISABLE_IT(&hdcmi, DCMI_IT_LINE | DCMI_IT_VSYNC | DCMI_IT_ERR | DCMI_IT_OVR);
  __HAL_DCMI_CLEAR_FLAG(&hdcmi,
                        DCMI_FLAG_FRAMERI | DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI | DCMI_FLAG_VSYNCRI);
  __HAL_DCMI_ENABLE_IT(&hdcmi, DCMI_IT_FRAME);

  HAL_NVIC_SetPriority(DCMI_IRQn, CAM_DCMI_IRQ_PRIO, 0U);
  HAL_NVIC_EnableIRQ(DCMI_IRQn);
}

/* DCMI 数据/同步脚改成上拉 + 最高速度，否则 PCLK 上升沿会丢采样点 */
static void cam_gpio_boost(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF13_DCMI;

  gpio.Pin = GPIO_PIN_6;                                        /* PA6 PIXCLK */
  HAL_GPIO_Init(GPIOA, &gpio);

  gpio.Pin = GPIO_PIN_8;                                        /* PH8 HSYNC */
  HAL_GPIO_Init(GPIOH, &gpio);

  gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 |
             GPIO_PIN_11;                                       /* PC6..PC9/PC11 D0..D4 */
  HAL_GPIO_Init(GPIOC, &gpio);

  gpio.Pin = GPIO_PIN_3;                                        /* PD3 D5 */
  HAL_GPIO_Init(GPIOD, &gpio);

  gpio.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;              /* PB7 VSYNC, PB8/PB9 D6/D7 */
  HAL_GPIO_Init(GPIOB, &gpio);
}

/* 启动 DMA 行双缓冲并打开采集 */
static uint8_t cam_capture_start(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();

  s_hdmaCam.Instance = DMA2_Stream1;
  s_hdmaCam.Init.Channel = DMA_CHANNEL_1;
  s_hdmaCam.Init.Direction = DMA_PERIPH_TO_MEMORY;
  s_hdmaCam.Init.PeriphInc = DMA_PINC_DISABLE;
  s_hdmaCam.Init.MemInc = DMA_MINC_ENABLE;
  s_hdmaCam.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  s_hdmaCam.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  s_hdmaCam.Init.Mode = DMA_CIRCULAR;
  s_hdmaCam.Init.Priority = DMA_PRIORITY_HIGH;          /* 与 LTDC/LVGL 抢 SDRAM 时不被饿死 */
  s_hdmaCam.Init.FIFOMode = DMA_FIFOMODE_ENABLE;        /* 1/2 阈值吸收总线抖动，避免丢采样点 */
  s_hdmaCam.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_HALFFULL;
  s_hdmaCam.Init.MemBurst = DMA_MBURST_SINGLE;
  s_hdmaCam.Init.PeriphBurst = DMA_PBURST_SINGLE;

  HAL_DMA_DeInit(&s_hdmaCam);
  if (HAL_DMA_Init(&s_hdmaCam) != HAL_OK)
  {
    return 1U;
  }

  __HAL_LINKDMA(&hdcmi, DMA_Handle, s_hdmaCam);
  __HAL_UNLOCK(&s_hdmaCam);   /* 先解锁，否则 MultiBufferStart 会直接返回 HAL_BUSY */

  /* 行双缓冲：两块行缓冲交替，每块 240 字（一行） */
  s_lineCnt = 0U;
  s_writeFb = 0U;
  s_pubPacket = 0U;
  if (HAL_DMAEx_MultiBufferStart_IT(&s_hdmaCam,
                                    (uint32_t)&DCMI->DR,
                                    (uint32_t)s_lineBuf0,
                                    (uint32_t)s_lineBuf1,
                                    CAM_LINE_WORDS) != HAL_OK)
  {
    return 1U;
  }

  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, CAM_DMA_IRQ_PRIO, 0U);
  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);

  __HAL_DCMI_ENABLE(&hdcmi);
  DCMI->CR |= DCMI_CR_CAPTURE;

  return 0U;
}

/* 传感器上电、复位与 SCCB 配置；任一环节失败返回 1 */
static uint8_t cam_sensor_init(void)
{
  uint16_t width;
  uint16_t height;
  uint16_t hts;
  uint16_t vts;

  /* PCF8574 先就位，并保持 PWDN 高电平（掉电） */
  if (PCF8574_Init() != 0U)
  {
    AppUart_Printf("[cam] pcf8574 not found\r\n");
    return 1U;
  }
  OV5640_PWDN_Set(1U);

  /* 复位时序：RST 拉低 20ms -> PWDN 清零上电 -> 5ms -> 释放 RST -> 20ms */
  OV5640_RST_Init();
  OV5640_RST_Set(0U);
  Camera_DelayMs(20U);
  OV5640_PWDN_Set(0U);
  Camera_DelayMs(5U);
  OV5640_RST_Set(1U);
  Camera_DelayMs(20U);

  SCCB_Init();
  Camera_DelayMs(5U);

  if (OV5640_Init() != 0U)
  {
    AppUart_Printf("[cam] ov5640 id error\r\n");
    return 1U;
  }
  AppUart_Printf("[cam] ov5640 id ok\r\n");

  OV5640_RGB565_Mode();
  (void)OV5640_OutSize_Set(16U, 4U, CAM_IMG_W, CAM_IMG_H);
  OV5640_SetMirrorFlip(CAM_MIRROR_H, CAM_FLIP_V);
  AppUart_Printf("[cam] mirror=%u flip=%u reg3820=0x%02X reg3821=0x%02X\r\n",
                 (unsigned)CAM_MIRROR_H,
                 (unsigned)CAM_FLIP_V,
                 (unsigned)OV5640_RD_Reg(0x3820),
                 (unsigned)OV5640_RD_Reg(0x3821));

  /* 回读输出尺寸与 HTS/VTS，便于核对帧率 */
  width = (uint16_t)(((uint16_t)OV5640_RD_Reg(0x3808) << 8) | OV5640_RD_Reg(0x3809));
  height = (uint16_t)(((uint16_t)OV5640_RD_Reg(0x380A) << 8) | OV5640_RD_Reg(0x380B));
  hts = (uint16_t)(((uint16_t)OV5640_RD_Reg(0x380C) << 8) | OV5640_RD_Reg(0x380D));
  vts = (uint16_t)(((uint16_t)OV5640_RD_Reg(0x380E) << 8) | OV5640_RD_Reg(0x380F));
  AppUart_Printf("[cam] sensor out=%ux%u hts=%u vts=%u\r\n", width, height, hts, vts);

  return 0U;
}

void AppCamera_Init(void)
{
  s_camReady = 0U;

  if (cam_sensor_init() != 0U)
  {
    return;
  }

  cam_dcmi_config();
  cam_gpio_boost();

  if (cam_capture_start() != 0U)
  {
    AppUart_Printf("[cam] dma start failed\r\n");
    return;
  }
  AppUart_Printf("[cam] capture started\r\n");

  s_lastStatSeq = s_pubPacket >> 2;
  s_statTick = HAL_GetTick();
  s_camReady = 1U;

  AppUart_Printf("[cam] init done\r\n");
}

/*=========================== 显示与统计 ===========================*/

/* 顶层混合因子必须与 ltdc.c 一致：PAxCA + Alpha0=0，窗口外默认色透明 */
static void cam_layer_config(uint32_t fbAddr)
{
  LTDC_LayerCfgTypeDef cfg = {0};

  cfg.WindowX0 = 320U;
  cfg.WindowX1 = 800U;
  cfg.WindowY0 = 0U;
  cfg.WindowY1 = 480U;
  cfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  cfg.Alpha = 255U;
  cfg.Alpha0 = 0U;
  cfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  cfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  cfg.FBStartAdress = fbAddr;
  cfg.ImageWidth = CAM_IMG_W;
  cfg.ImageHeight = CAM_IMG_H;
  cfg.Backcolor.Red = 0U;
  cfg.Backcolor.Green = 0U;
  cfg.Backcolor.Blue = 0U;

  (void)HAL_LTDC_ConfigLayer(&hltdc, &cfg, LTDC_LAYER_2);

  /* 层寄存器是影子寄存器：HAL_LTDC_ConfigLayer 不会置 LEN，
   * 必须显式使能图层并触发重载，否则右栏永远是黑/背景色 */
  __HAL_LTDC_LAYER_ENABLE(&hltdc, LTDC_LAYER_2);
  __HAL_LTDC_VERTICAL_BLANKING_RELOAD_CONFIG(&hltdc);
}

/* OVR/ERR 只读标志计数，不开中断 */
static void cam_poll_dcmi_error(void)
{
  /* 直接读 RISR：本工程 HAL 的 __HAL_DCMI_GET_FLAG 仍访问旧寄存器名 RIS/MIS */
  if ((DCMI->RISR & DCMI_FLAG_ERRRI) != 0U)
  {
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_ERRRI);
    s_esyncCnt++;
  }
  if ((DCMI->RISR & DCMI_FLAG_OVRRI) != 0U)
  {
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI);
    s_eovrCnt++;
  }
}

/* LTDC 取数错误/FIFO 下溢：读标志并清除，用于判断撕裂是否由 SDRAM 带宽不足引起。
 * 注意 F7 的 LTDC 只有一个 FUIF，不分图层 */
static void cam_poll_ltdc_error(void)
{
  uint32_t isr = LTDC->ISR;

  if ((isr & LTDC_ISR_FUIF) != 0U)
  {
    LTDC->ICR = LTDC_ICR_CFUIF;
    s_ltdcFuCnt++;
  }
  if ((isr & LTDC_ISR_TERRIF) != 0U)
  {
    LTDC->ICR = LTDC_ICR_CTERRIF;
    s_ltdcTerrCnt++;
  }
}

/* 每 2s 打印一次：帧率以 pub（发布帧数）为准，vsync 存在竞态漏计 */
static void cam_print_stats(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t elapsed = now - s_statTick;
  uint32_t seq;
  uint32_t fps;

  if (elapsed < 2000U)
  {
    return;
  }

  seq = s_pubPacket >> 2;
  fps = (uint32_t)(((seq - s_lastStatSeq) * 1000U) / elapsed);

  AppUart_Printf("[cam] fps=%lu pub=%lu vsync=%lu esync=%lu eovr=%lu dmaerr=%lu fu=%lu terr=%lu\r\n",
                 (unsigned long)fps,
                 (unsigned long)seq,
                 (unsigned long)s_vsyncCnt,
                 (unsigned long)s_esyncCnt,
                 (unsigned long)s_eovrCnt,
                 (unsigned long)s_dmaErrCnt,
                 (unsigned long)s_ltdcFuCnt,
                 (unsigned long)s_ltdcTerrCnt);

  s_lastStatSeq = seq;
  s_statTick = now;
}

void AppCamera_PollFrame(void)
{
  uint32_t packet;

  if (s_camReady == 0U)
  {
    return;
  }

  packet = s_pubPacket;
  if ((packet >> 2) != s_lastSeq)
  {
    s_lastSeq = packet >> 2;

    if (s_layerOn == 0U)
    {
      /* 首帧写满后才使能图层，避免启动瞬间逐行填图；
       * 之后的换帧由 DMA 行中断在帧完成时直接切换 CFBAR，任务侧不再参与 */
      cam_layer_config(cam_fb_addr((uint8_t)(packet & 0x03U)));
      s_layerOn = 1U;
    }
  }

  cam_poll_dcmi_error();
  cam_poll_ltdc_error();
  cam_print_stats();
}

void AppCamera_GetStats(AppCameraStats_t *stats)
{
  static uint32_t s_fpsValue = 0U;
  static uint32_t s_fpsLastSeq = 0U;
  static uint32_t s_fpsLastTick = 0U;
  uint32_t now;
  uint32_t seq;
  uint32_t dt;

  if (stats == NULL)
  {
    return;
  }

  now = HAL_GetTick();
  seq = s_pubPacket >> 2U;

  if (s_fpsLastTick == 0U)
  {
    s_fpsLastSeq = seq;
    s_fpsLastTick = now;
  }

  /* 0.5s 窗口统计帧率，供左侧面板显示 */
  dt = now - s_fpsLastTick;
  if (dt >= 500U)
  {
    s_fpsValue = ((seq - s_fpsLastSeq) * 1000U) / dt;
    s_fpsLastSeq = seq;
    s_fpsLastTick = now;
  }

  stats->fps = s_fpsValue;
  stats->frames = seq;
  stats->esync = s_esyncCnt;
  stats->eovr = s_eovrCnt;
  stats->dmaerr = s_dmaErrCnt;
  stats->ready = s_camReady;
}
