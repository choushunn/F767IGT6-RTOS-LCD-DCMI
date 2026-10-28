#ifndef TOUCH_PINS_H
#define TOUCH_PINS_H

#include "main.h"

/* 电容触摸 GT9xxx 引脚（与电阻屏 XPT2046 共用同一组接口，二选一）
 * SCL=PH6  SDA=PI3  RST=PI8  INT=PH7
 * 电阻屏预留：CLK=PH6 MOSI=PI3 MISO=PG3 CS=PI8 PEN=PH7
 */

#define TOUCH_SCL_PORT      GPIOH
#define TOUCH_SCL_PIN       GPIO_PIN_6

#define TOUCH_SDA_PORT      GPIOI
#define TOUCH_SDA_PIN       GPIO_PIN_3

#define TOUCH_RST_PORT      GPIOI
#define TOUCH_RST_PIN       GPIO_PIN_8

#define TOUCH_INT_PORT      GPIOH
#define TOUCH_INT_PIN       GPIO_PIN_7

#endif /* TOUCH_PINS_H */
