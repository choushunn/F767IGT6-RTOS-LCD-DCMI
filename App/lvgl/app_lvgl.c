#include "app_lvgl.h"

#include "app_config.h"
#include "camera/app_camera.h"
#include "display_config.h"
#include "touch/touch_detect.h"
#include "uart/app_uart.h"

#include "FreeRTOS.h"
#include "task.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

LV_FONT_DECLARE(app_font_cn_14);
LV_FONT_DECLARE(app_font_cn_24);

#define APP_UI_HOR_RES       DISPLAY_LVGL_W
#define APP_UI_VER_RES       DISPLAY_LVGL_H
#define APP_UI_FULL_W        DISPLAY_PANEL_W
#define APP_UI_FULL_H        DISPLAY_PANEL_H

#define UI_COLOR_BG          0x0B0E11
#define UI_COLOR_SURFACE     0x171C22
#define UI_COLOR_SURFACE_2   0x20262E
#define UI_COLOR_TEXT        0xE5E9EF
#define UI_COLOR_MUTED       0x87929F
#define UI_COLOR_SAFETY      0xF2C94C
#define UI_COLOR_OK          0x18C29C
#define UI_COLOR_DANGER      0xE34D59

/* UI-review flow only. Set to 0 when face-recognition results are connected. */
#define FACE_UI_DEMO_FLOW    1U

typedef enum
{
  FACE_UI_IDLE = 0,
  FACE_UI_SCANNING,
  FACE_UI_VERIFIED,
  FACE_UI_REJECTED
} FaceUiState_t;

static lv_obj_t *s_currentStep;
static lv_obj_t *s_stateTitle;
static lv_obj_t *s_stateDetail;
static lv_obj_t *s_stateAccent;
static lv_obj_t *s_actionButton;
static lv_obj_t *s_actionLabel;
static lv_obj_t *s_cameraDot;
static lv_obj_t *s_touchDot;
static lv_obj_t *s_deviceError;

static lv_obj_t *s_splashRoot;
static lv_obj_t *s_splashBar;
static lv_obj_t *s_splashPct;
static lv_obj_t *s_splashHint;

static lv_timer_t *s_faceTimer;
static FaceUiState_t s_faceState = FACE_UI_IDLE;
static uint8_t s_scanProgress;

static lv_obj_t *create_label(lv_obj_t *parent,
                              const char *text,
                              const lv_font_t *font,
                              uint32_t color);
static void app_lvgl_ui_create(void);

static uint32_t app_lvgl_tick_get_ms(void)
{
  return (uint32_t)xTaskGetTickCount();
}

static void set_text_color(lv_obj_t *obj, uint32_t color)
{
  lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}

static void set_status_color(uint32_t color)
{
  set_text_color(s_currentStep, color);
  set_text_color(s_stateTitle, color);
  lv_obj_set_style_bg_color(s_stateAccent, lv_color_hex(color), 0);
}

static void set_action_button(const char *text,
                              uint32_t background,
                              uint32_t foreground)
{
  lv_label_set_text(s_actionLabel, text);
  lv_obj_set_style_bg_color(s_actionButton, lv_color_hex(background), 0);
  set_text_color(s_actionLabel, foreground);
}

static void set_detail_hidden(uint8_t hidden)
{
  if (hidden != 0U)
  {
    lv_obj_set_hidden(s_stateDetail, true);
  }
  else
  {
    lv_obj_set_hidden(s_stateDetail, false);
  }
}

