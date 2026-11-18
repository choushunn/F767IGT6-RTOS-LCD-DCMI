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

#define APP_UI_W               DISPLAY_UI_W
#define APP_UI_H               DISPLAY_UI_H

#define UI_COLOR_BG            0x0B0E11
#define UI_COLOR_SURFACE       0x171C22
#define UI_COLOR_SURFACE_2     0x20262E
#define UI_COLOR_TEXT          0xE5E9EF
#define UI_COLOR_MUTED         0x87929F
#define UI_COLOR_SAFETY        0xF2C94C
#define UI_COLOR_OK            0x18C29C
#define UI_COLOR_DANGER        0xE34D59

/* UI-review flow only. Set to 0 when face-recognition results are connected. */
#define FACE_UI_DEMO_FLOW      1U

typedef enum
{
  FACE_UI_IDLE = 0,
  FACE_UI_PREVIEW,
  FACE_UI_PHOTO_READY,
  FACE_UI_SCANNING,
  FACE_UI_VERIFIED,
  FACE_UI_REJECTED
} FaceUiState_t;

static lv_obj_t *s_previewArea;
static lv_obj_t *s_adViewport;
static lv_obj_t *s_adTrack;
static lv_obj_t *s_stateTitle;
static lv_obj_t *s_progressArc;
static lv_obj_t *s_progressValue;
static lv_obj_t *s_stepLabel[3];
static lv_obj_t *s_stepDot[3];
static lv_obj_t *s_stepLine[2];
static lv_obj_t *s_actionButton;
static lv_obj_t *s_actionLabel;
static lv_obj_t *s_retakeButton;
static lv_obj_t *s_retakeLabel;
static lv_obj_t *s_deviceError;

static lv_obj_t *s_splashRoot;
static lv_obj_t *s_splashBar;
static lv_obj_t *s_splashPct;
static lv_obj_t *s_splashHint;

static lv_timer_t *s_faceTimer;
static lv_timer_t *s_cameraTimer;
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

static void set_action_button(const char *text,
                              uint32_t background,
                              uint32_t foreground,
                              uint8_t disabled)
{
  lv_label_set_text(s_actionLabel, text);
  lv_obj_set_style_bg_color(s_actionButton, lv_color_hex(background), 0);
  lv_obj_set_style_border_width(s_actionButton, 0, 0);
  set_text_color(s_actionLabel, foreground);

  if (disabled != 0U)
  {
    lv_obj_add_state(s_actionButton, LV_STATE_DISABLED);
  }
  else
  {
    lv_obj_remove_state(s_actionButton, LV_STATE_DISABLED);
  }
}

static void set_progress(uint8_t progress, uint32_t color, const char *text)
{
  s_scanProgress = progress;
  lv_arc_set_value(s_progressArc, (int32_t)progress);
  lv_obj_set_style_arc_color(s_progressArc,
                             lv_color_hex(color),
                             LV_PART_INDICATOR);
  lv_label_set_text(s_progressValue, text);
}

static void update_flow_steps(uint8_t activeStep, uint32_t activeColor)
{
  uint8_t i;

  for (i = 0U; i < 3U; i++)
  {
    uint32_t color;

    if (i < activeStep)
    {
      color = UI_COLOR_OK;
    }
    else if (i == activeStep)
    {
      color = activeColor;
    }
    else
    {
      color = UI_COLOR_MUTED;
    }

    set_text_color(s_stepLabel[i], color);
    lv_obj_set_style_bg_color(s_stepDot[i], lv_color_hex(color), 0);
  }

  for (i = 0U; i < 2U; i++)
  {
    uint32_t color = (activeStep > i) ? UI_COLOR_OK : UI_COLOR_MUTED;
    lv_obj_set_style_bg_color(s_stepLine[i], lv_color_hex(color), 0);
  }
}

