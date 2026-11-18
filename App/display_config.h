#ifndef APP_DISPLAY_CONFIG_H
#define APP_DISPLAY_CONFIG_H

/* ATK-7016 7 寸 RGB 面板：1024x600，RGB565 */
#define DISPLAY_PANEL_W             1024U
#define DISPLAY_PANEL_H             600U

/* 摄像头占满右侧 600x600；左侧 UI 使用剩余 424 像素。 */
#define DISPLAY_CAMERA_W            600U
#define DISPLAY_CAMERA_H            600U

/* 左侧 LVGL 人脸授权界面；相机从 UI 右边沿无缝铺到屏幕右边界。 */
#define DISPLAY_LVGL_W              424U
#define DISPLAY_LVGL_H              DISPLAY_PANEL_H
#define DISPLAY_CAMERA_X0           DISPLAY_LVGL_W
#define DISPLAY_CAMERA_X1           (DISPLAY_CAMERA_X0 + DISPLAY_CAMERA_W)
#define DISPLAY_CAMERA_Y0           0U
#define DISPLAY_CAMERA_Y1           (DISPLAY_CAMERA_Y0 + DISPLAY_CAMERA_H)

/* 启动动画占满整屏，帧缓冲至少需要 1024x600x2 字节。 */
#define DISPLAY_FB_ADDR             0xC0000000UL
#define DISPLAY_FB_BYTES            (DISPLAY_PANEL_W * DISPLAY_PANEL_H * 2U)

/* 三块 600x600 相机帧缓冲按 768KB 间隔排列，单帧占 720000B。 */
#define DISPLAY_CAM_FB0_ADDR        0xC0140000UL
#define DISPLAY_CAM_FB1_ADDR        0xC0200000UL
#define DISPLAY_CAM_FB2_ADDR        0xC02C0000UL

/* ATK-7016 的 LTDC 时序参数，单位为像素或行。 */
#define DISPLAY_HSW                 20U
#define DISPLAY_HBP                 140U
#define DISPLAY_HFP                 160U
#define DISPLAY_VSW                 3U
#define DISPLAY_VBP                 20U
#define DISPLAY_VFP                 12U

#endif /* APP_DISPLAY_CONFIG_H */