static void apply_face_state(FaceUiState_t state)
{
  s_faceState = state;

  if (state == FACE_UI_IDLE)
  {
    lv_label_set_text(s_currentStep, "人脸采集");
    lv_label_set_text(s_stateTitle, "等待验证");
    set_status_color(UI_COLOR_SAFETY);
    set_detail_hidden(1U);
    set_action_button("开始采集", UI_COLOR_SAFETY, UI_COLOR_BG);
    lv_obj_remove_state(s_actionButton, LV_STATE_DISABLED);
  }
  else if (state == FACE_UI_SCANNING)
  {
    lv_label_set_text(s_currentStep, "人脸采集");
    lv_label_set_text(s_stateTitle, "正在采集");
    set_status_color(UI_COLOR_SAFETY);
    set_detail_hidden(1U);
    set_action_button("验证中，请保持稳定", UI_COLOR_SURFACE_2, UI_COLOR_MUTED);
    lv_obj_add_state(s_actionButton, LV_STATE_DISABLED);
  }
  else if (state == FACE_UI_VERIFIED)
  {
    lv_label_set_text(s_currentStep, "上机授权");
    lv_label_set_text(s_stateTitle, "授权通过");
    set_status_color(UI_COLOR_OK);
    set_detail_hidden(1U);
    set_action_button("重新验证", UI_COLOR_SURFACE, UI_COLOR_OK);
    lv_obj_set_style_border_width(s_actionButton, 2, 0);
    lv_obj_set_style_border_color(s_actionButton, lv_color_hex(UI_COLOR_OK), 0);
    lv_obj_remove_state(s_actionButton, LV_STATE_DISABLED);
  }
  else
  {
    lv_label_set_text(s_currentStep, "人脸采集");
    lv_label_set_text(s_stateTitle, "验证未通过");
    set_status_color(UI_COLOR_DANGER);
    lv_label_set_text(s_stateDetail, "请调整站位或光线，移开遮挡后重新验证");
    set_detail_hidden(0U);
    set_action_button("重新验证", UI_COLOR_DANGER, UI_COLOR_TEXT);
    lv_obj_remove_state(s_actionButton, LV_STATE_DISABLED);
  }

  if (state != FACE_UI_VERIFIED)
  {
    lv_obj_set_style_border_width(s_actionButton, 0, 0);
  }
}

static void stop_face_timer(void)
{
  if (s_faceTimer != NULL)
  {
    lv_timer_delete(s_faceTimer);
    s_faceTimer = NULL;
  }
}

static void finish_face_demo(void)
{
  AppCameraStats_t cam;

  AppCamera_GetStats(&cam);
  stop_face_timer();
  apply_face_state((cam.ready != 0U) ? FACE_UI_VERIFIED : FACE_UI_REJECTED);
}

static void face_scan_timer_cb(lv_timer_t *timer)
{
  (void)timer;

  s_scanProgress = (uint8_t)(s_scanProgress + 3U);
  if (s_scanProgress >= 100U)
  {
    s_scanProgress = 100U;
  }

  if (s_scanProgress >= 90U)
  {
    lv_label_set_text(s_currentStep, "上机授权");
    lv_label_set_text(s_stateTitle, "正在授权");
  }
  else if (s_scanProgress >= 55U)
  {
    lv_label_set_text(s_currentStep, "身份比对");
    lv_label_set_text(s_stateTitle, "正在比对");
  }

  if (s_scanProgress >= 100U)
  {
#if FACE_UI_DEMO_FLOW
    finish_face_demo();
#else
    /* Real integrations replace this state with the recognition result. */
    stop_face_timer();
#endif
  }
}

static void start_face_scan(void)
{
  AppCameraStats_t cam;

  stop_face_timer();
  AppCamera_GetStats(&cam);
  if (cam.ready == 0U)
  {
    apply_face_state(FACE_UI_REJECTED);
    lv_label_set_text(s_stateTitle, "设备未就绪");
    lv_label_set_text(s_stateDetail, "摄像头尚未连接，无法开始人脸验证");
    return;
  }

  s_scanProgress = 0U;
  apply_face_state(FACE_UI_SCANNING);
  s_faceTimer = lv_timer_create(face_scan_timer_cb, 200, NULL);
}

static void action_button_cb(lv_event_t *event)
{
  (void)event;

  if (s_faceState != FACE_UI_SCANNING)
  {
    start_face_scan();
  }
}

static void set_dot_color(lv_obj_t *dot, uint8_t ok)
{
  lv_obj_set_style_bg_color(dot,
                            lv_color_hex((ok != 0U) ? UI_COLOR_OK : UI_COLOR_DANGER),
                            0);
}

static void ui_status_timer_cb(lv_timer_t *timer)
{
  AppCameraStats_t cam;
  uint8_t cameraReady;
  uint8_t touchReady;

  (void)timer;

  AppCamera_GetStats(&cam);
  cameraReady = (cam.ready != 0U) ? 1U : 0U;
  touchReady = (Touch_IsReady() != 0U) ? 1U : 0U;

  set_dot_color(s_cameraDot, cameraReady);
  set_dot_color(s_touchDot, touchReady);

  if ((cameraReady == 0U) || (touchReady == 0U))
  {
    if ((cameraReady == 0U) && (touchReady == 0U))
    {
      lv_label_set_text(s_deviceError, "设备异常");
    }
    else if (cameraReady == 0U)
    {
      lv_label_set_text(s_deviceError, "摄像头异常");
    }
    else
    {
      lv_label_set_text(s_deviceError, "触摸异常");
    }
    lv_obj_set_hidden(s_deviceError, false);
  }
  else
  {
    lv_obj_set_hidden(s_deviceError, true);
  }
}

