#include "lv_port_indev.h"

#include "lvgl.h"

static void lv_port_indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
  (void)indev;

  data->state = LV_INDEV_STATE_RELEASED;
  data->continue_reading = false;
}

void lv_port_indev_init(void)
{
  lv_indev_t *indev = lv_indev_create();
  if (indev == NULL)
  {
    return;
  }

  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lv_port_indev_read_cb);
}
