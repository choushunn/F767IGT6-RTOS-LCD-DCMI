#include "touch_detect.h"

#include "touch_pins.h"

#include "main.h"
#include "display_config.h"

/* GT9xxx 寄存器与地址 */
#define GT9XXX_ADDR_A          0x14U   /* INT 为高时的地址 */
#define GT9XXX_ADDR_B          0x5DU   /* INT 为低时的地址 */

#define GT9XXX_REG_PID         0x8140U /* 产品 ID（4 字节 ASCII） */
#define GT9XXX_REG_CTRL        0x8040U /* 控制寄存器（软复位） */
#define GT9XXX_REG_STATUS      0x814EU /* 触点状态 */
#define GT9XXX_REG_POINT1      0x8150U /* 第 1 触点坐标 */

/* 面板分辨率（与 LVGL/LTDC 一致） */
#define TOUCH_PANEL_W          DISPLAY_PANEL_W
#define TOUCH_PANEL_H          DISPLAY_PANEL_H

static uint8_t s_touchOk = 0U;
static uint8_t s_touchAddr = GT9XXX_ADDR_B;

/*=========================== 软件 I2C ===========================*/

static void Touch_I2C_Delay(void)
{
  volatile uint32_t i;

  for (i = 0U; i < 200U; i++)
  {
  }
}

#define TOUCH_SCL_H()     HAL_GPIO_WritePin(TOUCH_SCL_PORT, TOUCH_SCL_PIN, GPIO_PIN_SET)
#define TOUCH_SCL_L()     HAL_GPIO_WritePin(TOUCH_SCL_PORT, TOUCH_SCL_PIN, GPIO_PIN_RESET)
#define TOUCH_SDA_H()     HAL_GPIO_WritePin(TOUCH_SDA_PORT, TOUCH_SDA_PIN, GPIO_PIN_SET)
#define TOUCH_SDA_L()     HAL_GPIO_WritePin(TOUCH_SDA_PORT, TOUCH_SDA_PIN, GPIO_PIN_RESET)
#define TOUCH_SDA_READ()  HAL_GPIO_ReadPin(TOUCH_SDA_PORT, TOUCH_SDA_PIN)

static void Touch_SdaInput(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = TOUCH_SDA_PIN;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(TOUCH_SDA_PORT, &gpio);
}

static void Touch_SdaOutput(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = TOUCH_SDA_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(TOUCH_SDA_PORT, &gpio);
}

static void Touch_I2C_Start(void)
{
  Touch_SdaOutput();
  TOUCH_SDA_H();
  TOUCH_SCL_H();
  Touch_I2C_Delay();
  TOUCH_SDA_L();
  Touch_I2C_Delay();
  TOUCH_SCL_L();
  Touch_I2C_Delay();
}

static void Touch_I2C_Stop(void)
{
  Touch_SdaOutput();
  TOUCH_SCL_L();
  Touch_I2C_Delay();
  TOUCH_SDA_L();
  Touch_I2C_Delay();
  TOUCH_SCL_H();
  Touch_I2C_Delay();
  TOUCH_SDA_H();
  Touch_I2C_Delay();
}

/* 返回 1 表示收到 ACK */
static uint8_t Touch_I2C_WriteByte(uint8_t data)
{
  uint8_t i;
  uint8_t ack;

  Touch_SdaOutput();
  for (i = 0U; i < 8U; i++)
  {
    if ((data & 0x80U) != 0U)
    {
      TOUCH_SDA_H();
    }
    else
    {
      TOUCH_SDA_L();
    }
    data = (uint8_t)(data << 1U);
    Touch_I2C_Delay();
    TOUCH_SCL_H();
    Touch_I2C_Delay();
    TOUCH_SCL_L();
    Touch_I2C_Delay();
  }

  /* 第 9 个时钟读从机 ACK */
  Touch_SdaInput();
  Touch_I2C_Delay();
  TOUCH_SCL_H();
  Touch_I2C_Delay();
  ack = (TOUCH_SDA_READ() == GPIO_PIN_RESET) ? 1U : 0U;
  TOUCH_SCL_L();
  Touch_I2C_Delay();

  return ack;
}

/* ack=1 主机回 ACK，继续读；ack=0 回 NACK，结束 */
static uint8_t Touch_I2C_ReadByte(uint8_t ack)
{
  uint8_t i;
  uint8_t data = 0U;

  Touch_SdaInput();
  for (i = 0U; i < 8U; i++)
  {
    data = (uint8_t)(data << 1U);
    Touch_I2C_Delay();
    TOUCH_SCL_H();
    Touch_I2C_Delay();
    if (TOUCH_SDA_READ() != GPIO_PIN_RESET)
    {
      data |= 0x01U;
    }
    TOUCH_SCL_L();
    Touch_I2C_Delay();
  }

  Touch_SdaOutput();
  if (ack != 0U)
  {
    TOUCH_SDA_L();
  }
  else
  {
    TOUCH_SDA_H();
  }
  Touch_I2C_Delay();
  TOUCH_SCL_H();
  Touch_I2C_Delay();
  TOUCH_SCL_L();
  Touch_I2C_Delay();

  return data;
}

