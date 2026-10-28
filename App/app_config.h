#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "FreeRTOS.h"

/* LED 演示任务 */
#define APP_LED_TASK_PRIORITY       1
#define APP_LED_TASK_STACK_SIZE     ( configMINIMAL_STACK_SIZE )
#define APP_LED_TOGGLE_MS           500

/* 串口调试任务 */
#define APP_UART_TASK_PRIORITY      2
#define APP_UART_TASK_STACK_SIZE    ( 2 * configMINIMAL_STACK_SIZE )
#define APP_UART_QUEUE_LEN          16
#define APP_UART_MAX_MSG_LEN        128
#define APP_UART_SEND_TIMEOUT       pdMS_TO_TICKS( 100 )

/* LVGL 刷新任务
 * 渲染递归较深，栈给足，否则会触发 configCHECK_FOR_STACK_OVERFLOW 死循环 */
#define APP_LVGL_TASK_PRIORITY      4
#define APP_LVGL_TASK_STACK_SIZE    ( 32 * configMINIMAL_STACK_SIZE )
#define APP_LVGL_PERIOD_MS          5

#endif /* APP_CONFIG_H */
