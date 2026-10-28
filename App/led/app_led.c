#include "app_led.h"

#include "app_config.h"
#include "FreeRTOS.h"
#include "task.h"

#include "main.h" /* LED0_DS0_Pin / LED0_DS0_GPIO_Port */

static void vLedTask(void *argument)
{
  (void)argument;

  for (;;)
  {
    HAL_GPIO_TogglePin(LED0_DS0_GPIO_Port, LED0_DS0_Pin);
    vTaskDelay(pdMS_TO_TICKS(APP_LED_TOGGLE_MS));
  }
}

void AppLed_TaskCreate(void)
{
  xTaskCreate(vLedTask,
              "LED",
              APP_LED_TASK_STACK_SIZE,
              NULL,
              APP_LED_TASK_PRIORITY,
              NULL);
}
