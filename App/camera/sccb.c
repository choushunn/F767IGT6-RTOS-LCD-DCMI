#include "sccb.h"

#include "camera_util.h"

#include "main.h"

/* SCCB 引脚：SCL=PB4（推挽输出）、SDA=PB3（开漏输出 + 上拉，可切输入读 ACK） */
#define SCCB_PORT        GPIOB
#define SCCB_SCL_PIN     GPIO_PIN_4
#define SCCB_SDA_PIN     GPIO_PIN_3

#define SCCB_SCL_H()     HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, GPIO_PIN_SET)
#define SCCB_SCL_L()     HAL_GPIO_WritePin(SCCB_PORT, SCCB_SCL_PIN, GPIO_PIN_RESET)
#define SCCB_SDA_H()     HAL_GPIO_WritePin(SCCB_PORT, SCCB_SDA_PIN, GPIO_PIN_SET)
#define SCCB_SDA_L()     HAL_GPIO_WritePin(SCCB_PORT, SCCB_SDA_PIN, GPIO_PIN_RESET)
#define SCCB_SDA_READ()  HAL_GPIO_ReadPin(SCCB_PORT, SCCB_SDA_PIN)

/* SCCB 位延时 5us，符合 OV 系列 400kHz 以内的时序要求 */
static void SCCB_Delay(void)
{
  Camera_DelayUs(5U);
}

/* SDA 切为输入（上拉），用于读应答与读数据 */
static void SCCB_SdaInput(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = SCCB_SDA_PIN;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(SCCB_PORT, &gpio);
}

/* SDA 切为开漏输出，主机只拉低不主动拉高 */
static void SCCB_SdaOutput(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = SCCB_SDA_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(SCCB_PORT, &gpio);
}

void SCCB_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* SCL：推挽输出，上拉 */
  gpio.Pin = SCCB_SCL_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(SCCB_PORT, &gpio);

  /* SDA：开漏输出，上拉（可读回电平） */
  SCCB_SdaOutput();

  SCCB_SCL_H();
  SCCB_SDA_H();
}

/* 起始：SCL 为高时 SDA 由高变低 */
void SCCB_Start(void)
{
  SCCB_SdaOutput();
  SCCB_SDA_H();
  SCCB_SCL_H();
  SCCB_Delay();
  SCCB_SDA_L();
  SCCB_Delay();
  SCCB_SCL_L();
  SCCB_Delay();
}

/* 停止：SCL 为高时 SDA 由低变高 */
void SCCB_Stop(void)
{
  SCCB_SdaOutput();
  SCCB_SDA_L();
  SCCB_Delay();
  SCCB_SCL_H();
  SCCB_Delay();
  SCCB_SDA_H();
  SCCB_Delay();
}

/* 主机发送非应答（NA），表示读操作结束 */
void SCCB_No_Ack(void)
{
  SCCB_SdaOutput();
  SCCB_Delay();
  SCCB_SDA_H();
  SCCB_SCL_H();
  SCCB_Delay();
  SCCB_SCL_L();
  SCCB_Delay();
  SCCB_SDA_L();
  SCCB_Delay();
}

uint8_t SCCB_WR_Byte(uint8_t dat)
{
  uint8_t i;
  uint8_t res;

  SCCB_SdaOutput();

  for (i = 0U; i < 8U; i++)
  {
    if ((dat & 0x80U) != 0U)
    {
      SCCB_SDA_H();
    }
    else
    {
      SCCB_SDA_L();
    }
    dat = (uint8_t)(dat << 1U);
    SCCB_Delay();
    SCCB_SCL_H();
    SCCB_Delay();
    SCCB_SCL_L();
  }

  /* 第 9 个时钟读从机应答：SDA 为低表示 ACK */
  SCCB_SdaInput();
  SCCB_Delay();
  SCCB_SCL_H();
  SCCB_Delay();
  res = (SCCB_SDA_READ() == GPIO_PIN_RESET) ? 0U : 1U;
  SCCB_SCL_L();
  SCCB_SdaOutput();

  return res;
}

uint8_t SCCB_RD_Byte(void)
{
  uint8_t i;
  uint8_t dat = 0U;

  SCCB_SdaInput();

  for (i = 0U; i < 8U; i++)
  {
    SCCB_Delay();
    SCCB_SCL_H();
    dat = (uint8_t)(dat << 1U);
    if (SCCB_SDA_READ() != GPIO_PIN_RESET)
    {
      dat |= 0x01U;
    }
    SCCB_Delay();
    SCCB_SCL_L();
  }

  SCCB_SdaOutput();

  return dat;
}