static void ui_camera_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  AppCamera_PollFrame();
}

static void create_hazard_strip(lv_obj_t *parent,
                                int32_t x,
                                int32_t y,
                                int32_t width)
{
  const int32_t block = 32;
  int32_t i;

  for (i = 0; i < (width / block); i++)
  {
    lv_obj_t *stripe = lv_obj_create(parent);
    lv_obj_set_pos(stripe, x + (i * block), y);
    lv_obj_set_size(stripe, block, 6);
    lv_obj_set_style_radius(stripe, 0, 0);
    lv_obj_set_style_border_width(stripe, 0, 0);
    lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(
        stripe,
        lv_color_hex(((i & 1) == 0) ? UI_COLOR_BG : UI_COLOR_SAFETY),
        0);
  }
}

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
    if (value < 35)
    {
      lv_label_set_text(s_splashHint, "显示系统自检");
    }
    else if (value < 70)
    {
      lv_label_set_text(s_splashHint, "视觉采集自检");
    }
    else if (value < 95)
    {
      lv_label_set_text(s_splashHint, "安全联锁自检");
    }
    else
    {
      lv_label_set_text(s_splashHint, "准备授权");
    }
  }

  if (value >= 100)
  {
    lv_timer_delete(timer);
    s_splashRoot = NULL;
    s_splashBar = NULL;
    s_splashPct = NULL;
    s_splashHint = NULL;
    lv_obj_clean(lv_screen_active());

    lv_port_disp_set_area(APP_UI_HOR_RES, APP_UI_VER_RES);
    app_lvgl_ui_create();
  }
}

static void app_lvgl_splash_create(void)
{
  lv_obj_t *scr;
  lv_obj_t *accent;
  lv_obj_t *brand;
  lv_obj_t *subtitle;

  lv_port_disp_set_area(APP_UI_FULL_W, APP_UI_FULL_H);
  scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  accent = lv_obj_create(scr);
  lv_obj_set_pos(accent, 94, 130);
  lv_obj_set_size(accent, 16, 330);
  lv_obj_set_style_radius(accent, 0, 0);
  lv_obj_set_style_border_width(accent, 0, 0);
  lv_obj_set_style_bg_color(accent, lv_color_hex(UI_COLOR_SAFETY), 0);

  s_splashRoot = lv_obj_create(scr);
  lv_obj_set_pos(s_splashRoot, 144, 104);
  lv_obj_set_size(s_splashRoot, 720, 390);
  lv_obj_set_style_bg_opa(s_splashRoot, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_splashRoot, 0, 0);
  lv_obj_set_style_pad_all(s_splashRoot, 0, 0);
  lv_obj_set_style_pad_row(s_splashRoot, 16, 0);
  lv_obj_set_flex_flow(s_splashRoot, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_splashRoot,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  brand = create_label(s_splashRoot,
                       "身份安全授权",
                       &app_font_cn_24,
                       UI_COLOR_SAFETY);
  lv_obj_set_style_pad_top(brand, 8, 0);
  subtitle = create_label(s_splashRoot,
                          "设备 A-01  |  安全联锁启动中",
                          &app_font_cn_14,
                          UI_COLOR_MUTED);
  lv_obj_set_style_pad_bottom(subtitle, 20, 0);

  s_splashBar = lv_bar_create(s_splashRoot);
  lv_obj_set_size(s_splashBar, 520, 12);
  lv_bar_set_range(s_splashBar, 0, 100);
  lv_bar_set_value(s_splashBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_splashBar, lv_color_hex(UI_COLOR_SURFACE_2), 0);
  lv_obj_set_style_bg_color(s_splashBar,
                            lv_color_hex(UI_COLOR_SAFETY),
                            LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_splashBar, 0, 0);
  lv_obj_set_style_radius(s_splashBar, 0, LV_PART_INDICATOR);

  s_splashPct = create_label(s_splashRoot, "0%", &app_font_cn_24, UI_COLOR_TEXT);
  s_splashHint = create_label(s_splashRoot,
                              "显示系统自检",
                              &app_font_cn_14,
                              UI_COLOR_MUTED);

  create_hazard_strip(scr, 94, 480, 832);
  lv_timer_create(splash_timer_cb, 25, NULL);
}

static lv_obj_t *create_label(lv_obj_t *parent,
                              const char *text,
                              const lv_font_t *font,
                              uint32_t color)
{
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  return label;
}

static lv_obj_t *create_dot(lv_obj_t *parent, int32_t x, int32_t y)
{
  lv_obj_t *dot = lv_obj_create(parent);

  lv_obj_set_pos(dot, x, y);
  lv_obj_set_size(dot, 8, 8);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(UI_COLOR_OK), 0);
  return dot;
}

