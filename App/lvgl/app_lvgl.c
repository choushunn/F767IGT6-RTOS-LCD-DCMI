#include "app_lvgl.h"

#include "app_config.h"
#include "uart/app_uart.h"

#include "FreeRTOS.h"
#include "task.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include "main.h" /* LED0_DS0_Pin / LED1_DS1_Pin */

static lv_obj_t *s_clickLabel = NULL;
static lv_obj_t *s_uptimeLabel = NULL;
static lv_obj_t *s_heapLabel = NULL;
static lv_obj_t *s_led0Indicator = NULL;
static lv_obj_t *s_chart = NULL;
static lv_chart_series_t *s_chartSeries = NULL;
static lv_obj_t *s_arc = NULL;
static uint32_t s_clickCount = 0U;

/* LVGL tick 直接复用 FreeRTOS 的 1kHz SysTick */
static uint32_t app_lvgl_tick_get_ms(void)
{
  return (uint32_t)xTaskGetTickCount();
}

/*=========================== 事件回调 ===========================*/

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
    lv_label_set_text_fmt(s_clickLabel, "按钮点击: %lu 次", (unsigned long)s_clickCount);
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

/*=========================== 周期刷新 ===========================*/

/* 200ms：曲线、圆弧、LED0 实际电平 */
static void ui_fast_timer_cb(lv_timer_t *timer)
{
  (void)timer;

  if (s_chart != NULL)
  {
    lv_chart_set_next_value(s_chart, s_chartSeries, (int32_t)lv_rand(20, 95));
  }

  if (s_arc != NULL)
  {
    lv_arc_set_value(s_arc, (int32_t)lv_rand(0, 100));
  }

  /* 读取 LED0（PB1）真实电平：低电平点亮 */
  if (s_led0Indicator != NULL)
  {
    if (HAL_GPIO_ReadPin(LED0_DS0_GPIO_Port, LED0_DS0_Pin) == GPIO_PIN_RESET)
    {
      lv_led_on(s_led0Indicator);
    }
    else
    {
      lv_led_off(s_led0Indicator);
    }
  }
}

/* 1s：运行时间与 FreeRTOS 剩余堆 */
static void ui_slow_timer_cb(lv_timer_t *timer)
{
  (void)timer;

  if (s_uptimeLabel != NULL)
  {
    lv_label_set_text_fmt(s_uptimeLabel,
                          "运行时间 %lu s",
                          (unsigned long)(lv_tick_get() / 1000U));
  }

  if (s_heapLabel != NULL)
  {
    lv_label_set_text_fmt(s_heapLabel,
                          "FreeRTOS 剩余堆: %u B    LVGL 内存池: %u B",
                          (unsigned int)xPortGetFreeHeapSize(),
                          (unsigned int)LV_MEM_SIZE);
  }
}

/*=========================== 界面构建 ===========================*/

static lv_obj_t *create_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  if (font != NULL)
  {
    lv_obj_set_style_text_font(label, font, 0);
  }
  return label;
}

static lv_obj_t *create_panel(lv_obj_t *parent, int32_t w, int32_t h)
{
  lv_obj_t *panel = lv_obj_create(parent);
  lv_obj_set_size(panel, w, h);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x1B2430), 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_radius(panel, 12, 0);
  lv_obj_set_style_pad_all(panel, 16, 0);
  lv_obj_set_style_pad_row(panel, 12, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  return panel;
}

static lv_obj_t *create_row(lv_obj_t *parent, int32_t w, int32_t h)
{
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, w, h);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row,
                        LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  return row;
}

