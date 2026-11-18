#ifndef APP_CAMERA_H
#define APP_CAMERA_H

#include <stdint.h>

#include "display_config.h"

/* 界面轮询周期：40ms（只取最新完整帧，不追帧） */
#define APP_CAMERA_REFRESH_MS   40U

/* 相机帧缓冲：位于 LVGL 全屏帧缓冲的 1228800B 之后，三块按 768KB 间隔排列。
 * 必须三块：双缓冲时发布新帧后 CFBAR 要等垂直消隐才生效，
 * 期间 DMA 写入的另一块正是 LTDC 仍在扫描的旧帧，必现撕裂。 */
#define CAM_FB0_ADDR            DISPLAY_CAM_FB0_ADDR
#define CAM_FB1_ADDR            DISPLAY_CAM_FB1_ADDR
#define CAM_FB2_ADDR            DISPLAY_CAM_FB2_ADDR
#define CAM_FB_NUM              3U

#define CAM_IMG_W               DISPLAY_CAMERA_W
#define CAM_IMG_H               DISPLAY_CAMERA_H

/* 图像方向（按实机装配调整）：OV2640 sensor 0x04 的 bit[7:6]。 */
#define CAM_MIRROR_H            1U
#define CAM_FLIP_V              1U

/* 相机状态快照：供左侧人脸授权界面与诊断日志使用 */
typedef struct
{
  uint32_t fps;      /* 最近 0.5s 统计的帧率 */
  uint32_t frames;   /* 累计发布帧数 */
  uint32_t esync;    /* DCMI 同步错误累计 */
  uint32_t eovr;     /* DCMI 溢出错累计（丢像素，会触发采集流重排） */
  uint32_t dmaerr;   /* DMA 传输错误累计 */
  uint32_t desync;   /* 采集流错位校正次数 */
  uint8_t  ready;    /* 1 = 相机已初始化并开始采集 */
} AppCameraStats_t;

/* 调度器启动前调用：传感器上电复位、SCCB 配置、DCMI/DMA 启动 */
void AppCamera_Init(void);

/* LVGL 任务内按 APP_CAMERA_REFRESH_MS 周期调用：首帧使能图层、错位重排与统计打印 */
void AppCamera_PollFrame(void);

/* 读取相机状态快照（任务上下文调用） */
void AppCamera_GetStats(AppCameraStats_t *stats);

#endif /* APP_CAMERA_H */
