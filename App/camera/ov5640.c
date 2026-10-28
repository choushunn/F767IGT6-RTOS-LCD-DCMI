#include "ov5640.h"

/* 寄存器表由官方例程原样复制，一维写法会触发 -Wmissing-braces，这里局部屏蔽 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include "ov5640cfg.h"
#pragma clang diagnostic pop

#include "camera_util.h"
#include "pcf8574.h"
#include "sccb.h"

#include "main.h"

/* 复位脚：PA15（JTAG 引脚，本工程用 SWD 调试可安全复用） */
void OV5640_RST_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();

  gpio.Pin = GPIO_PIN_15;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio);
}

void OV5640_RST_Set(uint8_t sta)
{
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, (sta != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void OV5640_PWDN_Set(uint8_t sta)
{
  PCF8574_WriteBit(DCMI_PWDN_IO, sta);
}

/* 写一个寄存器：SCCB 三个字节（器件地址 + 寄存器地址高/低 + 数据） */
uint8_t OV5640_WR_Reg(uint16_t reg, uint8_t data)
{
  uint8_t res = 0U;

  SCCB_Start();
  if (SCCB_WR_Byte(OV5640_ADDR) != 0U)
  {
    res = 1U;
  }
  if (SCCB_WR_Byte((uint8_t)(reg >> 8)) != 0U)
  {
    res = 1U;
  }
  if (SCCB_WR_Byte((uint8_t)(reg & 0xFFU)) != 0U)
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

/* 读一个寄存器：先写寄存器地址，再重新起始读一个字节 */
uint8_t OV5640_RD_Reg(uint16_t reg)
{
  uint8_t val;

  SCCB_Start();
  (void)SCCB_WR_Byte(OV5640_ADDR);
  (void)SCCB_WR_Byte((uint8_t)(reg >> 8));
  (void)SCCB_WR_Byte((uint8_t)(reg & 0xFFU));
  SCCB_Stop();

  SCCB_Start();
  (void)SCCB_WR_Byte((uint8_t)(OV5640_ADDR | 0x01U));
  val = SCCB_RD_Byte();
  SCCB_No_Ack();
  SCCB_Stop();

  return val;
}

/* 读 ID 并校验，然后写入 UXGA 初始化表（复位与上电时序由调用方完成） */
uint8_t OV5640_Init(void)
{
  uint16_t i;
  uint16_t id;

  id = (uint16_t)OV5640_RD_Reg(OV5640_CHIPIDH);
  id = (uint16_t)(id << 8);
  id |= (uint16_t)OV5640_RD_Reg(OV5640_CHIPIDL);
  if (id != OV5640_ID)
  {
    return 1U;
  }

  (void)OV5640_WR_Reg(0x3103, 0x11);  /* 系统时钟取自 PAD */
  (void)OV5640_WR_Reg(0x3008, 0x82);  /* 软复位 */
  Camera_DelayMs(10U);

  for (i = 0U; i < (sizeof(ov5640_uxga_init_reg_tbl) / sizeof(ov5640_uxga_init_reg_tbl[0])); i++)
  {
    (void)OV5640_WR_Reg(ov5640_uxga_init_reg_tbl[i][0], (uint8_t)ov5640_uxga_init_reg_tbl[i][1]);
  }

  /* 闪光灯自检：打开再关闭，确认控制通路正常 */
  OV5640_Flash_Ctrl(1U);
  Camera_DelayMs(50U);
  OV5640_Flash_Ctrl(0U);

  return 0U;
}

/* 切换为 RGB565 输出（DVP 并口） */
void OV5640_RGB565_Mode(void)
{
  uint16_t i;

  for (i = 0U; i < (sizeof(ov5640_rgb565_reg_tbl) / sizeof(ov5640_rgb565_reg_tbl[0])); i++)
  {
    (void)OV5640_WR_Reg(ov5640_rgb565_reg_tbl[i][0], (uint8_t)ov5640_rgb565_reg_tbl[i][1]);
  }
}

/* 灯光模式参数表：自动 / 日光 / 办公室 / 阴天 / 室内 */
static const uint8_t OV5640_LIGHTMODE_TBL[5][7] =
{
  { 0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00 },
  { 0x06, 0x1C, 0x04, 0x00, 0x04, 0xF3, 0x01 },
  { 0x05, 0x48, 0x04, 0x00, 0x07, 0xCF, 0x01 },
  { 0x06, 0x48, 0x04, 0x00, 0x04, 0xD3, 0x01 },
  { 0x04, 0x10, 0x04, 0x00, 0x08, 0x40, 0x01 },
};

void OV5640_Light_Mode(uint8_t mode)
{
  uint8_t i;

  (void)OV5640_WR_Reg(0x3212, 0x03);  /* 组 3 开始 */
  for (i = 0U; i < 7U; i++)
  {
    (void)OV5640_WR_Reg((uint16_t)(0x3400U + i), OV5640_LIGHTMODE_TBL[mode][i]);
  }
  (void)OV5640_WR_Reg(0x3212, 0x13);  /* 组 3 结束 */
  (void)OV5640_WR_Reg(0x3212, 0xA3);  /* 启动组 3 */
}

/* 饱和度参数表：-3 ~ +3 共 7 级 */
static const uint8_t OV5640_SATURATION_TBL[7][6] =
{
  { 0x0C, 0x30, 0x3D, 0x3E, 0x3D, 0x01 },
  { 0x10, 0x3D, 0x4D, 0x4E, 0x4D, 0x01 },
  { 0x15, 0x52, 0x66, 0x68, 0x66, 0x02 },
  { 0x1A, 0x66, 0x80, 0x82, 0x80, 0x02 },
  { 0x1F, 0x7A, 0x9A, 0x9C, 0x9A, 0x02 },
  { 0x24, 0x8F, 0xB3, 0xB6, 0xB3, 0x03 },
  { 0x2B, 0xAB, 0xD6, 0xDA, 0xD6, 0x04 },
};

void OV5640_Color_Saturation(uint8_t sat)
{
  uint8_t i;

  (void)OV5640_WR_Reg(0x3212, 0x03);
  (void)OV5640_WR_Reg(0x5381, 0x1C);
  (void)OV5640_WR_Reg(0x5382, 0x5A);
  (void)OV5640_WR_Reg(0x5383, 0x06);
  for (i = 0U; i < 6U; i++)
  {
    (void)OV5640_WR_Reg((uint16_t)(0x5384U + i), OV5640_SATURATION_TBL[sat][i]);
  }
  (void)OV5640_WR_Reg(0x538B, 0x98);
  (void)OV5640_WR_Reg(0x538A, 0x01);
  (void)OV5640_WR_Reg(0x3212, 0x13);
  (void)OV5640_WR_Reg(0x3212, 0xA3);
}

/* 亮度：0~8 对应 -4 ~ +4 */
void OV5640_Brightness(uint8_t bright)
{
  uint8_t brtval;

  if (bright < 4U)
  {
    brtval = (uint8_t)(4U - bright);
  }
  else
  {
    brtval = (uint8_t)(bright - 4U);
  }

  (void)OV5640_WR_Reg(0x3212, 0x03);
  (void)OV5640_WR_Reg(0x5587, (uint8_t)(brtval << 4));
  if (bright < 4U)
  {
    (void)OV5640_WR_Reg(0x5588, 0x09);
  }
  else
  {
    (void)OV5640_WR_Reg(0x5588, 0x01);
  }
  (void)OV5640_WR_Reg(0x3212, 0x13);
  (void)OV5640_WR_Reg(0x3212, 0xA3);
}

/* 对比度：0~6 对应 -3 ~ +3 */
void OV5640_Contrast(uint8_t contrast)
{
  uint8_t reg0val = 0x00;
  uint8_t reg1val = 0x20;

  switch (contrast)
  {
    case 0U:
      reg0val = 0x14;
      reg1val = 0x14;
      break;
    case 1U:
      reg0val = 0x18;
      reg1val = 0x18;
      break;
    case 2U:
      reg0val = 0x1C;
      reg1val = 0x1C;
      break;
    case 4U:
      reg0val = 0x10;
      reg1val = 0x24;
      break;
    case 5U:
      reg0val = 0x18;
      reg1val = 0x28;
      break;
    case 6U:
      reg0val = 0x1C;
      reg1val = 0x2C;
      break;
    default:
      break;
  }

  (void)OV5640_WR_Reg(0x3212, 0x03);
  (void)OV5640_WR_Reg(0x5585, reg0val);
  (void)OV5640_WR_Reg(0x5586, reg1val);
  (void)OV5640_WR_Reg(0x3212, 0x13);
  (void)OV5640_WR_Reg(0x3212, 0xA3);
}

/* 锐度：小于 33 为固定锐度，33 为自动 */
void OV5640_Sharpness(uint8_t sharp)
{
  if (sharp < 33U)
  {
    (void)OV5640_WR_Reg(0x5308, 0x65);
    (void)OV5640_WR_Reg(0x5302, sharp);
  }
  else
  {
    (void)OV5640_WR_Reg(0x5308, 0x25);
    (void)OV5640_WR_Reg(0x5300, 0x08);
    (void)OV5640_WR_Reg(0x5301, 0x30);
    (void)OV5640_WR_Reg(0x5302, 0x10);
    (void)OV5640_WR_Reg(0x5303, 0x00);
    (void)OV5640_WR_Reg(0x5309, 0x08);
    (void)OV5640_WR_Reg(0x530A, 0x30);
    (void)OV5640_WR_Reg(0x530B, 0x04);
    (void)OV5640_WR_Reg(0x530C, 0x06);
  }
}

/* 特效参数表：正常 / 冷色 / 暖色 / 黑白 / 泛黄 / 反色 / 偏绿 */
static const uint8_t OV5640_EFFECTS_TBL[7][3] =
{
  { 0x06, 0x40, 0x10 },
  { 0x1E, 0xA0, 0x40 },
  { 0x1E, 0x80, 0xC0 },
  { 0x1E, 0x80, 0x80 },
  { 0x1E, 0x40, 0xA0 },
  { 0x40, 0x40, 0x10 },
  { 0x1E, 0x60, 0x60 },
};

void OV5640_Special_Effects(uint8_t eft)
{
  (void)OV5640_WR_Reg(0x3212, 0x03);
  (void)OV5640_WR_Reg(0x5580, OV5640_EFFECTS_TBL[eft][0]);
  (void)OV5640_WR_Reg(0x5583, OV5640_EFFECTS_TBL[eft][1]);
  (void)OV5640_WR_Reg(0x5584, OV5640_EFFECTS_TBL[eft][2]);
  (void)OV5640_WR_Reg(0x5003, 0x08);
  (void)OV5640_WR_Reg(0x3212, 0x13);
  (void)OV5640_WR_Reg(0x3212, 0xA3);
}

/* 测试图案：0 关闭，1 彩条，2 色块 */
void OV5640_Test_Pattern(uint8_t mode)
{
  if (mode == 0U)
  {
    (void)OV5640_WR_Reg(0x503D, 0x00);
  }
  else if (mode == 1U)
  {
    (void)OV5640_WR_Reg(0x503D, 0x80);
  }
  else if (mode == 2U)
  {
    (void)OV5640_WR_Reg(0x503D, 0x82);
  }
  else
  {
    /* 其余模式不做处理 */
  }
}

/* 闪光灯：1 打开，0 关闭 */
void OV5640_Flash_Ctrl(uint8_t sw)
{
  (void)OV5640_WR_Reg(0x3016, 0x02);
  (void)OV5640_WR_Reg(0x301C, 0x02);
  if (sw != 0U)
  {
    (void)OV5640_WR_Reg(0x3019, 0x02);
  }
  else
  {
    (void)OV5640_WR_Reg(0x3019, 0x00);
  }
}

/* 设置图像方向。OV5640 的 0x3820/0x3821 用 bit[2:1] 控制 ISP 翻转/镜像，
 * 具体方向取决于模组装配，由上层按实测结果配置 */
void OV5640_SetMirrorFlip(uint8_t mirror, uint8_t flip)
{
  uint8_t reg;

  reg = OV5640_RD_Reg(0x3821);
  reg = (uint8_t)(reg & (uint8_t)~0x06U);
  if (mirror != 0U)
  {
    reg = (uint8_t)(reg | 0x06U);
  }
  (void)OV5640_WR_Reg(0x3821, reg);

  reg = OV5640_RD_Reg(0x3820);
  reg = (uint8_t)(reg & (uint8_t)~0x06U);
  if (flip != 0U)
  {
    reg = (uint8_t)(reg | 0x06U);
  }
  (void)OV5640_WR_Reg(0x3820, reg);
}

/* 设置输出尺寸与取图偏移。
 * 不使用 0x3212 组 3 包裹：该方式要等下一帧才真正生效，紧随其后的回读仍是旧值，
 * 本板 DVP RGB565 实测未生效（回读仍是 1280x720），这里直接写 DVPHO/DVPVO 与偏移寄存器。 */
uint8_t OV5640_OutSize_Set(uint16_t offx, uint16_t offy, uint16_t width, uint16_t height)
{
  (void)OV5640_WR_Reg(0x3808, (uint8_t)(width >> 8));
  (void)OV5640_WR_Reg(0x3809, (uint8_t)(width & 0xFFU));
  (void)OV5640_WR_Reg(0x380A, (uint8_t)(height >> 8));
  (void)OV5640_WR_Reg(0x380B, (uint8_t)(height & 0xFFU));
  (void)OV5640_WR_Reg(0x3810, (uint8_t)(offx >> 8));
  (void)OV5640_WR_Reg(0x3811, (uint8_t)(offx & 0xFFU));
  (void)OV5640_WR_Reg(0x3812, (uint8_t)(offy >> 8));
  (void)OV5640_WR_Reg(0x3813, (uint8_t)(offy & 0xFFU));

  return 0U;
}

/* 设置 ISP 取图窗口（最大 2592x1944），一般不调用 */
uint8_t OV5640_ImageWin_Set(uint16_t offx, uint16_t offy, uint16_t width, uint16_t height)
{
  uint16_t xst;
  uint16_t yst;
  uint16_t xend;
  uint16_t yend;

  xst = offx;
  yst = offy;
  xend = (uint16_t)(offx + width - 1U);
  yend = (uint16_t)(offy + height - 1U);

  (void)OV5640_WR_Reg(0x3212, 0x03);
  (void)OV5640_WR_Reg(0x3800, (uint8_t)(xst >> 8));
  (void)OV5640_WR_Reg(0x3801, (uint8_t)(xst & 0xFFU));
  (void)OV5640_WR_Reg(0x3802, (uint8_t)(yst >> 8));
  (void)OV5640_WR_Reg(0x3803, (uint8_t)(yst & 0xFFU));
  (void)OV5640_WR_Reg(0x3804, (uint8_t)(xend >> 8));
  (void)OV5640_WR_Reg(0x3805, (uint8_t)(xend & 0xFFU));
  (void)OV5640_WR_Reg(0x3806, (uint8_t)(yend >> 8));
  (void)OV5640_WR_Reg(0x3807, (uint8_t)(yend & 0xFFU));
  (void)OV5640_WR_Reg(0x3212, 0x13);
  (void)OV5640_WR_Reg(0x3212, 0xA3);

  return 0U;
}
