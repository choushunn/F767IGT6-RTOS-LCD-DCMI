#include "app_camera.h"

#include "camera_util.h"
#include "ov2640.h"
#include "pcf8574.h"
#include "sccb.h"

#include "dcmi.h"
#include "ltdc.h"
#include "uart/app_uart.h"

/*==============================================================================
 * 数据通路：OV2640 ->（DCMI 8 位并口）-> DMA2_Stream1 -> SDRAM 相机帧缓冲
 *            -> LTDC 图层 2 硬件扫描显示。CPU 不参与任何像素搬运。
 *
 * 一帧 600x600 RGB565 = 720000B = 180000 个 32 位字，而 DMA 的 NDTR 只有
 * 16 位（最大 65535 字），整帧长度写进去会被静默截断，所以必须把一帧拆成
 * 若干块：每块 CAM_LINES_PER_CHUNK 行 = 300*24 = 7200 个 32 位字，一帧
 * CAM_CHUNKS_PER_FRAME = 25 块。
 *
 * 块续排方式：DMA 用 NORMAL 模式，一块传完 EN 由硬件清零，TC 中断里把
 * M0AR/NDTR 指向下一块并重新开流。SWD 实测 FIFO Full + 内存 INCR4 可维持
 * 15fps 级吞吐，而 FIFO 直接模式的吞吐只有约 2–6fps。
 *
 * 曾按"零间隙"思路改成 DBM 双缓冲 + CIRC，在本板不成立：块开始后几十个字
 * 内 FEIF（DMA FIFO 错误）就被反复置位（≈15 万次/秒），传输停滞、DCMI 溢出、
 * 帧不再完成（pub 冻结）。两种 FIFO 配置（1/2 阈值、直接模式）都复现，
 * 因此不使用 DBM；当前方案是 NORMAL 逐块装载。
 *
 * 帧对齐由 DCMI 自身保证：置 DCMI_CR_CAPTURE 后，DCMI 从下一个 VSYNC 开始
 * 采集，第一个字就是第 0 行第 0 个像素。帧结束中断再校验一次帧内块计数，
 * 出现整块丢失/OVR 丢像素导致的错位时请求重排（带冷却时间）。
 *============================================================================*/

/* 一行 600 像素 = 1200B = 300 个 32 位字（DCMI 每个 32 位字给 2 个像素） */
#define CAM_LINE_BYTES          (CAM_IMG_W * 2U)
#define CAM_LINE_WORDS          (CAM_LINE_BYTES / 4U)

/* 每块 24 行：28800B/块，25 块/帧。 */
#define CAM_LINES_PER_CHUNK     24U
#define CAM_CHUNK_WORDS         (CAM_LINE_WORDS * CAM_LINES_PER_CHUNK)
#define CAM_CHUNK_BYTES         (CAM_LINE_BYTES * CAM_LINES_PER_CHUNK)
#define CAM_CHUNKS_PER_FRAME    (CAM_IMG_H / CAM_LINES_PER_CHUNK)

/* 重排冷却：总线拥塞导致 OVR 时避免每个轮询周期都重启采集 */
#define CAM_RESYNC_COOLDOWN_MS  1000U

/* 停流判定：15fps 下一帧 66ms，超过 600ms 没有新帧即认为采集通路已经停死
 * （DMA 流被硬件关闭 / DCMI 不再产生帧结束中断），需要重排恢复 */
#define CAM_FRAME_TIMEOUT_MS    600U

#if (((CAM_IMG_W * 2U) % 4U) != 0U)
#error "一行像素必须是整数个 32 位字（DCMI 按 4 字节给一个数据字）"
#endif
#if ((CAM_IMG_H % CAM_LINES_PER_CHUNK) != 0U)
#error "CAM_IMG_H 必须是 CAM_LINES_PER_CHUNK 的整数倍，否则帧边界不落在块边界上"
#endif
#if ((DISPLAY_LVGL_W + DISPLAY_CAMERA_W) != DISPLAY_PANEL_W)
#error "UI 宽度与摄像头宽度必须正好覆盖面板宽度"
#endif
#if (DISPLAY_CAMERA_H != DISPLAY_PANEL_H)
#error "摄像头高度必须覆盖面板高度"
#endif
#if (CAM_CHUNK_WORDS > 65535U)
#error "单块 DMA 长度超过 NDTR 的 16 位上限"
#endif
#if ((CAM_CHUNK_BYTES & 15U) != 0U)
#error "单块 DMA 字节数必须满足 16 字节突发对齐"
#endif

/* OV2640 原生 4:3 画幅 1600x1200：居中裁出 600x600 直接 1:1 输出。
 * 不缩放（窗口尺寸 == 输出尺寸）的原因：DSP 的 image size（0xC0/0xC1 回读）
 * 只有 800x600，窗口必须 <= 该尺寸；之前用 1152x1152 窗口超出 DSP 能力，
 * 输出是"每行一个静态字节"的无效数据。1:1 也顺带避开所有缩放比例问题。 */
#define CAM_SOURCE_W        1600U
#define CAM_SOURCE_H        1200U
#define CAM_CROP_SRC_W      600U
#define CAM_CROP_SRC_H      600U
#define CAM_CROP_X          ((CAM_SOURCE_W - CAM_CROP_SRC_W) / 2U)   /* 500 */
#define CAM_CROP_Y          ((CAM_SOURCE_H - CAM_CROP_SRC_H) / 2U)   /* 300 */

