#ifndef OV5640_H
#define OV5640_H

#include <stdint.h>

/* 芯片 ID 与 SCCB 从机地址（8 位写地址，7 位 0x3C） */
#define OV5640_ID             0x5640U
#define OV5640_ADDR           0x78U

#define OV5640_CHIPIDH        0x300AU  /* 芯片 ID 高字节 */
#define OV5640_CHIPIDL        0x300BU  /* 芯片 ID 低字节 */

/* 复位脚 PA15：1 释放复位，0 保持复位 */
void OV5640_RST_Init(void);
void OV5640_RST_Set(uint8_t sta);

/* 掉电控制经 PCF8574 的 P2：1 掉电，0 上电 */
void OV5640_PWDN_Set(uint8_t sta);

uint8_t OV5640_WR_Reg(uint16_t reg, uint8_t data);
uint8_t OV5640_RD_Reg(uint16_t reg);

/* 图像方向：mirror=1 水平镜像，flip=1 垂直翻转（写 0x3821 / 0x3820 的 bit[2:1]） */
void OV5640_SetMirrorFlip(uint8_t mirror, uint8_t flip);

/* 初始化：读 ID 校验 + 写 UXGA 初始化表；返回 0 成功，1 ID 错误 */
uint8_t OV5640_Init(void);

void OV5640_RGB565_Mode(void);
void OV5640_Light_Mode(uint8_t mode);
void OV5640_Color_Saturation(uint8_t sat);
void OV5640_Brightness(uint8_t bright);
void OV5640_Contrast(uint8_t contrast);
void OV5640_Sharpness(uint8_t sharp);
void OV5640_Special_Effects(uint8_t eft);
void OV5640_Test_Pattern(uint8_t mode);
void OV5640_Flash_Ctrl(uint8_t sw);
uint8_t OV5640_OutSize_Set(uint16_t offx, uint16_t offy, uint16_t width, uint16_t height);
uint8_t OV5640_ImageWin_Set(uint16_t offx, uint16_t offy, uint16_t width, uint16_t height);

#endif /* OV5640_H */
