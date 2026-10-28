#ifndef CAMERA_UTIL_H
#define CAMERA_UTIL_H

#include <stdint.h>

/* 微秒级延时：基于 DWT 周期计数器（首次调用时自动使能，不依赖 RTOS） */
void Camera_DelayUs(uint32_t us);

/* 毫秒级延时：直接使用 HAL 时基（TIM7 1kHz），调度器启动前同样可用 */
void Camera_DelayMs(uint32_t ms);

#endif /* CAMERA_UTIL_H */