/* 中断优先级：2 的抢占级高于 RTOS 阈值 5（数值越小越高），
 * 因此 DCMI / DMA ISR 内禁止调用任何 FreeRTOS API */
#define CAM_DMA_IRQ_PRIO    2U
#define CAM_DCMI_IRQ_PRIO   2U

/* 诊断：初始化阶段与失败原因，串口不可用时可直接用调试器读这两个变量
 * （在 map 里查 g_camStage / g_camDmaErr 地址） */
volatile uint32_t g_camStage = 0U;
volatile uint32_t g_camDmaErr = 0U;
/* 诊断：8 根数据线 D0..D7 的高电平占比（0~100）。若全为 0 或 100，
 * 说明数据线没有被传感器驱动（接线/引脚问题）。 */
volatile uint32_t g_dataDuty[8] = {0U};
/* 诊断：两次行中断之间 DMA 前进的字数（600 RGB565 像素应为 300） */
volatile uint32_t g_wordsPerLine = 0U;
volatile uint32_t g_wordsPerLineMax = 0U;   /* 流正常时的最大值（288 = 每像素 2 字节） */
static uint32_t s_lineNdtr = 0U;
static uint8_t s_lineNdtrValid = 0U;

static DMA_HandleTypeDef s_hdmaCam;

/* 发布包：bit[1:0] = 帧缓冲序号(0..2)，其余位 = 累计发布帧号；ISR 单字写入 */
static volatile uint32_t s_pubPacket = 0U;
static volatile uint32_t s_doneChunks = 0U;   /* 自本次排流起已完成的 DMA 块数 */
static volatile uint32_t s_vsyncCnt = 0U;
static volatile uint32_t s_esyncCnt = 0U;
static volatile uint32_t s_eovrCnt = 0U;
static volatile uint32_t s_dmaErrCnt = 0U;
static volatile uint32_t s_desyncCnt = 0U;    /* 帧同步校验失败 / 强制重排次数 */
static volatile uint32_t s_ltdcFuCnt = 0U;    /* LTDC FIFO 下溢次数（带宽不足的迹象） */
static volatile uint32_t s_ltdcTerrCnt = 0U;  /* LTDC 取数传输错误次数 */
static volatile uint32_t s_resyncReq = 0U;    /* ISR 请求重排采集流，由任务执行 */
static volatile uint32_t s_dmaErrFlags = 0U;  /* 首次 DMA 错误的 LISR 快照（诊断用） */
static volatile uint32_t s_dmaErrCr = 0U;     /* 首次错误时的 DMA2_Stream1->CR */
static volatile uint32_t s_dmaErrNdtr = 0U;   /* 首次错误时的 DMA2_Stream1->NDTR */
static volatile uint32_t s_dmaErrDcmi = 0U;   /* 首次错误时的 DCMI->RISR */
static volatile uint32_t s_feCnt = 0U;        /* DMA FIFO 错误累计（只诊断，不重排） */
static volatile uint32_t s_dcmiVsyncCnt = 0U; /* DCMI 收到的 VSYNC 边沿数（诊断） */
static volatile uint32_t s_dcmiLineCnt = 0U;  /* DCMI 收到的行同步数（诊断） */
static volatile uint32_t s_lineIsrCnt = 0U;   /* 行中断计数（测真实行数/帧） */
static volatile uint32_t s_linesPerFrame = 0U;/* 上一帧实际行数（行中断统计） */
static volatile uint32_t s_framesIrq = 0U;    /* 完成的帧数（行中断统计口径） */
static volatile uint32_t s_tcTotal = 0U;      /* 累计块完成数（不随重排清零） */
static volatile uint8_t s_fbBase = 0U;        /* 块序号 0 对应的帧缓冲序号 */
static volatile uint8_t s_dispFb = 0U;        /* LTDC 当前扫描的帧缓冲序号 */
static volatile uint8_t s_layerOn = 0U;

static uint8_t s_camReady = 0U;
static uint32_t s_lastSeq = 0U;
static uint32_t s_lastStatSeq = 0U;
static uint32_t s_statTick = 0U;
static uint32_t s_lastResyncTick = 0U;
static uint32_t s_lastPubTick = 0U;

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

/* 线性块序号 -> SDRAM 地址。块序号从 0 连续递增，每 CAM_CHUNKS_PER_FRAME 块
 * 换一块帧缓冲（三块轮换），帧内行号由块内偏移决定。 */
static uint32_t cam_chunk_addr(uint32_t seq)
{
  uint32_t fb = ((uint32_t)s_fbBase + (seq / CAM_CHUNKS_PER_FRAME)) % CAM_FB_NUM;
  uint32_t row = (seq % CAM_CHUNKS_PER_FRAME) * CAM_LINES_PER_CHUNK;

  return cam_fb_addr((uint8_t)fb) + (row * CAM_LINE_BYTES);
}

/*=========================== 采集流的启停 ===========================*/

/* 停止采集：先停 DCMI 再停 DMA，并清掉本流的全部挂起标志。
 * 调用者必须保证 DMA2_Stream1 / DCMI 中断不会在这个过程里重入。 */
