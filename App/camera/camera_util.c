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
  uint32_t second;
  uint32_t ticks;
  uint32_t spins;

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
  second = DWT->CYCCNT;

  if (second != start)
  {
    /* DWT 正常计数 */
    while ((DWT->CYCCNT - start) < ticks)
    {
    }
    return;
  }

  /* DWT 没有在计数（调试器可以清掉 CYCCNTENA，或者 DEMCR.TRCENA 被复位）：
   * 绝不能死等，否则整机卡在传感器初始化里（实测接 SWD 调试器就会发生）。
   * 退回固定轮数的空循环，精度要求不高（SCCB 只要求 >= 1us 量级）。 */
  spins = ticks / 4U;
  while (spins > 0U)
  {
    spins--;
    __NOP();
  }
}

void Camera_DelayMs(uint32_t ms)
{
  HAL_Delay(ms);
}
