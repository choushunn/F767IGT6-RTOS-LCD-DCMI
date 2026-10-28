#ifndef TOUCH_DETECT_H
#define TOUCH_DETECT_H

#include <stdint.h>

/* 探测并初始化 GT9xxx 电容触摸（软件 I2C）
 * 返回：1 = 探测成功，0 = 未找到触摸芯片 */
uint8_t Touch_Detect(void);

/* 读取当前触点坐标（面板坐标，800x480 直通）
 * 返回：1 = 有触点并输出坐标，0 = 无触点 */
uint8_t Touch_Scan(uint16_t *x, uint16_t *y);

/* 触摸芯片是否已成功探测（1 = 可用） */
uint8_t Touch_IsReady(void);

#endif /* TOUCH_DETECT_H */