static void cam_stream_stop(void)
{
  DCMI->CR &= ~(DCMI_CR_CAPTURE | DCMI_CR_ENABLE);

  DMA2_Stream1->CR &= ~DMA_SxCR_EN;
  while ((DMA2_Stream1->CR & DMA_SxCR_EN) != 0U)
  {
    /* 等当前突发结束（几个 AHB 周期），EN 由硬件清零 */
  }

  DMA2->LIFCR = DMA_FLAG_FEIF0_4 | DMA_FLAG_DMEIF0_4 | DMA_FLAG_TEIF0_4 |
                DMA_FLAG_HTIF0_4 | DMA_FLAG_TCIF0_4 |
                DMA_FLAG_FEIF1_5 | DMA_FLAG_DMEIF1_5 | DMA_FLAG_TEIF1_5 |
                DMA_FLAG_HTIF1_5 | DMA_FLAG_TCIF1_5;
}

/* 装载第一块并开流：DMA 使用 NORMAL 模式。
 * 每块 TC 中断里立即把 M0AR/NDTR 指向下一块并重新使能，只有寄存器重装，
 * 不在块边界重新配置 DCMI。历史实机日志中该方式可保持 pub==vsync、eovr=0；
 * 本板的 DBM+CIRC 组合会在 NDTR 尚未耗尽时反复产生 FEIF 并冻结采集。 */
static void cam_stream_arm(void)
{
  DMA2_Stream1->CR &= ~DMA_SxCR_EN;
  while ((DMA2_Stream1->CR & DMA_SxCR_EN) != 0U)
  {
  }

  /* 禁用双缓冲并强制下一次传输从 M0AR 开始。 */
  DMA2_Stream1->CR &= ~(DMA_SxCR_DBM | DMA_SxCR_CT);
  DMA2_Stream1->PAR = (uint32_t)&DCMI->DR;
  DMA2_Stream1->M0AR = cam_chunk_addr(0U);
  DMA2_Stream1->NDTR = CAM_CHUNK_WORDS;

  s_doneChunks = 0U;

  DMA2_Stream1->CR |= DMA_SxCR_EN;

  /* 使能 DCMI 并开始捕获：DCMI 从下一个 VSYNC 起把第 0 行第 0 个像素
   * 送进 DR，因此块 0 的第一行就是图像第一行 */
  __HAL_DCMI_ENABLE(&hdcmi);
  DCMI->CR |= DCMI_CR_CAPTURE;
}

/*=========================== 中断服务（不调用 RTOS API） ===========================*/

/* 一块 DMA 传输完成：立即续排下一块，满一帧则发布并换层 */
static void cam_on_chunk_done(void)
{
  uint32_t done = s_doneChunks + 1U;   /* 含刚完成的这一块 */

  s_doneChunks = done;

  /* NORMAL 模式下 TC 后硬件已清除 EN。先续排，避免等待统计和图层操作。 */
  DMA2_Stream1->M0AR = cam_chunk_addr(done);
  DMA2_Stream1->NDTR = CAM_CHUNK_WORDS;
  DMA2_Stream1->CR |= DMA_SxCR_EN;

  if ((done % CAM_CHUNKS_PER_FRAME) == 0U)
  {
    uint32_t frame = done / CAM_CHUNKS_PER_FRAME;    /* 自本次排流起的帧号(1起) */
    uint8_t fb = (uint8_t)(((uint32_t)s_fbBase + frame - 1U) % CAM_FB_NUM);
    uint32_t seq = (s_pubPacket >> 2) + 1U;

    s_dispFb = fb;
    s_pubPacket = (seq << 2) | (uint32_t)fb;
    s_lastPubTick = HAL_GetTick();   /* 供任务侧停流看门狗判断 */
    __DSB();

    /* 帧刚写满即刻切扫描地址（只改 CFBAR + VBR，垂直消隐期生效）。
     * 若等任务轮询再切，DMA 早已开始写下一块，LTDC 仍在扫旧帧会撕裂。 */
    if (s_layerOn != 0U)
    {
      LTDC_Layer2->CFBAR = cam_fb_addr(fb);
      __HAL_LTDC_VERTICAL_BLANKING_RELOAD_CONFIG(&hltdc);
    }
  }
}

/* DCMI 行中断：统计一帧的真实行数（核对传感器输出几何） */
void HAL_DCMI_LineEventCallback(DCMI_HandleTypeDef *hdcmi)
{
  uint32_t now;
  uint32_t delta;

  (void)hdcmi;
  s_lineIsrCnt++;

  /* 用 NDTR 差值测每行字数；600 像素 RGB565 应为 300 字。 */
  now = DMA2_Stream1->NDTR;
  if (s_lineNdtrValid != 0U)
  {
    if (now <= s_lineNdtr)
    {
      delta = s_lineNdtr - now;
    }
    else
    {
      delta = 0U;                    /* NDTR 重装（块边界），丢弃 */
    }
    if ((delta > 0U) && (delta < 2048U))
    {
      g_wordsPerLine = delta;
      if (delta > g_wordsPerLineMax)
      {
        g_wordsPerLineMax = delta;
      }
    }
  }
  s_lineNdtr = now;
  s_lineNdtrValid = 1U;
}