static void app_lvgl_ui_create(void)
{
  lv_obj_t *scr = lv_screen_active();

  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0E1621), 0);
  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(scr,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(scr, 16, 0);
  lv_obj_set_style_pad_row(scr, 16, 0);

  /* ---------------- 顶部标题栏 ---------------- */
  lv_obj_t *header = create_row(scr, 992, 48);
  create_label(header, "STM32F767IGT6  ·  FreeRTOS + LVGL v9", &lv_font_montserrat_20, 0xFFFFFF);

  s_uptimeLabel = create_label(header, "运行时间 0 s", &lv_font_montserrat_16, 0x8AB4F8);

  /* ---------------- 主体区域 ---------------- */
  lv_obj_t *body = create_row(scr, 992, 440);
  lv_obj_set_style_pad_column(body, 16, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(body,
                        LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  /* 左侧：板载控制 */
  lv_obj_t *left = create_panel(body, 420, 440);
  create_label(left, "板载控制", &lv_font_montserrat_20, 0xFFFFFF);

  lv_obj_t *rowLed1 = create_row(left, 388, 48);
  create_label(rowLed1, "LED1 (DS1 / PB0)", &lv_font_montserrat_16, 0xD0D6DC);
  lv_obj_t *ledSwitch = lv_switch_create(rowLed1);
  lv_obj_add_event_cb(ledSwitch, led_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t *rowLed0 = create_row(left, 388, 48);
  create_label(rowLed0, "LED0 (DS0 / PB1) 由任务翻转", &lv_font_montserrat_16, 0xD0D6DC);
  s_led0Indicator = lv_led_create(rowLed0);
  lv_obj_set_size(s_led0Indicator, 26, 26);
  lv_led_set_color(s_led0Indicator, lv_color_hex(0x2ECC71));
  lv_led_set_brightness(s_led0Indicator, 255);

  lv_obj_t *rowSlider = create_row(left, 388, 48);
  create_label(rowSlider, "滑块", &lv_font_montserrat_16, 0xD0D6DC);
  lv_obj_t *slider = lv_slider_create(rowSlider);
  lv_obj_set_size(slider, 240, 18);
  lv_slider_set_range(slider, 0, 100);
  lv_slider_set_value(slider, 50, LV_ANIM_OFF);

  lv_obj_t *bar = lv_bar_create(left);
  lv_obj_set_size(bar, 388, 22);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 50, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, bar);

  lv_obj_t *rowBtn = create_row(left, 388, 56);
  lv_obj_t *button = lv_button_create(rowBtn);
  lv_obj_set_size(button, 160, 44);
  lv_obj_add_event_cb(button, click_button_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *buttonLabel = lv_label_create(button);
  lv_label_set_text(buttonLabel, "点击我");
  lv_obj_center(buttonLabel);

  s_clickLabel = create_label(rowBtn, "按钮点击: 0 次", &lv_font_montserrat_16, 0xD0D6DC);

  /* 右侧：实时曲线 + 圆弧 */
  lv_obj_t *right = create_panel(body, 556, 440);
  create_label(right, "实时曲线（随机数据源）", &lv_font_montserrat_20, 0xFFFFFF);

  s_chart = lv_chart_create(right);
  lv_obj_set_size(s_chart, 520, 220);
  lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x101820), 0);
  lv_obj_set_style_border_width(s_chart, 0, 0);
  lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(s_chart, 40);
  lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_set_div_line_count(s_chart, 5, 5);
  s_chartSeries = lv_chart_add_series(s_chart, lv_color_hex(0x00E5FF), LV_CHART_AXIS_PRIMARY_Y);

  lv_obj_t *rowArc = create_row(right, 520, 120);
  s_arc = lv_arc_create(rowArc);
  lv_obj_set_size(s_arc, 110, 110);
  lv_arc_set_range(s_arc, 0, 100);
  lv_arc_set_value(s_arc, 50);

  create_label(rowArc, "圆弧指示器 0~100", &lv_font_montserrat_16, 0xD0D6DC);

  /* ---------------- 底部状态栏 ---------------- */
  s_heapLabel = create_label(scr,
                             "FreeRTOS 剩余堆: --    LVGL 内存池: --",
                             &lv_font_montserrat_14,
                             0x8AB4F8);

  lv_timer_create(ui_fast_timer_cb, 200, NULL);
  lv_timer_create(ui_slow_timer_cb, 1000, NULL);
}

/*=========================== LVGL 任务 ===========================*/

static void AppLvgl_Task(void *param)
{
  (void)param;

  AppUart_Printf("[LVGL] task start\r\n");

  lv_init();                              /* 仅在本任务内调用一次 */
  AppUart_Printf("[LVGL] lv_init done\r\n");

  lv_tick_set_cb(app_lvgl_tick_get_ms);   /* 绑定 FreeRTOS SysTick */
  lv_port_disp_init();
  AppUart_Printf("[LVGL] display ready\r\n");

  lv_port_indev_init();
  app_lvgl_ui_create();
  AppUart_Printf("[LVGL] ui ready, free heap = %u bytes\r\n",
                 (unsigned int)xPortGetFreeHeapSize());

  for (;;)
  {
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(APP_LVGL_PERIOD_MS));
  }
}

void AppLvgl_TaskCreate(void)
{
  BaseType_t ret = xTaskCreate(AppLvgl_Task,
                               "LVGL",
                               APP_LVGL_TASK_STACK_SIZE,
                               NULL,
                               APP_LVGL_TASK_PRIORITY,
                               NULL);
  if (ret != pdPASS)
  {
    AppUart_Printf("[LVGL] task create FAILED\r\n");
  }
}
