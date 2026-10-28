#include "pcf8574.h"

#include "camera_util.h"

#include "main.h"

/* 软件 I2C：SCL=PH4、SDA=PH5，均为开漏输出 + 内部上拉（SDA 直接读 IDR） */
#define PCF_PORT        GPIOH
#define PCF_SCL_PIN     GPIO_PIN_4
#define PCF_SDA_PIN     GPIO_PIN_5

#define PCF_SCL_H()     HAL_GPIO_WritePin(PCF_PORT, PCF_SCL_PIN, GPIO_PIN_SET)
#define PCF_SCL_L()     HAL_GPIO_WritePin(PCF_PORT, PCF_SCL_PIN, GPIO_PIN_RESET)
#define PCF_SDA_H()     HAL_GPIO_WritePin(PCF_PORT, PCF_SDA_PIN, GPIO_PIN_SET)
#define PCF_SDA_L()     HAL_GPIO_WritePin(PCF_PORT, PCF_SDA_PIN, GPIO_PIN_RESET)
#define PCF_SDA_READ()  HAL_GPIO_ReadPin(PCF_PORT, PCF_SDA_PIN)

/* 位延时 5us，对应约 100kHz 的 I2C 速率 */
static void PCF_Delay(void)
{
  Camera_DelayUs(5U);
}

static void PCF_Start(void)
{
  PCF_SDA_H();
  PCF_SCL_H();
  PCF_Delay();
  PCF_SDA_L();
  PCF_Delay();
  PCF_SCL_L();
  PCF_Delay();
}

static void PCF_Stop(void)
{
  PCF_SCL_L();
  PCF_Delay();
  PCF_SDA_L();
  PCF_Delay();
  PCF_SCL_H();
  PCF_Delay();
  PCF_SDA_H();
  PCF_Delay();
}

/* 返回 1 表示收到 ACK */
static uint8_t PCF_WriteByte(uint8_t dat)
{
  uint8_t i;
  uint8_t ack;

  for (i = 0U; i < 8U; i++)
  {
    if ((dat & 0x80U) != 0U)
    {
      PCF_SDA_H();
    }
    else
    {
      PCF_SDA_L();
    }
    dat = (uint8_t)(dat << 1U);
    PCF_Delay();
    PCF_SCL_H();
    PCF_Delay();
    PCF_SCL_L();
  }

  /* 第 9 个时钟释放 SDA 读应答 */
  PCF_SDA_H();
  PCF_Delay();
  PCF_SCL_H();
  PCF_Delay();
  ack = (PCF_SDA_READ() == GPIO_PIN_RESET) ? 1U : 0U;
  PCF_SCL_L();
  PCF_Delay();

  return ack;
}

static uint8_t PCF_ReadByte(void)
{
  uint8_t i;
  uint8_t dat = 0U;

  PCF_SDA_H();
  for (i = 0U; i < 8U; i++)
  {
    PCF_Delay();
    PCF_SCL_H();
    dat = (uint8_t)(dat << 1U);
    if (PCF_SDA_READ() != GPIO_PIN_RESET)
    {
      dat |= 0x01U;
    }
    PCF_Delay();
    PCF_SCL_L();
  }

  /* 主机回非应答，结束读 */
  PCF_SDA_H();
  PCF_Delay();
  PCF_SCL_H();
  PCF_Delay();
  PCF_SCL_L();
  PCF_Delay();

  return dat;
}

uint8_t PCF8574_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOH_CLK_ENABLE();

  /* SCL / SDA 都按开漏 + 上拉配置，释放即为高电平 */
  gpio.Pin = PCF_SCL_PIN | PCF_SDA_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(PCF_PORT, &gpio);

  PCF_SDA_H();
  PCF_SCL_H();
  PCF_Delay();

  /* 探测器件：写地址收到 ACK 即认为在位 */
  PCF_Start();
  if (PCF_WriteByte((uint8_t)(PCF8574_ADDR_7BIT << 1U)) == 0U)
  {
    PCF_Stop();
    return 1U;
  }
  PCF_Stop();

  /* 默认输出全高：摄像头 PWDN 保持高电平（掉电） */
  PCF8574_WriteOneByte(0xFFU);

  return 0U;
}

uint8_t PCF8574_ReadOneByte(void)
{
  uint8_t dat;

  PCF_Start();
  (void)PCF_WriteByte((uint8_t)((PCF8574_ADDR_7BIT << 1U) | 0x01U));
  dat = PCF_ReadByte();
  PCF_Stop();

  return dat;
}

void PCF8574_WriteOneByte(uint8_t data)
{
  PCF_Start();
  (void)PCF_WriteByte((uint8_t)(PCF8574_ADDR_7BIT << 1U));
  (void)PCF_WriteByte(data);
  PCF_Stop();

  /* PCF8574 需要一个字节传输完成的时间，留 1ms 余量 */
  Camera_DelayMs(1U);
}

void PCF8574_WriteBit(uint8_t bit, uint8_t sta)
{
  uint8_t data;

  data = PCF8574_ReadOneByte();
  if (sta != 0U)
  {
    data = (uint8_t)(data | (1U << bit));
  }
  else
  {
    data = (uint8_t)(data & ~(1U << bit));
  }
  PCF8574_WriteOneByte(data);
}

uint8_t PCF8574_ReadBit(uint8_t bit)
{
  uint8_t data;

  data = PCF8574_ReadOneByte();
  return ((data & (1U << bit)) != 0U) ? 1U : 0U;
}