/* DCMI 帧结束（VSYNC）：帧计数 + 行数快照 + 帧对齐校验 */
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
  uint32_t inFrame;

  /* HAL 已在 IRQHandler 里清标志并关掉帧中断，这里必须重新打开，
   * 否则只能收到一次帧中断 */
  __HAL_DCMI_CLEAR_FLAG(hdcmi, DCMI_FLAG_FRAMERI);
  __HAL_DCMI_ENABLE_IT(hdcmi, DCMI_IT_FRAME);

  s_vsyncCnt++;

  /* 记录本帧行数（行中断计数），用于核对 576/600/1200 等真实几何 */
  s_linesPerFrame = s_lineIsrCnt;
  s_lineIsrCnt = 0U;
  s_framesIrq++;

  /* 正常情况：VSYNC 到来时刚好发完最后一块（帧内计数归 0）。
   * 允许 CAM_CHUNKS_PER_FRAME-1：最后一块的 TC 可能压在 DMA FIFO 排空之后，
   * 即帧中断先到、块中断后到。其它值说明整块丢失或丢像素，采集流已错位。 */
  inFrame = s_doneChunks % CAM_CHUNKS_PER_FRAME;
  if ((inFrame != 0U) && (inFrame != (CAM_CHUNKS_PER_FRAME - 1U)))
  {
    s_desyncCnt++;
    s_resyncReq = 1U;
  }
}

void DCMI_IRQHandler(void)
{
  HAL_DCMI_IRQHandler(&hdcmi);
}

/* DMA2_Stream1：块完成中断只做"续排下一块 + 帧发布"，不搬运像素 */
void DMA2_Stream1_IRQHandler(void)
{
  uint32_t flags = DMA2->LISR & (DMA_FLAG_FEIF1_5 | DMA_FLAG_DMEIF1_5 |
                                 DMA_FLAG_TEIF1_5 | DMA_FLAG_HTIF1_5 |
                                 DMA_FLAG_TCIF1_5);

  if (flags == 0U)
  {
    return;
  }

  DMA2->LIFCR = flags;                 /* W1C：只清本次出现的标志 */

  if ((flags & DMA_FLAG_TCIF1_5) != 0U)
  {
    s_tcTotal++;
    cam_on_chunk_done();
  }

  if ((flags & (DMA_FLAG_TEIF1_5 | DMA_FLAG_DMEIF1_5)) != 0U)
  {
    /* 真正的总线/直接模式错误：计数并请求重排（FIFO 错误会让硬件关流） */
    s_dmaErrCnt++;
    if (s_dmaErrFlags == 0U)
    {
      s_dmaErrFlags = flags;
      s_dmaErrCr = DMA2_Stream1->CR;
      s_dmaErrNdtr = DMA2_Stream1->NDTR;
      s_dmaErrDcmi = DCMI->RISR;
    }
    s_resyncReq = 1U;
  }

  if ((flags & DMA_FLAG_FEIF1_5) != 0U)
  {
    /* FIFO 错误只计数：中断未使能，出现时顺带清掉，不作为重排依据 */
    s_feCnt++;
  }
}

/*=========================== 初始化 ===========================*/

/* 诊断：用 TIM3_CH1 输入捕获测 PA6(DCMI_PIXCLK) 的实际频率。
 * TIM3 在 APB1，定时器时钟 108MHz，预分频 0 → 1 计数 = 9.26ns。 */
static uint32_t cam_measure_pclk_hz(void)
{
  GPIO_InitTypeDef gpio = {0};
  uint32_t t1;
  uint32_t t2;
  uint32_t guard;

  gpio.Pin = GPIO_PIN_6;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF2_TIM3;     /* PA6 = TIM3_CH1 */
  HAL_GPIO_Init(GPIOA, &gpio);

  __HAL_RCC_TIM3_CLK_ENABLE();
  TIM3->CR1 = 0U;
  TIM3->PSC = 0U;
  TIM3->ARR = 0xFFFFFFFFU;
  TIM3->CCMR1 = 0x01U;               /* CC1S=01: IC1 映射到 TI1 */
  TIM3->CCER = TIM_CCER_CC1E;        /* 上升沿捕获 */
  TIM3->SR = 0U;
  TIM3->CR1 = TIM_CR1_CEN;

  guard = 0U;
  while (((TIM3->SR & TIM_SR_CC1IF) == 0U) && (++guard < 2000000U)) { }
  t1 = TIM3->CCR1;
  TIM3->SR = ~TIM_SR_CC1IF;
  guard = 0U;
  while (((TIM3->SR & TIM_SR_CC1IF) == 0U) && (++guard < 2000000U)) { }
  t2 = TIM3->CCR1;

  TIM3->CR1 = 0U;
  TIM3->SR = 0U;

  gpio.Alternate = GPIO_AF13_DCMI;   /* 恢复 DCMI 复用 */
  HAL_GPIO_Init(GPIOA, &gpio);

  if ((t2 <= t1) || ((t2 - t1) == 0U))
  {
    return 0U;                       /* 没有捕获到边沿 */
  }
  return 108000000U / (t2 - t1);
}

