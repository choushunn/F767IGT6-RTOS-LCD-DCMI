#ifndef APP_DISPLAY_CONFIG_H
#define APP_DISPLAY_CONFIG_H

/* ATK-7016 7 寸 RGB 面板：1024x600，RGB565 */
#define DISPLAY_PANEL_W             1024U
#define DISPLAY_PANEL_H             600U

/* 物理面板保持横向 1024x600；竖屏安装后，LVGL 逻辑画布为 600x1024。 */
#define DISPLAY_UI_W                600U
#define DISPLAY_UI_H                1024U

/* 竖屏预览/冻结照片区域，四周先保留空白。 */
#define DISPLAY_PREVIEW_W           512U
#define DISPLAY_PREVIEW_H           512U
#define DISPLAY_PREVIEW_X           44U
#define DISPLAY_PREVIEW_Y           44U

/* 等待页顶部动态内容区；后续可替换为广告图片或视频帧。 */
#define DISPLAY_AD_W                DISPLAY_UI_W
#define DISPLAY_AD_H                600U
#define DISPLAY_AD_X                0U
#define DISPLAY_AD_Y                0U

/* 相机输出与竖屏预览区一致。LTDC 仍按原生横向坐标扫描，因此把 270° 竖屏
 * 逻辑窗口映射回物理坐标：portrait Y -> physical X，portrait X -> physical Y。 */
#define DISPLAY_CAMERA_W            DISPLAY_PREVIEW_W
#define DISPLAY_CAMERA_H            DISPLAY_PREVIEW_H

#define DISPLAY_CAMERA_X0 \
  (DISPLAY_PANEL_W - DISPLAY_PREVIEW_Y - DISPLAY_PREVIEW_H)
#define DISPLAY_CAMERA_X1           (DISPLAY_CAMERA_X0 + DISPLAY_CAMERA_W)
#define DISPLAY_CAMERA_Y0           DISPLAY_PREVIEW_X
#define DISPLAY_CAMERA_Y1           (DISPLAY_CAMERA_Y0 + DISPLAY_CAMERA_H)

/* 启动动画占满整屏，帧缓冲至少需要 1024x600x2 字节。 */
#define DISPLAY_FB_ADDR             0xC0000000UL
#define DISPLAY_FB_BYTES            (DISPLAY_PANEL_W * DISPLAY_PANEL_H * 2U)

/* 三块 512x512 相机帧缓冲按 768KB 间隔排列，单帧占 524288B。 */
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

#if ((DISPLAY_UI_W != DISPLAY_PANEL_H) || (DISPLAY_UI_H != DISPLAY_PANEL_W))
#error "portrait UI must be the native panel rotated by 90 degrees"
#endif
#if (((DISPLAY_PREVIEW_X * 2U) + DISPLAY_PREVIEW_W) != DISPLAY_UI_W)
#error "512x512 preview must be horizontally centered"
#endif
#if ((DISPLAY_PREVIEW_Y + DISPLAY_PREVIEW_H) >= DISPLAY_UI_H)
#error "preview must leave room for status and controls"
#endif
#if ((DISPLAY_AD_X + DISPLAY_AD_W) > DISPLAY_UI_W) || \
    ((DISPLAY_AD_Y + DISPLAY_AD_H) > DISPLAY_UI_H)
#error "advertising viewport must fit inside the portrait UI"
#endif
#if ((DISPLAY_CAMERA_X1 > DISPLAY_PANEL_W) || (DISPLAY_CAMERA_Y1 > DISPLAY_PANEL_H))
#error "camera preview window must fit inside the native panel"
#endif

#endif /* APP_DISPLAY_CONFIG_H */
