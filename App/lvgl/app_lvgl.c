#include "app_lvgl.h"

#include "app_config.h"
#include "camera/app_camera.h"
#include "camera/pcf8574.h"
#include "touch/touch_detect.h"
#include "uart/app_uart.h"

#include "FreeRTOS.h"
#include "task.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

#include "main.h" /* LED0_DS0_Pin / LED1_DS1_Pin */

/* 子集中文字库（黑体 14px，4bpp，含 ASCII 与界面用到的汉字，约 40KB） */
LV_FONT_DECLARE(app_font_cn_14);

/* ATK-7084 7 寸屏：800x480。左栏 0~320 为 LVGL 状态面板（图层 0），
 * 右栏 320~800 由 LTDC 图层 2 硬件扫描相机画面；LVGL 分辨率即为 320x480，
 * 不渲染右栏，可把图层 0 的 SDRAM 带宽压到原来的一半以下 */
#define APP_UI_HOR_RES    320
#define APP_UI_VER_RES    480
#define APP_UI_LEFT_W     320
#define APP_UI_CONTENT_W  (APP_UI_LEFT_W - 32)
#define APP_UI_FULL_W     800   /* 加载动画期间临时占用整屏 */

#define UI_COLOR_BG       0x0E1621
#define UI_COLOR_PANEL    0x1B2430
#define UI_COLOR_TEXT     0xE6EDF3
#define UI_COLOR_ACCENT   0x8AB4F8
#define UI_COLOR_OK       0x2ECC71
#define UI_COLOR_WARN     0xE67E22

/* 状态面板中需要动态刷新的控件 */
static lv_obj_t *s_uptimeValue = NULL;
static lv_obj_t *s_fpsValue = NULL;
static lv_obj_t *s_framesValue = NULL;
static lv_obj_t *s_errorValue = NULL;
static lv_obj_t *s_camStateValue = NULL;
static lv_obj_t *s_heapValue = NULL;
static lv_obj_t *s_poolValue = NULL;
static lv_obj_t *s_touchValue = NULL;
static lv_obj_t *s_led0Lamp = NULL;

/* 启动加载动画 */
static lv_obj_t *s_splashRoot = NULL;
static lv_obj_t *s_splashBar = NULL;
static lv_obj_t *s_splashPct = NULL;
static lv_obj_t *s_splashHint = NULL;

/* 前置声明：加载动画在控件构造与主界面之前执行 */
static lv_obj_t *create_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color);
static void app_lvgl_ui_create(void);

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

/* 开关控制板载蜂鸣器（PCF8574 P0，低电平驱动响，与开关状态相反） */
static void buzzer_switch_event_cb(lv_event_t *e)
{
  lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
  bool isOn = lv_obj_has_state(sw, LV_STATE_CHECKED);

  PCF8574_WriteBit(BEEP_IO, isOn ? 0U : 1U);
}

/*=========================== 周期刷新 ===========================*/

/* 500ms：刷新状态面板全部动态值 */
static void ui_status_timer_cb(lv_timer_t *timer)
{
  AppCameraStats_t cam;
  uint32_t upSec;

  (void)timer;

  upSec = lv_tick_get() / 1000U;

  if (s_uptimeValue != NULL)
  {
    lv_label_set_text_fmt(s_uptimeValue, "%02lu:%02lu:%02lu",
                          (unsigned long)(upSec / 3600U),
                          (unsigned long)((upSec / 60U) % 60U),
                          (unsigned long)(upSec % 60U));
  }

  AppCamera_GetStats(&cam);

  if (s_fpsValue != NULL)
  {
    lv_label_set_text_fmt(s_fpsValue, "%lu", (unsigned long)cam.fps);
  }
  if (s_framesValue != NULL)
  {
    lv_label_set_text_fmt(s_framesValue, "%lu", (unsigned long)cam.frames);
  }
  if (s_errorValue != NULL)
  {
    lv_label_set_text_fmt(s_errorValue, "%lu / %lu / %lu",
                          (unsigned long)cam.esync,
                          (unsigned long)cam.eovr,
                          (unsigned long)cam.dmaerr);
    lv_obj_set_style_text_color(s_errorValue,
                                lv_color_hex(((cam.esync + cam.eovr + cam.dmaerr) == 0U)
                                                 ? UI_COLOR_OK
                                                 : UI_COLOR_WARN),
                                0);
  }
  if (s_camStateValue != NULL)
  {
    lv_label_set_text(s_camStateValue,
                      (cam.ready != 0U) ? "运行中" : "已停止");
    lv_obj_set_style_text_color(s_camStateValue,
                                lv_color_hex((cam.ready != 0U) ? UI_COLOR_OK : UI_COLOR_WARN),
                                0);
  }

  if (s_heapValue != NULL)
  {
    lv_label_set_text_fmt(s_heapValue, "%u B", (unsigned int)xPortGetFreeHeapSize());
  }
  if (s_poolValue != NULL)
  {
    lv_label_set_text_fmt(s_poolValue, "%u B", (unsigned int)LV_MEM_SIZE);
  }
  if (s_touchValue != NULL)
  {
    lv_label_set_text(s_touchValue, (Touch_IsReady() != 0U) ? "正常" : "未找到");
    lv_obj_set_style_text_color(s_touchValue,
                                lv_color_hex((Touch_IsReady() != 0U) ? UI_COLOR_OK : UI_COLOR_WARN),
                                0);
  }

  /* LED0（PB1）由 app_led 任务 500ms 翻转，这里读回真实电平：低电平点亮 */
  if (s_led0Lamp != NULL)
  {
    if (HAL_GPIO_ReadPin(LED0_DS0_GPIO_Port, LED0_DS0_Pin) == GPIO_PIN_RESET)
    {
      lv_led_on(s_led0Lamp);
    }
    else
    {
      lv_led_off(s_led0Lamp);
    }
  }
}