/* 诊断：把 PH8(HREF)/PB7(VSYNC) 临时切成 GPIO 输入采样，
 * 判断同步脚到底有没有在翻转。返回高电平占比（0~100）。 */
static uint32_t cam_probe_pin(GPIO_TypeDef *port, uint16_t pin)
{
  GPIO_InitTypeDef gpio = {0};
  uint32_t i;
  uint32_t high = 0U;
  const uint32_t samples = 200000U;

  gpio.Pin = pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(port, &gpio);

  for (i = 0U; i < samples; i++)
  {
    if (HAL_GPIO_ReadPin(port, pin) != GPIO_PIN_RESET)
    {
      high++;
    }
  }

  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Alternate = GPIO_AF13_DCMI;
  HAL_GPIO_Init(port, &gpio);

  return (high * 100U) / samples;
}

/* DCMI 运行时重配：上升沿采样、8 位并口、只开帧结束中断 */
static void cam_dcmi_config(void)
{
  __HAL_DCMI_DISABLE(&hdcmi);

  hdcmi.Init.SynchroMode = DCMI_SYNCHRO_HARDWARE;
  /* PCK 上升沿采样（RISING/FALLING 都实测过：帧缓冲同样出现"每个 32 位字
   * 4 个相同字节"，故采样边沿不是根因，保持标准配置）。 */
  hdcmi.Init.PCKPolarity = DCMI_PCKPOLARITY_RISING;
  /* 同步极性：匹配正点原子 OV2640 参考配置。SWD 连续读取 DCMI_DR 时，
   * HSYNC=LOW 得到 AD36AD36 这类两个独立 16 位像素；HIGH 得到
   * BBBBBBBB 这类四个相同字节，故 HSYNC 必须低有效。 */
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

  /* 只关 VSYNC/ERR/OVR（HAL 在 OVR/ERR 中断里会 Abort DMA）。
   * LINE 中断保留：用它统计"一帧到底有多少行"，用来核对传感器真实输出几何。 */
  __HAL_DCMI_DISABLE_IT(&hdcmi, DCMI_IT_VSYNC | DCMI_IT_ERR | DCMI_IT_OVR);
  __HAL_DCMI_CLEAR_FLAG(&hdcmi,
                        DCMI_FLAG_FRAMERI | DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI | DCMI_FLAG_VSYNCRI);
  __HAL_DCMI_ENABLE_IT(&hdcmi, DCMI_IT_FRAME | DCMI_IT_LINE);

  HAL_NVIC_SetPriority(DCMI_IRQn, CAM_DCMI_IRQ_PRIO, 0U);
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

/* DMA 静态配置：仅设置通道/方向/宽度/优先级/FIFO/突发，
 * 传输长度与目标地址在 cam_stream_arm() 里按块排 */
static uint8_t cam_dma_config(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();

  s_hdmaCam.Instance = DMA2_Stream1;
  s_hdmaCam.Init.Channel = DMA_CHANNEL_1;
  s_hdmaCam.Init.Direction = DMA_PERIPH_TO_MEMORY;
  s_hdmaCam.Init.PeriphInc = DMA_PINC_DISABLE;
  s_hdmaCam.Init.MemInc = DMA_MINC_ENABLE;
  s_hdmaCam.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  s_hdmaCam.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  s_hdmaCam.Init.Mode = DMA_NORMAL;                    /* TC 中断逐块续排 M0AR/NDTR */
  s_hdmaCam.Init.Priority = DMA_PRIORITY_HIGH;         /* 与 LTDC/LVGL 抢 SDRAM 时不被饿死 */
  /* FIFO Full + 内存 INCR4：SWD 实测 2 秒完成 696 个 24 行块（约 14.5fps）；
   * 原直接模式仅 2–6fps，且 FEIF 持续增长。WORD 数据宽度下 HAL 只允许
   * FIFO Full 配合 INC4/INC8。 */
  s_hdmaCam.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
  s_hdmaCam.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
  s_hdmaCam.Init.MemBurst = DMA_MBURST_INC4;
  s_hdmaCam.Init.PeriphBurst = DMA_PBURST_SINGLE;

  HAL_DMA_DeInit(&s_hdmaCam);
  if (HAL_DMA_Init(&s_hdmaCam) != HAL_OK)
  {
    g_camDmaErr = (uint32_t)s_hdmaCam.ErrorCode + 1U;
    return 1U;
  }
  g_camDmaErr = 0U;

  /* HAL 句柄只需给 DCMI 兜底（HAL_DCMI_IRQHandler 的错误分支会用到），
   * 启停全部走寄存器，保证块边界不出现"关流重装"的空档 */
  __HAL_LINKDMA(&hdcmi, DMA_Handle, s_hdmaCam);
  __HAL_UNLOCK(&s_hdmaCam);

  /* 只开 TC（每块到点续排）和 TE（总线传输错误）。
   * 不打开 FE 中断；正常 FIFO 排空由硬件完成，TE/DME 才视为真实错误。 */
  __HAL_DMA_ENABLE_IT(&s_hdmaCam, DMA_IT_TC | DMA_IT_TE);

  HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, CAM_DMA_IRQ_PRIO, 0U);

  return 0U;
}

