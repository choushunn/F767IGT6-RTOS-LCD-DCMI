#include "ov2640.h"

#include "ov2640cfg.h"

#include "camera_util.h"
#include "pcf8574.h"
#include "sccb.h"

#include "main.h"

#define OV2640_BANK_SENSOR      0x01U
#define OV2640_BANK_DSP         0x00U

static void ov2640_write_table(const uint8_t table[][2], uint16_t count)
{
  uint16_t i;

  for (i = 0U; i < count; i++)
  {
    (void)OV2640_WR_Reg(table[i][0], table[i][1]);
  }
}

void OV2640_RST_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();

  gpio.Pin = GPIO_PIN_15;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio);
}

void OV2640_RST_Set(uint8_t sta)
{
  HAL_GPIO_WritePin(GPIOA,
                    GPIO_PIN_15,
                    (sta != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void OV2640_PWDN_Set(uint8_t sta)
{
  PCF8574_WriteBit(DCMI_PWDN_IO, sta);
}

uint8_t OV2640_WR_Reg(uint8_t reg, uint8_t data)
{
  uint8_t res = 0U;

  SCCB_Start();
  if (SCCB_WR_Byte(OV2640_ADDR) != 0U)
  {
    res = 1U;
  }
  if (SCCB_WR_Byte(reg) != 0U)
  {
    res = 1U;
  }
  if (SCCB_WR_Byte(data) != 0U)
  {
    res = 1U;
  }
  SCCB_Stop();

  return res;
}

uint8_t OV2640_RD_Reg(uint8_t reg)
{
  uint8_t val;

  SCCB_Start();
  (void)SCCB_WR_Byte(OV2640_ADDR);
  (void)SCCB_WR_Byte(reg);
  SCCB_Stop();

  SCCB_Start();
  (void)SCCB_WR_Byte((uint8_t)(OV2640_ADDR | 0x01U));
  val = SCCB_RD_Byte();
  SCCB_No_Ack();
  SCCB_Stop();

  return val;
}

uint8_t OV2640_Init(void)
{
  uint16_t id;
  uint16_t count;

  (void)OV2640_WR_Reg(0xFFU, OV2640_BANK_SENSOR);

  id = (uint16_t)OV2640_RD_Reg(OV2640_SENSOR_MIDH);
  id = (uint16_t)(id << 8U);
  id |= (uint16_t)OV2640_RD_Reg(OV2640_SENSOR_MIDL);
  if (id != OV2640_MID)
  {
    return 1U;
  }

  id = (uint16_t)OV2640_RD_Reg(OV2640_SENSOR_PIDH);
  id = (uint16_t)(id << 8U);
  id |= (uint16_t)OV2640_RD_Reg(OV2640_SENSOR_PIDL);
  if (id != OV2640_PID)
  {
    return 2U;
  }

  count = (uint16_t)(sizeof(ov2640_uxga_init_reg_tbl) /
                     sizeof(ov2640_uxga_init_reg_tbl[0]));
  ov2640_write_table(ov2640_uxga_init_reg_tbl, count);

  /* Allow the AEC/AWB loops to settle before DMA starts. */
  Camera_DelayMs(200U);

  return 0U;
}

void OV2640_RGB565_Mode(void)
{
  (void)OV2640_WR_Reg(0xFFU, OV2640_BANK_DSP);
  /* 0xDA=0x09：RGB565 + 低字节先输出。
   * STM32 DCMI 在 8 位模式下把先到的字节放在 DCMI_DR 的 LSB，正点原子
   * OV2640 RGB565 参考序列同样使用 0x09；写成 0x08（高字节先）会把每个
   * RGB565 像素的高低字节对调，颜色异常但不是图像几何问题的根因。 */
  (void)OV2640_WR_Reg(0xDAU, 0x09U);
  (void)OV2640_WR_Reg(0xD7U, 0x03U);
  (void)OV2640_WR_Reg(0xDFU, 0x02U);
  (void)OV2640_WR_Reg(0x33U, 0xA0U);
  (void)OV2640_WR_Reg(0x3CU, 0x00U);
  (void)OV2640_WR_Reg(0xE1U, 0x67U);
  (void)OV2640_WR_Reg(0x05U, 0x00U);
}

uint8_t OV2640_ImageWin_Set(uint16_t offx,
                            uint16_t offy,
                            uint16_t width,
                            uint16_t height)
{
  uint16_t hsize;
  uint16_t vsize;
  uint8_t high;

  if ((width & 0x03U) != 0U)
  {
    return 1U;
  }
  if ((height & 0x03U) != 0U)
  {
    return 2U;
  }

  hsize = (uint16_t)(width >> 2U);
  vsize = (uint16_t)(height >> 2U);

  (void)OV2640_WR_Reg(0xFFU, OV2640_BANK_DSP);
  (void)OV2640_WR_Reg(0xE0U, 0x04U);
  (void)OV2640_WR_Reg(0x51U, (uint8_t)(hsize & 0xFFU));
  (void)OV2640_WR_Reg(0x52U, (uint8_t)(vsize & 0xFFU));
  (void)OV2640_WR_Reg(0x53U, (uint8_t)(offx & 0xFFU));
  (void)OV2640_WR_Reg(0x54U, (uint8_t)(offy & 0xFFU));

  high = (uint8_t)((vsize >> 1U) & 0x80U);
  high |= (uint8_t)((offy >> 4U) & 0x70U);
  high |= (uint8_t)((hsize >> 5U) & 0x08U);
  high |= (uint8_t)((offx >> 8U) & 0x07U);
  (void)OV2640_WR_Reg(0x55U, high);
  (void)OV2640_WR_Reg(0x57U, (uint8_t)((hsize >> 2U) & 0x80U));
  (void)OV2640_WR_Reg(0xE0U, 0x00U);

  return 0U;
}

uint8_t OV2640_OutSize_Set(uint16_t width, uint16_t height)
{
  uint16_t outw;
  uint16_t outh;
  uint8_t high;

  if ((width & 0x03U) != 0U)
  {
    return 1U;
  }
  if ((height & 0x03U) != 0U)
  {
    return 2U;
  }

  outw = (uint16_t)(width >> 2U);
  outh = (uint16_t)(height >> 2U);

  (void)OV2640_WR_Reg(0xFFU, OV2640_BANK_DSP);
  (void)OV2640_WR_Reg(0xE0U, 0x04U);
  (void)OV2640_WR_Reg(0x5AU, (uint8_t)(outw & 0xFFU));
  (void)OV2640_WR_Reg(0x5BU, (uint8_t)(outh & 0xFFU));

  high = (uint8_t)((outw >> 8U) & 0x03U);
  high |= (uint8_t)((outh >> 6U) & 0x04U);
  (void)OV2640_WR_Reg(0x5CU, high);
  (void)OV2640_WR_Reg(0xE0U, 0x00U);

  return 0U;
}

void OV2640_SetMirrorFlip(uint8_t mirror, uint8_t flip)
{
  uint8_t reg;

  (void)OV2640_WR_Reg(0xFFU, OV2640_BANK_SENSOR);
  reg = OV2640_RD_Reg(0x04U);
  reg = (uint8_t)(reg & (uint8_t)~0xC0U);
  if (mirror != 0U)
  {
    reg = (uint8_t)(reg | 0x80U);
  }
  if (flip != 0U)
  {
    reg = (uint8_t)(reg | 0x40U);
  }
  (void)OV2640_WR_Reg(0x04U, reg);
  (void)OV2640_WR_Reg(0xFFU, OV2640_BANK_DSP);
}
