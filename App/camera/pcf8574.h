#ifndef PCF8574_H
#define PCF8574_H

#include <stdint.h>

/* PCF8574 7 位地址 0x20（本板挂在 PH4=SCL / PH5=SDA 的软件 I2C 上） */
#define PCF8574_ADDR_7BIT    0x20U

/* PCF8574 各 IO 的功能分配（与正点原子例程一致） */
#define BEEP_IO              0U   /* P0：蜂鸣器，低电平驱动响 */
#define DCMI_PWDN_IO         2U   /* P2：OV2640 掉电控制，高电平掉电 */

/* 初始化：配置引脚并探测器件，返回 0 表示器件应答正常 */
uint8_t PCF8574_Init(void);

uint8_t PCF8574_ReadOneByte(void);
void PCF8574_WriteOneByte(uint8_t data);

/* 读-改-写单个 IO 位 */
void PCF8574_WriteBit(uint8_t bit, uint8_t sta);
uint8_t PCF8574_ReadBit(uint8_t bit);

#endif /* PCF8574_H */