/* 采集启动（调度器启动前调用一次） */
static uint8_t cam_capture_start(void)
{
  if (cam_dma_config() != 0U)
  {
    return 1U;
  }

  cam_stream_stop();

  s_pubPacket = 0U;
  s_vsyncCnt = 0U;
  s_esyncCnt = 0U;
  s_eovrCnt = 0U;
  s_dmaErrCnt = 0U;
  s_desyncCnt = 0U;
  s_resyncReq = 0U;
  s_fbBase = 0U;
  s_dispFb = 0U;
  s_layerOn = 0U;

  cam_stream_arm();

  NVIC_ClearPendingIRQ(DCMI_IRQn);
  NVIC_ClearPendingIRQ(DMA2_Stream1_IRQn);
  HAL_NVIC_EnableIRQ(DCMI_IRQn);
  HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);

  s_lastPubTick = HAL_GetTick();

  return 0U;
}

/* 运行期重排：采集流错位（整块丢失或 OVR 丢像素）后重新与 VSYNC 对齐。
 * 重排会丢一帧，故调用点带冷却时间。任务上下文调用。 */
static void cam_capture_restart(void)
{
  /* 这里不能用 taskENTER_CRITICAL()：DCMI/DMA 的抢占优先级是 2，
   * 高于 RTOS 阈值 5，临界区屏蔽不住，必须直接关 NVIC 通道 */
  NVIC_DisableIRQ(DMA2_Stream1_IRQn);
  NVIC_DisableIRQ(DCMI_IRQn);

  cam_stream_stop();

  /* 换一块帧缓冲重新开始，避免写坏 LTDC 正在扫描的那块 */
  s_fbBase = (uint8_t)(((uint32_t)s_dispFb + 1U) % CAM_FB_NUM);

  cam_stream_arm();

  NVIC_ClearPendingIRQ(DMA2_Stream1_IRQn);
  NVIC_ClearPendingIRQ(DCMI_IRQn);
  NVIC_EnableIRQ(DCMI_IRQn);
  NVIC_EnableIRQ(DMA2_Stream1_IRQn);

  s_lastPubTick = HAL_GetTick();
}

/* 传感器上电、复位与 SCCB 配置；任一环节失败返回 1 */
static uint8_t cam_sensor_init(void)
{
  uint8_t result;

  /* PCF8574 先就位，并保持 PWDN 高电平（掉电） */
  if (PCF8574_Init() != 0U)
  {
    AppUart_Printf("[cam] pcf8574 not found\r\n");
    return 1U;
  }
  OV2640_PWDN_Set(1U);

  /* 复位时序：RST 拉低 20ms -> PWDN 清零上电 -> 5ms -> 释放 RST -> 20ms */
  OV2640_RST_Init();
  OV2640_RST_Set(0U);
  Camera_DelayMs(20U);
  OV2640_PWDN_Set(0U);
  Camera_DelayMs(5U);
  OV2640_RST_Set(1U);
  Camera_DelayMs(20U);

  SCCB_Init();
  Camera_DelayMs(5U);

  result = OV2640_Init();
  if (result != 0U)
  {
    AppUart_Printf("[cam] ov2640 init failed, code=%u\r\n", (unsigned)result);
    return 1U;
  }
  AppUart_Printf("[cam] ov2640 id ok\r\n");

  OV2640_RGB565_Mode();
  result = OV2640_ImageWin_Set(CAM_CROP_X,
                               CAM_CROP_Y,
                               CAM_CROP_SRC_W,
                               CAM_CROP_SRC_H);
  if (result == 0U)
  {
    result = OV2640_OutSize_Set(CAM_IMG_W, CAM_IMG_H);
  }
  if (result != 0U)
  {
    AppUart_Printf("[cam] ov2640 output config failed, code=%u\r\n",
                   (unsigned)result);
    return 1U;
  }
  OV2640_SetMirrorFlip(CAM_MIRROR_H, CAM_FLIP_V);
  AppUart_Printf("[cam] mirror=%u flip=%u\r\n",
                 (unsigned)CAM_MIRROR_H,
                 (unsigned)CAM_FLIP_V);
  AppUart_Printf("[cam] sensor out=%ux%u crop=%ux%u offset=%u,%u\r\n",
                 (unsigned)CAM_IMG_W,
                 (unsigned)CAM_IMG_H,
                 (unsigned)CAM_CROP_SRC_W,
                 (unsigned)CAM_CROP_SRC_H,
                 (unsigned)CAM_CROP_X,
                 (unsigned)CAM_CROP_Y);

  /* 回读 DSP 窗口/输出尺寸寄存器，确认缩放设置真的生效 */
  {
    uint8_t rb_51, rb_52, rb_53, rb_54, rb_55, rb_57, rb_5a, rb_5b, rb_5c;
    uint8_t rb_12, rb_17, rb_18, rb_19, rb_1a, rb_da;
    uint8_t rb_c0, rb_c1, rb_8c, rb_05, rb_11, rb_d3;

    (void)OV2640_WR_Reg(0xFFU, 0x00U);
    rb_51 = OV2640_RD_Reg(0x51U);
    rb_52 = OV2640_RD_Reg(0x52U);
    rb_53 = OV2640_RD_Reg(0x53U);
    rb_54 = OV2640_RD_Reg(0x54U);
    rb_55 = OV2640_RD_Reg(0x55U);
    rb_57 = OV2640_RD_Reg(0x57U);
    rb_5a = OV2640_RD_Reg(0x5AU);
    rb_5b = OV2640_RD_Reg(0x5BU);
    rb_5c = OV2640_RD_Reg(0x5CU);
    rb_da = OV2640_RD_Reg(0xDAU);
    rb_c0 = OV2640_RD_Reg(0xC0U);
    rb_c1 = OV2640_RD_Reg(0xC1U);
    rb_8c = OV2640_RD_Reg(0x8CU);
    rb_05 = OV2640_RD_Reg(0x05U);
    rb_11 = OV2640_RD_Reg(0x11U);
    rb_d3 = OV2640_RD_Reg(0xD3U);
    (void)OV2640_WR_Reg(0xFFU, 0x01U);
    rb_12 = OV2640_RD_Reg(0x12U);
    rb_17 = OV2640_RD_Reg(0x17U);
    rb_18 = OV2640_RD_Reg(0x18U);
    rb_19 = OV2640_RD_Reg(0x19U);
    rb_1a = OV2640_RD_Reg(0x1AU);
    (void)OV2640_WR_Reg(0xFFU, 0x00U);
    AppUart_Printf("[cam] rb win %02X %02X %02X %02X %02X %02X  out %02X %02X %02X  "
                   "da %02X  com7 %02X hst %02X/%02X vst %02X/%02X\r\n",
                   (unsigned)rb_51, (unsigned)rb_52, (unsigned)rb_53, (unsigned)rb_54,
                   (unsigned)rb_55, (unsigned)rb_57, (unsigned)rb_5a, (unsigned)rb_5b,
                   (unsigned)rb_5c, (unsigned)rb_da, (unsigned)rb_12, (unsigned)rb_17,
                   (unsigned)rb_18, (unsigned)rb_19, (unsigned)rb_1a);
    AppUart_Printf("[cam] rb dsp hsize=%u vsize=%u ctrl0=%02X bypass=%02X "
                   "clkrc=%02X pll=%02X\r\n",
                   (unsigned)(((uint32_t)rb_c0 * 4U) + 0U),
                   (unsigned)(((uint32_t)rb_c1 * 4U) + 0U),
                   (unsigned)rb_8c, (unsigned)rb_05, (unsigned)rb_11, (unsigned)rb_d3);
  }

  return 0U;
}

