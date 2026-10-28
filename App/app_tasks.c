#include "app_tasks.h"

#include "FreeRTOS.h"
#include "task.h"

#include "led/app_led.h"
#include "uart/app_uart.h"
#include "lvgl/app_lvgl.h"
#include "camera/app_camera.h"

/* configCHECK_FOR_STACK_OVERFLOW = 2 触发时进入死循环，便于调试器定位 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;

  for (;;)
  {
  }
}

void AppTasks_Init(void)
{
  /* 相机在调度器启动前完成上电、配置与采集启动 */
  AppCamera_Init();

  AppLed_TaskCreate();
  AppUart_TaskCreate();
  AppLvgl_TaskCreate();
}
