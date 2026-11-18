#ifndef APP_DISPLAY_CONFIG_H
#define APP_DISPLAY_CONFIG_H

/* ATK-7016 7 寸 RGB 面板：1024x600，RGB565 */
#define DISPLAY_PANEL_W             1024U
#define DISPLAY_PANEL_H             600U

/* OV5640 保持 480x480，在右侧区域内垂直居中。 */
#define DISPLAY_CAMERA_W            480U
#define DISPLAY_CAMERA_H            480U

/* 左侧 LVGL 状态面板；相机占用最右侧 480 像素宽的窗口。 */
#define DISPLAY_LVGL_W              (DISPLAY_PANEL_W - DISPLAY_CAMERA_W)
#define DISPLAY_LVGL_H              DISPLAY_PANEL_H
#define DISPLAY_CAMERA_X0           DISPLAY_LVGL_W
#define DISPLAY_CAMERA_X1           DISPLAY_PANEL_W
#define DISPLAY_CAMERA_Y0           ((DISPLAY_PANEL_H - DISPLAY_CAMERA_H) / 2U)
#define DISPLAY_CAMERA_Y1           (DISPLAY_CAMERA_Y0 + DISPLAY_CAMERA_H)

/* 启动动画占满整屏，帧缓冲至少需要 1024x600x2 字节。 */
#define DISPLAY_FB_ADDR             0xC0000000UL
#define DISPLAY_FB_BYTES            (DISPLAY_PANEL_W * DISPLAY_PANEL_H * 2U)

/* 三块相机帧缓冲从 1.25MB 边界之后开始，避开 LVGL 全屏缓冲。 */
#define DISPLAY_CAM_FB0_ADDR        0xC0140000UL
#define DISPLAY_CAM_FB1_ADDR        0xC01C0000UL
#define DISPLAY_CAM_FB2_ADDR        0xC0240000UL

/* ATK-7016 的 LTDC 时序参数，单位为像素或行。 */
#define DISPLAY_HSW                 20U
#define DISPLAY_HBP                 140U
#define DISPLAY_HFP                 160U
#define DISPLAY_VSW                 3U
#define DISPLAY_VBP                 20U
#define DISPLAY_VFP                 12U

#endif /* APP_DISPLAY_CONFIG_H */
