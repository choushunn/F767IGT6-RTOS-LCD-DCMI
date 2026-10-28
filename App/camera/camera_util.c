#include "camera_util.h"

#include "main.h"

/* 1 表示 DWT 周期计数器已打开 */
static uint8_t s_dwtReady = 0U;

/* 打开 DWT 周期计数器（Cortex-M7 的 DWT 需要先置 DEMCR.TRCENA） */
static void Camera_DwtInit(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  s_dwtReady = 1U;
}

void Camera_DelayUs(uint32_t us)
{
  uint32_t start;
  uint32_t ticks;

  if (us == 0U)
  {
    return;
  }

  if (s_dwtReady == 0U)
  {
    Camera_DwtInit();
  }

  /* 按当前内核主频换算成周期数；差值比较天然兼容 CYCCNT 回绕 */
  ticks = us * (SystemCoreClock / 1000000U);
  start = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < ticks)
  {
  }
}

void Camera_DelayMs(uint32_t ms)
{
  HAL_Delay(ms);
}
