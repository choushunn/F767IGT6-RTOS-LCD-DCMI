#include "lv_port_indev.h"

#include "lvgl.h"

#include "touch/touch_detect.h"
#include "uart/app_uart.h"

static void lv_port_indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
  uint16_t x = 0U;
  uint16_t y = 0U;

  (void)indev;

  if (Touch_Scan(&x, &y) != 0U)
  {
    /* Touch_Scan() 返回原生 1024x600 物理坐标。LVGL v9 输入层会根据
     * 显示旋转自动转换到 600x1024 逻辑坐标，这里不能再次旋转。 */
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  }
  else
  {
    data->state = LV_INDEV_STATE_RELEASED;
  }

  data->continue_reading = false;
}

void lv_port_indev_init(void)
{
  lv_indev_t *indev;
  uint8_t detected;

  /* GT9xxx 电容触摸：软件 I2C 探测 + 初始化（失败时输入设备仍注册，只是始终 RELEASED） */
  detected = Touch_Detect();
  AppUart_Printf("[TOUCH] GT9xxx %s\r\n", (detected != 0U) ? "detected" : "not found");

  indev = lv_indev_create();
  if (indev == NULL)
  {
    return;
  }

  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lv_port_indev_read_cb);
}