/* 40ms：通知相机处理换帧统计（换帧本身在 DMA 中断里完成，LVGL 不搬运像素） */
static void ui_camera_timer_cb(lv_timer_t *timer)
{
  (void)timer;

  AppCamera_PollFrame();
}

/*=========================== 启动加载动画 ===========================*/

/* 每 25ms 推进 4%，约 0.65s；阶段文案随进度切换 */
static void splash_timer_cb(lv_timer_t *timer)
{
  int32_t value = lv_bar_get_value(s_splashBar) + 4;

  if (value > 100)
  {
    value = 100;
  }
  lv_bar_set_value(s_splashBar, value, LV_ANIM_OFF);
  lv_label_set_text_fmt(s_splashPct, "%d%%", (int)value);

  if (s_splashHint != NULL)
  {
    if (value < 30)
    {
      lv_label_set_text(s_splashHint, "显示就绪");
    }
    else if (value < 60)
    {
      lv_label_set_text(s_splashHint, "输入就绪");
    }
    else if (value < 95)
    {
      lv_label_set_text(s_splashHint, "相机就绪");
    }
    else
    {
      lv_label_set_text(s_splashHint, "进入界面");
    }
  }

  if (value >= 100)
  {
    lv_timer_delete(timer);

    if (s_splashRoot != NULL)
    {
      lv_obj_delete(s_splashRoot);
      s_splashRoot = NULL;
    }

    /* 加载结束：图层 0 收回左栏 320 宽，再构建状态面板 */
    lv_port_disp_set_area(APP_UI_LEFT_W, APP_UI_VER_RES);
    app_lvgl_ui_create();
  }
}

static void app_lvgl_splash_create(void)
{
  lv_obj_t *scr;

  /* 加载动画占满整屏：图层 0 临时扩到 800x480（相机图层此时尚未使能） */
  lv_port_disp_set_area(APP_UI_FULL_W, APP_UI_VER_RES);

  scr = lv_screen_active();

  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  s_splashRoot = lv_obj_create(scr);
  lv_obj_set_size(s_splashRoot, APP_UI_FULL_W, APP_UI_VER_RES);
  lv_obj_set_style_bg_opa(s_splashRoot, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_splashRoot, 0, 0);
  lv_obj_set_style_pad_all(s_splashRoot, 0, 0);
  lv_obj_set_style_pad_row(s_splashRoot, 14, 0);
  lv_obj_set_flex_flow(s_splashRoot, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_splashRoot,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(s_splashRoot, LV_DIR_NONE);

  create_label(s_splashRoot, "系统启动", &app_font_cn_14, UI_COLOR_TEXT);
  create_label(s_splashRoot, "STM32F767 - FreeRTOS + LVGL", &app_font_cn_14, UI_COLOR_ACCENT);

  s_splashBar = lv_bar_create(s_splashRoot);
  lv_obj_set_size(s_splashBar, 360, 14);
  lv_bar_set_range(s_splashBar, 0, 100);
  lv_bar_set_value(s_splashBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_splashBar, lv_color_hex(0x1B2430), 0);
  lv_obj_set_style_bg_color(s_splashBar, lv_color_hex(0x00E5FF), LV_PART_INDICATOR);

  s_splashPct = create_label(s_splashRoot, "0%", &app_font_cn_14, UI_COLOR_TEXT);
  s_splashHint = create_label(s_splashRoot, "启动中", &app_font_cn_14, 0x7F8C8D);

  lv_timer_create(splash_timer_cb, 25, NULL);
}

/*=========================== 控件构造 ===========================*/

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
  lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_PANEL), 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_radius(panel, 10, 0);
  lv_obj_set_style_pad_all(panel, 10, 0);
  /* 右侧多留 8px：面板右边缘与相机图层相邻，避免最右侧控件贴边被遮 */
  lv_obj_set_style_pad_right(panel, 18, 0);
  lv_obj_set_style_pad_row(panel, 2, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_scroll_dir(panel, LV_DIR_NONE);
  return panel;
}