void AppCamera_Init(void)
{
  s_camReady = 0U;
  g_camStage = 1U;

  if (cam_sensor_init() != 0U)
  {
    g_camStage = 2U;                 /* 传感器/SCCB 失败 */
    return;
  }
  g_camStage = 3U;

  cam_dcmi_config();
  g_camStage = 4U;

  AppUart_Printf("[cam] probe pclk=%luHz href(PH8)=%lu%% vsync(PB7)=%lu%%\r\n",
                 (unsigned long)cam_measure_pclk_hz(),
                 (unsigned long)cam_probe_pin(GPIOH, GPIO_PIN_8),
                 (unsigned long)cam_probe_pin(GPIOB, GPIO_PIN_7));

  /* 逐根测数据线占空比（D0..D7 = PC6,PC7,PC8,PC9,PC11,PD3,PB8,PB9） */
  g_dataDuty[0] = cam_probe_pin(GPIOC, GPIO_PIN_6);
  g_dataDuty[1] = cam_probe_pin(GPIOC, GPIO_PIN_7);
  g_dataDuty[2] = cam_probe_pin(GPIOC, GPIO_PIN_8);
  g_dataDuty[3] = cam_probe_pin(GPIOC, GPIO_PIN_9);
  g_dataDuty[4] = cam_probe_pin(GPIOC, GPIO_PIN_11);
  g_dataDuty[5] = cam_probe_pin(GPIOD, GPIO_PIN_3);
  g_dataDuty[6] = cam_probe_pin(GPIOB, GPIO_PIN_8);
  g_dataDuty[7] = cam_probe_pin(GPIOB, GPIO_PIN_9);
  g_camStage = 5U;

  cam_gpio_boost();
  g_camStage = 6U;

  if (cam_capture_start() != 0U)
  {
    /* 常见原因：HAL_DMA_Init 参数被拒（FIFO 阈值/突发组合）、DMA 时钟未使能 */
    g_camStage = 7U;                 /* DMA 配置失败 */
    AppUart_Printf("[cam] dma start failed, hal_err=0x%08lX\r\n",
                   (unsigned long)s_hdmaCam.ErrorCode);
    return;
  }
  g_camStage = 8U;
  AppUart_Printf("[cam] capture started, chunk=%lu words x %lu per frame\r\n",
                 (unsigned long)CAM_CHUNK_WORDS,
                 (unsigned long)CAM_CHUNKS_PER_FRAME);

  s_lastStatSeq = 0U;
  s_statTick = HAL_GetTick();
  s_lastResyncTick = s_statTick;
  s_camReady = 1U;

  AppUart_Printf("[cam] init done\r\n");
}

/*=========================== 显示与统计 ===========================*/

/* 顶层混合因子必须与 ltdc.c 一致：PAxCA + Alpha0=0，
 * 图层窗口外的默认色 alpha=0 才会透明（CA 模式下不读 Alpha0，会盖黑左栏） */
