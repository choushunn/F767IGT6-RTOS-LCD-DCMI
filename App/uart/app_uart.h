#ifndef APP_UART_H
#define APP_UART_H

/* 创建串口调试任务（复用 CubeMX 已初始化的 USART1 / huart1） */
void AppUart_TaskCreate(void);

/* 线程安全打印：格式化后入队，由串口任务统一发送，不阻塞调用方 */
void AppUart_Printf(const char *format, ...);

#endif /* APP_UART_H */