static void create_divider(lv_obj_t *parent, int32_t x, int32_t y, int32_t width)
{
  lv_obj_t *divider = lv_obj_create(parent);

  lv_obj_set_pos(divider, x, y);
  lv_obj_set_size(divider, width, 1);
  lv_obj_set_style_bg_color(divider, lv_color_hex(UI_COLOR_SURFACE_2), 0);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(divider, 0, 0);
  lv_obj_set_style_radius(divider, 0, 0);
}

static void app_lvgl_ui_create(void)
{
  lv_obj_t *scr;
  lv_obj_t *device;
  lv_obj_t *onlineDot;
  lv_obj_t *onlineText;

  scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  device = create_label(scr, "A-01", &app_font_cn_24, UI_COLOR_SAFETY);
  lv_obj_set_pos(device, 16, 18);

  onlineDot = create_dot(scr, 350, 23);
  onlineText = create_label(scr, "在线", &app_font_cn_14, UI_COLOR_MUTED);
  lv_obj_set_pos(onlineText, 364, 15);
  create_divider(scr, 16, 74, 392);

  s_currentStep = create_label(scr, "人脸采集", &app_font_cn_24, UI_COLOR_SAFETY);
  lv_obj_set_pos(s_currentStep, 16, 118);

  s_stateAccent = lv_obj_create(scr);
  lv_obj_set_pos(s_stateAccent, 16, 178);
  lv_obj_set_size(s_stateAccent, 4, 82);
  lv_obj_set_style_radius(s_stateAccent, 0, 0);
  lv_obj_set_style_border_width(s_stateAccent, 0, 0);
  lv_obj_set_style_bg_opa(s_stateAccent, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_stateAccent, lv_color_hex(UI_COLOR_SAFETY), 0);

  s_stateTitle = create_label(scr, "等待验证", &app_font_cn_24, UI_COLOR_TEXT);
  lv_obj_set_pos(s_stateTitle, 36, 190);

  s_stateDetail = create_label(scr,
                               "",
                               &app_font_cn_14,
                               UI_COLOR_MUTED);
  lv_obj_set_pos(s_stateDetail, 36, 232);
  lv_obj_set_width(s_stateDetail, 364);

  s_actionButton = lv_button_create(scr);
  lv_obj_set_pos(s_actionButton, 16, 476);
  lv_obj_set_size(s_actionButton, 392, 68);
  lv_obj_set_style_radius(s_actionButton, 5, 0);
  lv_obj_set_style_bg_color(s_actionButton,
                            lv_color_hex(UI_COLOR_SAFETY),
                            0);
  lv_obj_set_style_bg_opa(s_actionButton, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_actionButton, 0, 0);
  lv_obj_set_style_shadow_width(s_actionButton, 0, 0);
  lv_obj_add_event_cb(s_actionButton, action_button_cb, LV_EVENT_CLICKED, NULL);

  s_actionLabel = create_label(s_actionButton,
                               "开始采集",
                               &app_font_cn_24,
                               UI_COLOR_BG);
  lv_obj_center(s_actionLabel);

  s_cameraDot = create_dot(scr, 360, 557);
  s_touchDot = create_dot(scr, 380, 557);

  s_deviceError = create_label(scr, "", &app_font_cn_14, UI_COLOR_DANGER);
  lv_obj_set_pos(s_deviceError, 16, 548);
  lv_obj_set_hidden(s_deviceError, true);

  apply_face_state(FACE_UI_IDLE);
  lv_timer_create(ui_status_timer_cb, 500, NULL);
  lv_timer_create(ui_camera_timer_cb, APP_CAMERA_REFRESH_MS, NULL);
}

static void AppLvgl_Task(void *param)
{
  (void)param;

  lv_init();
  lv_tick_set_cb(app_lvgl_tick_get_ms);
  lv_port_disp_init();
  lv_port_indev_init();
  app_lvgl_splash_create();

  AppUart_Printf("[LVGL] face-auth UI ready, %dx%d, free heap = %u bytes\r\n",
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