static void cam_layer_config(uint32_t fbAddr)
{
  LTDC_LayerCfgTypeDef cfg = {0};

  cfg.WindowX0 = DISPLAY_CAMERA_X0;
  cfg.WindowX1 = DISPLAY_CAMERA_X1;
  cfg.WindowY0 = DISPLAY_CAMERA_Y0;
  cfg.WindowY1 = DISPLAY_CAMERA_Y1;
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

  /* 层寄存器是影子寄存器：HAL_LTDC_ConfigLayer 不置 LEN，
   * 必须显式使能图层并触发重载，否则右栏永远是背景色 */
  __HAL_LTDC_LAYER_ENABLE(&hltdc, LTDC_LAYER_2);
  __HAL_LTDC_VERTICAL_BLANKING_RELOAD_CONFIG(&hltdc);
}

/* OVR/ERR 只读标志计数，不开中断 */
static void cam_poll_dcmi_error(void)
{
  uint32_t risr = DCMI->RISR;

  /* VSYNC / LINE 中断虽然没开，标志位仍会置位：清掉并计数，
   * 用它们换算传感器真实的帧率和行同步频率（诊断输出几何用） */
  if ((risr & DCMI_FLAG_VSYNCRI) != 0U)
  {
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_VSYNCRI);
    s_dcmiVsyncCnt++;
  }
  if ((risr & DCMI_FLAG_LINERI) != 0U)
  {
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_LINERI);
    s_dcmiLineCnt++;
  }

  /* 直接读 RISR：本工程 HAL 的 __HAL_DCMI_GET_FLAG 仍访问旧寄存器名 RIS/MIS */
  if ((risr & DCMI_FLAG_ERRRI) != 0U)
  {
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_ERRRI);
    s_esyncCnt++;
    /* 同步错误说明 HSYNC/VSYNC 与数据流已经对不上，必须重新对齐 */
    s_resyncReq = 1U;
  }
  if ((risr & DCMI_FLAG_OVRRI) != 0U)
  {
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI);
    s_eovrCnt++;
    /* OVR 表示 FIFO 满导致像素被丢弃，后续像素相对 SDRAM 行地址已经错位，
     * 这一帧及之后的帧都不能再用，请求重排 */
    s_resyncReq = 1U;
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

/* 每 2s 打印一次：帧率以 pub（发布帧数）为准 */
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

  AppUart_Printf("[cam] fps=%lu pub=%lu vsync=%lu desync=%lu esync=%lu eovr=%lu "
                 "dmaerr=%lu fu=%lu terr=%lu\r\n",
                 (unsigned long)fps,
                 (unsigned long)seq,
                 (unsigned long)s_vsyncCnt,
                 (unsigned long)s_desyncCnt,
                 (unsigned long)s_esyncCnt,
                 (unsigned long)s_eovrCnt,
                 (unsigned long)s_dmaErrCnt,
                 (unsigned long)s_ltdcFuCnt,
                 (unsigned long)s_ltdcTerrCnt);
  AppUart_Printf("[cam] diag tc=%lu chunk=%lu lines/f=%lu frames=%lu fe=%lu dv=%lu dl=%lu "
                 "sr=0x%lX dcmi=0x%lX dma_cr=0x%lX ndtr=%lu\r\n",
                 (unsigned long)s_tcTotal,
                 (unsigned long)s_doneChunks,
                 (unsigned long)s_linesPerFrame,
                 (unsigned long)s_framesIrq,
                 (unsigned long)s_feCnt,
                 (unsigned long)s_dcmiVsyncCnt,
                 (unsigned long)s_dcmiLineCnt,
                 (unsigned long)DCMI->SR,
                 (unsigned long)DCMI->RISR,
                 (unsigned long)DMA2_Stream1->CR,
                 (unsigned long)DMA2_Stream1->NDTR);

  s_lastStatSeq = seq;
  s_statTick = now;
}

void AppCamera_PollFrame(void)
{
  uint32_t packet;
  uint32_t now;

  if (s_camReady == 0U)
  {
    return;
  }

  now = HAL_GetTick();

  /* 兜底看门狗：长时间没有新帧说明采集通路已经停死
   * （DMA 流被硬件关闭 / DCMI 不再产生帧结束中断），直接请求重排 */
  if ((uint32_t)(now - s_lastPubTick) >= CAM_FRAME_TIMEOUT_MS)
  {
    s_resyncReq = 1U;
  }

  /* ISR/看门狗请求重排（带冷却，避免总线长期拥塞时反复重启） */
  if (s_resyncReq != 0U)
  {
    if ((now - s_lastResyncTick) >= CAM_RESYNC_COOLDOWN_MS)
    {
      s_resyncReq = 0U;
      s_lastResyncTick = now;
      cam_capture_restart();
    }
  }

  packet = s_pubPacket;
  if ((packet >> 2) != s_lastSeq)
  {
    s_lastSeq = packet >> 2;

    if (s_layerOn == 0U)
    {
      /* 首帧写满后才使能图层，避免启动瞬间逐行填图；
       * 之后的换帧由 DMA 块中断在帧完成时直接切换 CFBAR */
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
  stats->desync = s_desyncCnt;
  stats->ready = s_camReady;
}