/* 16 位寄存器地址读 */
static uint8_t Touch_ReadReg16(uint8_t addr, uint16_t reg, uint8_t *buf, uint16_t len)
{
  uint16_t i;

  Touch_I2C_Start();
  if (Touch_I2C_WriteByte((uint8_t)(addr << 1U)) == 0U)
  {
    Touch_I2C_Stop();
    return 0U;
  }
  if (Touch_I2C_WriteByte((uint8_t)(reg >> 8U)) == 0U)
  {
    Touch_I2C_Stop();
    return 0U;
  }
  if (Touch_I2C_WriteByte((uint8_t)(reg & 0xFFU)) == 0U)
  {
    Touch_I2C_Stop();
    return 0U;
  }

  Touch_I2C_Start();
  if (Touch_I2C_WriteByte((uint8_t)((addr << 1U) | 0x01U)) == 0U)
  {
    Touch_I2C_Stop();
    return 0U;
  }

  for (i = 0U; i < len; i++)
  {
    buf[i] = Touch_I2C_ReadByte((((uint16_t)(i + 1U)) < len) ? 1U : 0U);
  }

  Touch_I2C_Stop();
  return 1U;
}

/* 16 位寄存器地址写单字节 */
static uint8_t Touch_WriteReg16(uint8_t addr, uint16_t reg, uint8_t value)
{
  Touch_I2C_Start();
  if (Touch_I2C_WriteByte((uint8_t)(addr << 1U)) == 0U)
  {
    Touch_I2C_Stop();
    return 0U;
  }
  if (Touch_I2C_WriteByte((uint8_t)(reg >> 8U)) == 0U)
  {
    Touch_I2C_Stop();
    return 0U;
  }
  if (Touch_I2C_WriteByte((uint8_t)(reg & 0xFFU)) == 0U)
  {
    Touch_I2C_Stop();
    return 0U;
  }
  if (Touch_I2C_WriteByte(value) == 0U)
  {
    Touch_I2C_Stop();
    return 0U;
  }

  Touch_I2C_Stop();
  return 1U;
}

/*=========================== 探测与扫描 ===========================*/

uint8_t Touch_Detect(void)
{
  GPIO_InitTypeDef gpio = {0};
  uint8_t pid[4] = {0};

  s_touchOk = 0U;

  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOI_CLK_ENABLE();

  /* SCL：开漏输出 + 内部上拉 */
  gpio.Pin = TOUCH_SCL_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(TOUCH_SCL_PORT, &gpio);

  /* SDA：开漏输出 + 内部上拉 */
  Touch_SdaOutput();
  TOUCH_SCL_H();
  TOUCH_SDA_H();

  /* RST / INT 推挽输出，先拉低锁地址 0x5D */
  gpio.Pin = TOUCH_RST_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(TOUCH_RST_PORT, &gpio);
  HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_RESET);

  gpio.Pin = TOUCH_INT_PIN;
  HAL_GPIO_Init(TOUCH_INT_PORT, &gpio);
  HAL_GPIO_WritePin(TOUCH_INT_PORT, TOUCH_INT_PIN, GPIO_PIN_RESET);

  HAL_Delay(10U);
  HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_SET);
  HAL_Delay(120U); /* GT9xxx 复位后需要 >=100ms 才能响应 I2C */

  /* INT 释放为输入 */
  gpio.Pin = TOUCH_INT_PIN;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(TOUCH_INT_PORT, &gpio);

  /* 两个候选地址依次探测产品 ID */
  s_touchAddr = GT9XXX_ADDR_B;
  if (Touch_ReadReg16(s_touchAddr, GT9XXX_REG_PID, pid, 4U) == 0U)
  {
    s_touchAddr = GT9XXX_ADDR_A;
    if (Touch_ReadReg16(s_touchAddr, GT9XXX_REG_PID, pid, 4U) == 0U)
    {
      return 0U;
    }
  }

  /* 仅接受 GT9xxx 家族的产品 ID（GT911/9147/9271 以 "91" 开头，GT1151 以 "11" 开头） */
  if (!(((pid[0] == '9') && (pid[1] == '1')) || ((pid[0] == '1') && (pid[1] == '1'))))
  {
    return 0U;
  }

  /* 官方软复位序列：0x8040 写 0x02 -> 10ms -> 0x00 */
  (void)Touch_WriteReg16(s_touchAddr, GT9XXX_REG_CTRL, 0x02U);
  HAL_Delay(10U);
  (void)Touch_WriteReg16(s_touchAddr, GT9XXX_REG_CTRL, 0x00U);
  HAL_Delay(10U);

  s_touchOk = 1U;
  return 1U;
}

uint8_t Touch_IsReady(void)
{
  return s_touchOk;
}

uint8_t Touch_Scan(uint16_t *x, uint16_t *y)
{
  uint8_t status = 0U;
  uint8_t buf[4] = {0};

  if (s_touchOk == 0U)
  {
    return 0U;
  }

  if (Touch_ReadReg16(s_touchAddr, GT9XXX_REG_STATUS, &status, 1U) == 0U)
  {
    return 0U;
  }

  if ((status & 0x80U) == 0U)
  {
    return 0U; /* 坐标未就绪 */
  }

  if ((status & 0x0FU) != 0U)
  {
    if (Touch_ReadReg16(s_touchAddr, GT9XXX_REG_POINT1, buf, 4U) != 0U)
    {
      uint16_t rawX = (uint16_t)(((uint16_t)buf[1] << 8U) | (uint16_t)buf[0]);
      uint16_t rawY = (uint16_t)(((uint16_t)buf[3] << 8U) | (uint16_t)buf[2]);

      /* 面板坐标与 1024x600 横屏显示同向，直接透传并钳位 */
      *x = (rawX < TOUCH_PANEL_W) ? rawX : (TOUCH_PANEL_W - 1U);
      *y = (rawY < TOUCH_PANEL_H) ? rawY : (TOUCH_PANEL_H - 1U);

      (void)Touch_WriteReg16(s_touchAddr, GT9XXX_REG_STATUS, 0x00U);
      return 1U;
    }
  }

  /* 清除坐标就绪标志，避免重复读旧帧 */
  (void)Touch_WriteReg16(s_touchAddr, GT9XXX_REG_STATUS, 0x00U);
  return 0U;
}
