#include "app_uart.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "usart.h" /* huart1 */

static QueueHandle_t s_uartQueue = NULL;

static void AppUart_SendRaw(const char *msg)
{
  uint16_t len = (uint16_t)strlen(msg);

  if (len > 0U)
  {
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, len, HAL_MAX_DELAY);
  }
}

/* 串口任务：阻塞等待消息，取出后发送并释放内存 */
static void vUartTask(void *argument)
{
  (void)argument;
  char *pMsg;

  for (;;)
  {
    if (xQueueReceive(s_uartQueue, &pMsg, portMAX_DELAY) == pdPASS)
    {
      AppUart_SendRaw(pMsg);
      vPortFree(pMsg);
    }
  }
}

void AppUart_TaskCreate(void)
{
  s_uartQueue = xQueueCreate(APP_UART_QUEUE_LEN, sizeof(char *));
  if (s_uartQueue == NULL)
  {
    return;
  }

  xTaskCreate(vUartTask,
              "UART",
              APP_UART_TASK_STACK_SIZE,
              NULL,
              APP_UART_TASK_PRIORITY,
              NULL);
}

void AppUart_Printf(const char *format, ...)
{
  char *pMsg = pvPortMalloc(APP_UART_MAX_MSG_LEN);
  if (pMsg == NULL)
  {
    return;
  }

  va_list args;
  va_start(args, format);
  vsnprintf(pMsg, APP_UART_MAX_MSG_LEN, format, args);
  va_end(args);
  pMsg[APP_UART_MAX_MSG_LEN - 1U] = '\0';

  /* 调度器未启动或队列未创建时直接同步发送 */
  if (s_uartQueue == NULL)
  {
    AppUart_SendRaw(pMsg);
    vPortFree(pMsg);
    return;
  }

  if (xQueueSend(s_uartQueue, &pMsg, APP_UART_SEND_TIMEOUT) != pdPASS)
  {
    vPortFree(pMsg); /* 队列满，丢弃该条消息 */
  }
}
