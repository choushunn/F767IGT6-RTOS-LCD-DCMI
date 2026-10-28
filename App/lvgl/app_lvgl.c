#include "app_lvgl.h"

#include "app_config.h"
#include "uart/app_uart.h"

#include "FreeRTOS.h"
#include "task.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include "main.h" /* LED1_DS1_Pin / LED1_DS1_GPIO_Port */

static lv_obj_t *s_clickLabel = NULL;
static lv_obj_t *s_uptimeLabel = NULL;
static uint32_t s_clickCount = 0U;

/* LVGL tick 直接复用 FreeRTOS 的 1kHz SysTick */
static uint32_t app_lvgl_tick_get_ms(void)
{
  return (uint32_t)xTaskGetTickCount();
}

/* 开关控制板载 LED1（DS1 / PB0，低电平点亮） */
static void led_switch_event_cb(lv_event_t *e)
{
  lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
  bool isOn = lv_obj_has_state(sw, LV_STATE_CHECKED);

  HAL_GPIO_WritePin(LED1_DS1_GPIO_Port,
                    LED1_DS1_Pin,
                    isOn ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void click_button_event_cb(lv_event_t *e)
{
  (void)e;

  s_clickCount++;
  if (s_clickLabel != NULL)
  {
    lv_label_set_text_fmt(s_clickLabel, "Button clicks: %lu", (unsigned long)s_clickCount);
  }
}

static void slider_event_cb(lv_event_t *e)
{
  lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
  lv_obj_t *bar = (lv_obj_t *)lv_event_get_user_data(e);

  if (bar != NULL)
  {
    lv_bar_set_value(bar, lv_slider_get_value(slider), LV_ANIM_OFF);
  }
}

/* 周期刷新运行时间文本，验证 LVGL 定时器与 tick 链路 */
static void ui_timer_cb(lv_timer_t *timer)
{
  (void)timer;

  if (s_uptimeLabel != NULL)
  {
    lv_label_set_text_fmt(s_uptimeLabel,
                          "Uptime: %lu s",
                          (unsigned long)(lv_tick_get() / 1000U));
  }
}

static void app_lvgl_ui_create(void)
{
  lv_obj_t *scr = lv_screen_active();

  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), 0);
  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(scr,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(scr, 16, 0);
  lv_obj_set_style_pad_top(scr, 20, 0);

  /* 标题 */
  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "STM32F767IGT6  FreeRTOS + LVGL v9");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

  /* 控制面板 */
  lv_obj_t *panel = lv_obj_create(scr);
  lv_obj_set_size(panel, 640, 400);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel,
                        LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  /* 行 1：LED1 开关 */
  lv_obj_t *row1 = lv_obj_create(panel);
  lv_obj_set_size(row1, 560, 56);
  lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row1,
                        LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *ledLabel = lv_label_create(row1);
  lv_label_set_text(ledLabel, "LED1 (DS1 / PB0)");

  lv_obj_t *ledSwitch = lv_switch_create(row1);
  lv_obj_add_event_cb(ledSwitch, led_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

  /* 行 2：按钮 + 点击计数 */
  lv_obj_t *row2 = lv_obj_create(panel);
  lv_obj_set_size(row2, 560, 56);
  lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row2,
                        LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *button = lv_button_create(row2);
  lv_obj_set_size(button, 160, 44);
  lv_obj_add_event_cb(button, click_button_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *buttonLabel = lv_label_create(button);
  lv_label_set_text(buttonLabel, "Click me");
  lv_obj_center(buttonLabel);

  s_clickLabel = lv_label_create(row2);
  lv_label_set_text(s_clickLabel, "Button clicks: 0");

  /* 行 3：滑块控制进度条 */
  lv_obj_t *row3 = lv_obj_create(panel);
  lv_obj_set_size(row3, 560, 56);
  lv_obj_set_flex_flow(row3, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row3,
                        LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *bar = lv_bar_create(row3);
  lv_obj_set_size(bar, 360, 24);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 50, LV_ANIM_OFF);

  lv_obj_t *slider = lv_slider_create(row3);
  lv_obj_set_size(slider, 160, 20);
  lv_slider_set_range(slider, 0, 100);
  lv_slider_set_value(slider, 50, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, bar);

  /* 行 4：运行时间 */
  s_uptimeLabel = lv_label_create(panel);
  lv_label_set_text(s_uptimeLabel, "Uptime: 0 s");

  lv_timer_create(ui_timer_cb, 500, NULL);
}

static void AppLvgl_Task(void *param)
{
  (void)param;

  lv_init();                              /* 仅在本任务内调用一次 */
  lv_tick_set_cb(app_lvgl_tick_get_ms);   /* 绑定 FreeRTOS SysTick */
  lv_port_disp_init();
  lv_port_indev_init();
  app_lvgl_ui_create();

  AppUart_Printf("[LVGL] init done, free heap = %u bytes\r\n",
                 (unsigned int)xPortGetFreeHeapSize());

  for (;;)
  {
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(APP_LVGL_PERIOD_MS));
  }
}

void AppLvgl_TaskCreate(void)
{
  xTaskCreate(AppLvgl_Task,
              "LVGL",
              APP_LVGL_TASK_STACK_SIZE,
              NULL,
              APP_LVGL_TASK_PRIORITY,
              NULL);
}