static lv_obj_t *create_row(lv_obj_t *parent, int32_t w, int32_t h)
{
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, w, h);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row,
                        LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(row, LV_DIR_NONE);
  return row;
}

/* 分区标题：小号强调色文字 + 分隔线 */
static void create_section(lv_obj_t *parent, const char *title)
{
  lv_obj_t *label = create_label(parent, title, &app_font_cn_14, UI_COLOR_ACCENT);
  lv_obj_set_style_pad_top(label, 8, 0);
  (void)label;
}

/* 一行「名称 —— 数值」，返回数值 label 供后续刷新 */
static lv_obj_t *create_status_row(lv_obj_t *parent, const char *name, const char *value, uint32_t color)
{
  lv_obj_t *row = create_row(parent, APP_UI_CONTENT_W, 26);
  lv_obj_t *valueLabel;

  create_label(row, name, &app_font_cn_14, UI_COLOR_TEXT);
  valueLabel = create_label(row, value, &app_font_cn_14, color);

  return valueLabel;
}

static void app_lvgl_ui_create(void)
{
  lv_obj_t *scr = lv_screen_active();

  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  /* 整个 LVGL 屏幕（320x480）就是左侧状态面板 */
  lv_obj_t *left = create_panel(scr, APP_UI_LEFT_W, APP_UI_VER_RES);
  lv_obj_set_style_radius(left, 0, 0);

  create_label(left, "系统状态", &app_font_cn_14, 0xFFFFFF);
  lv_obj_t *subtitle = create_label(left, "STM32F767 - RTOS + LVGL", &app_font_cn_14, 0x7F8C8D);
  lv_obj_set_style_pad_bottom(subtitle, 2, 0);

  s_uptimeValue = create_status_row(left, "运行时间", "00:00:00", UI_COLOR_ACCENT);

  /* 相机 */
  create_section(left, "相机");
  s_fpsValue = create_status_row(left, "帧率", "0", UI_COLOR_OK);
  s_framesValue = create_status_row(left, "帧数", "0", UI_COLOR_TEXT);
  s_errorValue = create_status_row(left, "错误", "0 / 0 / 0", UI_COLOR_OK);
  s_camStateValue = create_status_row(left, "状态", "--", UI_COLOR_WARN);

  /* 系统资源 */
  create_section(left, "系统");
  s_heapValue = create_status_row(left, "剩余堆", "--", UI_COLOR_TEXT);
  s_poolValue = create_status_row(left, "内存池", "--", UI_COLOR_TEXT);
  s_touchValue = create_status_row(left, "触摸", "--", UI_COLOR_TEXT);

  /* 板载资源 */
  create_section(left, "板载");
  lv_obj_t *rowLed0 = create_row(left, APP_UI_CONTENT_W, 30);
  create_label(rowLed0, "LED0 闪烁", &app_font_cn_14, UI_COLOR_TEXT);
  s_led0Lamp = lv_led_create(rowLed0);
  lv_obj_set_size(s_led0Lamp, 18, 18);
  lv_led_set_color(s_led0Lamp, lv_color_hex(UI_COLOR_OK));
  lv_led_set_brightness(s_led0Lamp, 255);

  lv_obj_t *rowLed1 = create_row(left, APP_UI_CONTENT_W, 34);
  create_label(rowLed1, "LED1", &app_font_cn_14, UI_COLOR_TEXT);
  lv_obj_t *ledSwitch = lv_switch_create(rowLed1);
  lv_obj_add_event_cb(ledSwitch, led_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t *rowBeep = create_row(left, APP_UI_CONTENT_W, 34);
  create_label(rowBeep, "蜂鸣器", &app_font_cn_14, UI_COLOR_TEXT);
  lv_obj_t *beepSwitch = lv_switch_create(rowBeep);
  lv_obj_add_event_cb(beepSwitch, buzzer_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

  lv_timer_create(ui_status_timer_cb, 500, NULL);
  lv_timer_create(ui_camera_timer_cb, APP_CAMERA_REFRESH_MS, NULL);
}

/*=========================== LVGL 任务 ===========================*/

static void AppLvgl_Task(void *param)
{
  (void)param;

  lv_init();                              /* 仅在本任务内调用一次 */
  lv_tick_set_cb(app_lvgl_tick_get_ms);   /* 绑定 FreeRTOS SysTick */
  lv_port_disp_init();
  lv_port_indev_init();
  app_lvgl_splash_create();               /* 先显示启动加载动画，完成后自动进入状态面板 */

  AppUart_Printf("[LVGL] init done, %dx%d, free heap = %u bytes\r\n",
                 APP_UI_HOR_RES,
                 APP_UI_VER_RES,
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