static void apply_face_state(FaceUiState_t state)
{
  s_faceState = state;

  if (state == FACE_UI_IDLE)
  {
    lv_obj_set_hidden(s_adViewport, false);
    lv_port_disp_set_visible_area(0,
                                  0,
                                  (int32_t)APP_UI_W - 1,
                                  (int32_t)APP_UI_H - 1);
  }
  else
  {
    lv_obj_set_hidden(s_adViewport, true);
    lv_port_disp_set_visible_area(0,
                                  (int32_t)DISPLAY_AD_H,
                                  (int32_t)APP_UI_W - 1,
                                  (int32_t)APP_UI_H - 1);
  }

  if (state == FACE_UI_PHOTO_READY)
  {
    lv_obj_set_pos(s_actionButton, 308, 888);
    lv_obj_set_size(s_actionButton, 248, 76);
    lv_obj_set_pos(s_retakeButton, 44, 888);
    lv_obj_set_size(s_retakeButton, 248, 76);
    lv_obj_set_hidden(s_retakeButton, false);
  }
  else
  {
    lv_obj_set_pos(s_actionButton, 44, 888);
    lv_obj_set_size(s_actionButton, 512, 76);
    lv_obj_set_hidden(s_retakeButton, true);
  }

  switch (state)
  {
    case FACE_UI_IDLE:
      lv_label_set_text(s_stateTitle, "等待上机验证");
      set_text_color(s_stateTitle, UI_COLOR_TEXT);
      set_progress(0U, UI_COLOR_SAFETY, "0%");
      update_flow_steps(0U, UI_COLOR_SAFETY);
      set_action_button("开始上机",
                        UI_COLOR_SAFETY,
                        UI_COLOR_BG,
                        0U);
      break;

    case FACE_UI_PREVIEW:
      lv_label_set_text(s_stateTitle, "请正视摄像头");
      set_text_color(s_stateTitle, UI_COLOR_TEXT);
      set_progress(15U, UI_COLOR_SAFETY, "15%");
      update_flow_steps(1U, UI_COLOR_SAFETY);
      set_action_button("开始拍照",
                        UI_COLOR_SAFETY,
                        UI_COLOR_BG,
                        0U);
      break;

    case FACE_UI_PHOTO_READY:
      lv_label_set_text(s_stateTitle, "确认抓拍");
      set_text_color(s_stateTitle, UI_COLOR_TEXT);
      set_progress(30U, UI_COLOR_SAFETY, "30%");
      update_flow_steps(2U, UI_COLOR_SAFETY);
      set_action_button("开始验证",
                        UI_COLOR_SAFETY,
                        UI_COLOR_BG,
                        0U);
      break;

    case FACE_UI_SCANNING:
      lv_label_set_text(s_stateTitle, "身份比对");
      set_text_color(s_stateTitle, UI_COLOR_TEXT);
      update_flow_steps(3U, UI_COLOR_OK);
      set_action_button("验证中",
                        UI_COLOR_SURFACE_2,
                        UI_COLOR_MUTED,
                        1U);
      break;

    case FACE_UI_VERIFIED:
      lv_label_set_text(s_stateTitle, "授权通过");
      set_text_color(s_stateTitle, UI_COLOR_OK);
      set_progress(100U, UI_COLOR_OK, "100%");
      update_flow_steps(3U, UI_COLOR_OK);
      set_action_button("重新验证",
                        UI_COLOR_SURFACE,
                        UI_COLOR_OK,
                        0U);
      lv_obj_set_style_border_width(s_actionButton, 2, 0);
      lv_obj_set_style_border_color(s_actionButton,
                                    lv_color_hex(UI_COLOR_OK),
                                    0);
      break;

    case FACE_UI_REJECTED:
    default:
      lv_label_set_text(s_stateTitle, "验证未通过");
      set_text_color(s_stateTitle, UI_COLOR_DANGER);
      lv_obj_set_style_arc_color(s_progressArc,
                                 lv_color_hex(UI_COLOR_DANGER),
                                 LV_PART_INDICATOR);
      lv_label_set_text(s_progressValue, "未通过");
      update_flow_steps(1U, UI_COLOR_DANGER);
      set_action_button("重新验证",
                        UI_COLOR_DANGER,
                        UI_COLOR_TEXT,
                        0U);
      break;
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

static void stop_camera_timer(void)
{
  if (s_cameraTimer != NULL)
  {
    lv_timer_delete(s_cameraTimer);
    s_cameraTimer = NULL;
  }
}

static void ui_camera_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  AppCamera_PollFrame();
}

static void start_camera_timer(void)
{
  stop_camera_timer();
  s_cameraTimer = lv_timer_create(ui_camera_timer_cb,
                                  APP_CAMERA_REFRESH_MS,
                                  NULL);
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
  char progressText[8];

  (void)timer;

  s_scanProgress = (uint8_t)(s_scanProgress + 2U);
  if (s_scanProgress >= 100U)
  {
    s_scanProgress = 100U;
  }

  if (s_scanProgress >= 55U)
  {
    lv_label_set_text(s_stateTitle, "上机授权");
  }
  else
  {
    lv_label_set_text(s_stateTitle, "身份比对");
  }

  lv_arc_set_value(s_progressArc, (int32_t)s_scanProgress);
  lv_snprintf(progressText, sizeof(progressText), "%u%%", (unsigned)s_scanProgress);
  lv_label_set_text(s_progressValue, progressText);

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

static void start_preview(void)
{
  if (AppCamera_StartPreview() != 0U)
  {
    apply_face_state(FACE_UI_REJECTED);
    lv_label_set_text(s_stateTitle, "预览启动失败");
    lv_label_set_text(s_deviceError, "摄像头未就绪");
    lv_obj_set_hidden(s_deviceError, false);
    return;
  }

  apply_face_state(FACE_UI_PREVIEW);
  start_camera_timer();
}

static void start_photo_capture(void)
{
  AppCameraStats_t cam;

  AppCamera_GetStats(&cam);
  if (cam.frames == 0U)
  {
    lv_label_set_text(s_stateTitle, "请稍候");
    return;
  }

  stop_camera_timer();
  AppCamera_FreezePreview();
  stop_face_timer();
  apply_face_state(FACE_UI_PHOTO_READY);
}

static void start_face_scan(void)
{
  stop_face_timer();
  apply_face_state(FACE_UI_SCANNING);
  set_progress(30U, UI_COLOR_SAFETY, "30%");
  s_faceTimer = lv_timer_create(face_scan_timer_cb, 100, NULL);
}

static void action_button_cb(lv_event_t *event)
{
  (void)event;

  switch (s_faceState)
  {
    case FACE_UI_IDLE:
      start_preview();
      break;

    case FACE_UI_PREVIEW:
      start_photo_capture();
      break;

    case FACE_UI_PHOTO_READY:
      start_face_scan();
      break;

    case FACE_UI_VERIFIED:
    case FACE_UI_REJECTED:
      stop_camera_timer();
      AppCamera_StopPreview();
      stop_face_timer();
      apply_face_state(FACE_UI_IDLE);
      lv_obj_set_hidden(s_deviceError, true);
      break;

    case FACE_UI_SCANNING:
    default:
      break;
  }
}

static void retake_button_cb(lv_event_t *event)
{
  (void)event;

  if (s_faceState == FACE_UI_PHOTO_READY)
  {
    start_preview();
  }
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
    app_lvgl_ui_create();
  }
}

static void app_lvgl_splash_create(void)
{
  lv_obj_t *scr;
  lv_obj_t *accent;
  lv_obj_t *brand;
  lv_obj_t *subtitle;

  scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  accent = lv_obj_create(scr);
  lv_obj_set_pos(accent, 68, 152);
  lv_obj_set_size(accent, 12, 588);
  lv_obj_set_style_radius(accent, 0, 0);
  lv_obj_set_style_border_width(accent, 0, 0);
  lv_obj_set_style_bg_color(accent, lv_color_hex(UI_COLOR_SAFETY), 0);

  s_splashRoot = lv_obj_create(scr);
  lv_obj_set_pos(s_splashRoot, 112, 128);
  lv_obj_set_size(s_splashRoot, 424, 736);
  lv_obj_set_style_bg_opa(s_splashRoot, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_splashRoot, 0, 0);
  lv_obj_set_style_pad_all(s_splashRoot, 0, 0);
  lv_obj_set_style_pad_row(s_splashRoot, 20, 0);
  lv_obj_set_flex_flow(s_splashRoot, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_splashRoot,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  brand = create_label(s_splashRoot,
                       "身份安全授权",
                       &app_font_cn_24,
                       UI_COLOR_SAFETY);
  lv_obj_set_style_pad_top(brand, 12, 0);
  subtitle = create_label(s_splashRoot,
                          "设备 A-01  |  安全联锁启动中",
                          &app_font_cn_14,
                          UI_COLOR_MUTED);
  lv_obj_set_style_pad_bottom(subtitle, 36, 0);

  s_splashBar = lv_bar_create(s_splashRoot);
  lv_obj_set_size(s_splashBar, 360, 12);
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

  create_hazard_strip(scr, 68, 824, 464);
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
  lv_obj_set_size(dot, 10, 10);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(UI_COLOR_MUTED), 0);
  return dot;
}

static lv_obj_t *create_flow_line(lv_obj_t *parent,
                                  int32_t x,
                                  int32_t y,
                                  int32_t width)
{
  lv_obj_t *line = lv_obj_create(parent);

  lv_obj_set_pos(line, x, y);
  lv_obj_set_size(line, width, 2);
  lv_obj_set_style_radius(line, 0, 0);
  lv_obj_set_style_border_width(line, 0, 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(line, lv_color_hex(UI_COLOR_MUTED), 0);
  return line;
}

static void ad_scroll_anim_cb(void *var, int32_t value)
{
  lv_obj_set_x((lv_obj_t *)var, value);
}

static void create_waiting_ad(lv_obj_t *scr)
{
  const uint32_t barColors[8] = {
    UI_COLOR_TEXT,
    UI_COLOR_SAFETY,
    0x2D9CDB,
    UI_COLOR_OK,
    0xBB6BD9,
    UI_COLOR_DANGER,
    0x2F80ED,
    UI_COLOR_BG
  };
  const int32_t barWidth = 100;
  uint8_t cycle;
  uint8_t colorIndex;
  lv_anim_t anim;

  s_adViewport = lv_obj_create(scr);
  lv_obj_set_pos(s_adViewport, (int32_t)DISPLAY_AD_X, (int32_t)DISPLAY_AD_Y);
  lv_obj_set_size(s_adViewport, (int32_t)DISPLAY_AD_W, (int32_t)DISPLAY_AD_H);
  lv_obj_set_style_radius(s_adViewport, 0, 0);
  lv_obj_set_style_border_width(s_adViewport, 0, 0);
  lv_obj_set_style_pad_all(s_adViewport, 0, 0);
  lv_obj_set_style_bg_color(s_adViewport, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(s_adViewport, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(s_adViewport, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(s_adViewport, LV_DIR_NONE);
  lv_obj_set_clickable(s_adViewport, false);

  s_adTrack = lv_obj_create(s_adViewport);
  lv_obj_set_pos(s_adTrack, 0, 0);
  lv_obj_set_size(s_adTrack, barWidth * 16, (int32_t)DISPLAY_AD_H);
  lv_obj_set_style_radius(s_adTrack, 0, 0);
  lv_obj_set_style_border_width(s_adTrack, 0, 0);
  lv_obj_set_style_pad_all(s_adTrack, 0, 0);
  lv_obj_set_style_bg_opa(s_adTrack, LV_OPA_TRANSP, 0);
  lv_obj_set_clickable(s_adTrack, false);

  for (cycle = 0U; cycle < 2U; cycle++)
  {
    for (colorIndex = 0U; colorIndex < 8U; colorIndex++)
    {
      int32_t index = ((int32_t)cycle * 8) + (int32_t)colorIndex;
      lv_obj_t *bar = lv_obj_create(s_adTrack);

      lv_obj_set_pos(bar, index * barWidth, 0);
      lv_obj_set_size(bar, barWidth, (int32_t)DISPLAY_AD_H);
      lv_obj_set_style_radius(bar, 0, 0);
      lv_obj_set_style_border_width(bar, 0, 0);
      lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(bar,
                                lv_color_hex(barColors[colorIndex]),
                                0);
    }
  }

  lv_anim_init(&anim);
  lv_anim_set_var(&anim, s_adTrack);
  lv_anim_set_values(&anim, 0, -(barWidth * 8));
  lv_anim_set_duration(&anim, 12000U);
  lv_anim_set_path_cb(&anim, lv_anim_path_linear);
  lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&anim, ad_scroll_anim_cb);
  (void)lv_anim_start(&anim);
}

static void create_flow_steps(lv_obj_t *scr)
{
  const char *texts[3] = {"开始上机", "开始拍照", "开始验证"};
  const int32_t labelX[3] = {24, 224, 424};
  const int32_t dotX[3] = {99, 295, 491};
  uint8_t i;

  for (i = 0U; i < 3U; i++)
  {
    s_stepLabel[i] = create_label(scr,
                                  texts[i],
                                  &app_font_cn_14,
                                  UI_COLOR_MUTED);
    lv_obj_set_pos(s_stepLabel[i], labelX[i], 808);
    lv_obj_set_width(s_stepLabel[i], 152);
    lv_obj_set_style_text_align(s_stepLabel[i], LV_TEXT_ALIGN_CENTER, 0);

    s_stepDot[i] = create_dot(scr, dotX[i], 840);
  }

  s_stepLine[0] = create_flow_line(scr, 109, 844, 186);
  s_stepLine[1] = create_flow_line(scr, 305, 844, 186);
}

static void app_lvgl_ui_create(void)
{
  lv_obj_t *scr;
  lv_obj_t *arcLabel;

  scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  s_previewArea = lv_obj_create(scr);
  lv_obj_set_pos(s_previewArea, (int32_t)DISPLAY_PREVIEW_X, (int32_t)DISPLAY_PREVIEW_Y);
  lv_obj_set_size(s_previewArea,
                  (int32_t)DISPLAY_PREVIEW_W,
                  (int32_t)DISPLAY_PREVIEW_H);
  lv_obj_set_style_radius(s_previewArea, 10, 0);
  lv_obj_set_style_bg_color(s_previewArea, lv_color_hex(UI_COLOR_SURFACE), 0);
  lv_obj_set_style_bg_opa(s_previewArea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_previewArea, 1, 0);
  lv_obj_set_style_border_color(s_previewArea,
                                lv_color_hex(UI_COLOR_SURFACE_2),
                                0);
  lv_obj_set_clickable(s_previewArea, false);

  create_waiting_ad(scr);

  s_stateTitle = create_label(scr,
                              "等待上机验证",
                              &app_font_cn_24,
                              UI_COLOR_TEXT);
  lv_obj_set_pos(s_stateTitle, 0, 608);
  lv_obj_set_width(s_stateTitle, APP_UI_W);
  lv_obj_set_style_text_align(s_stateTitle, LV_TEXT_ALIGN_CENTER, 0);

  s_progressArc = lv_arc_create(scr);
  lv_obj_set_pos(s_progressArc, 220, 644);
  lv_obj_set_size(s_progressArc, 160, 160);
  lv_arc_set_range(s_progressArc, 0, 100);
  lv_arc_set_bg_angles(s_progressArc, 0, 360);
  lv_arc_set_rotation(s_progressArc, 270);
  lv_arc_set_value(s_progressArc, 0);
  lv_obj_set_clickable(s_progressArc, false);
  lv_obj_set_style_arc_width(s_progressArc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_progressArc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_progressArc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(s_progressArc, true, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_progressArc,
                             lv_color_hex(UI_COLOR_SURFACE_2),
                             LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_progressArc,
                             lv_color_hex(UI_COLOR_SAFETY),
                             LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_progressArc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_border_width(s_progressArc, 0, LV_PART_KNOB);

  s_progressValue = create_label(s_progressArc, "0%", &app_font_cn_24, UI_COLOR_TEXT);
  arcLabel = s_progressValue;
  lv_obj_center(arcLabel);

  create_flow_steps(scr);

  s_deviceError = create_label(scr, "", &app_font_cn_14, UI_COLOR_DANGER);
  lv_obj_set_pos(s_deviceError, 44, 856);
  lv_obj_set_width(s_deviceError, 512);
  lv_obj_set_style_text_align(s_deviceError, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_hidden(s_deviceError, true);

  s_actionButton = lv_button_create(scr);
  lv_obj_set_pos(s_actionButton, 44, 888);
  lv_obj_set_size(s_actionButton, 512, 76);
  lv_obj_set_style_radius(s_actionButton, 8, 0);
  lv_obj_set_style_bg_color(s_actionButton, lv_color_hex(UI_COLOR_SAFETY), 0);
  lv_obj_set_style_bg_opa(s_actionButton, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_actionButton, 0, 0);
  lv_obj_set_style_shadow_width(s_actionButton, 0, 0);
  lv_obj_add_event_cb(s_actionButton, action_button_cb, LV_EVENT_CLICKED, NULL);

  s_actionLabel = create_label(s_actionButton,
                               "开始上机",
                               &app_font_cn_24,
                               UI_COLOR_BG);
  lv_obj_center(s_actionLabel);

  s_retakeButton = lv_button_create(scr);
  lv_obj_set_pos(s_retakeButton, 44, 888);
  lv_obj_set_size(s_retakeButton, 248, 76);
  lv_obj_set_style_radius(s_retakeButton, 8, 0);
  lv_obj_set_style_bg_color(s_retakeButton,
                            lv_color_hex(UI_COLOR_SURFACE),
                            0);
  lv_obj_set_style_bg_opa(s_retakeButton, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_retakeButton, 2, 0);
  lv_obj_set_style_border_color(s_retakeButton,
                                lv_color_hex(UI_COLOR_SAFETY),
                                0);
  lv_obj_set_style_shadow_width(s_retakeButton, 0, 0);
  lv_obj_add_event_cb(s_retakeButton, retake_button_cb, LV_EVENT_CLICKED, NULL);

  s_retakeLabel = create_label(s_retakeButton,
                               "重拍",
                               &app_font_cn_24,
                               UI_COLOR_SAFETY);
  lv_obj_center(s_retakeLabel);

  apply_face_state(FACE_UI_IDLE);
  lv_timer_create(ui_status_timer_cb, 500, NULL);

  /* 相机轮询只在 PREVIEW 状态启动；待机、冻结和结果状态不持续采集。 */
}

static void AppLvgl_Task(void *param)
{
  (void)param;

  lv_init();
  lv_tick_set_cb(app_lvgl_tick_get_ms);
  lv_port_disp_init();
  lv_port_indev_init();
  app_lvgl_splash_create();

  AppUart_Printf("[LVGL] portrait face-auth UI ready, %dx%d\r\n",
                 (int)APP_UI_W,
                 (int)APP_UI_H);

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
