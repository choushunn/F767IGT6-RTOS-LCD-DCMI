# 竖屏人脸验证 UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将现有横向授权界面改为实机确认的 270°、600×1024 竖屏 UI，包含 512×512 按需相机预览、环形进度和底部单步流程。

**Architecture:** LVGL 仍使用原生 1024×600 帧缓冲，通过显示旋转把逻辑画布变成 600×1024；触摸坐标执行反向旋转。UI 设计阶段关闭相机硬件图层，只呈现可交互的竖屏界面和演示进度。

**Tech Stack:** STM32F767IGT6、LTDC、DMA2D、FreeRTOS、LVGL v9、Keil AC6。

**Spec:** `Docs/superpowers/specs/2026-11-18-portrait-face-auth-ui-design.md`

## Global Constraints

- 物理面板保持 `1024×600`，逻辑界面为 `600×1024`。
- 实机确认旋转方向为 270°；LVGL 输入层自动匹配显示旋转。
- 512×512 预览位固定为 `x=44, y=44`。
- 待机不显示实时相机画面，LAYER 2 在 UI 设计阶段保持关闭。
- 主按钮文字顺序为“开始上机 / 开始拍照 / 开始验证”，验证中不可重复触发。
- 保留 `FACE_UI_DEMO_FLOW=1`，不得描述为真实人脸识别。
- 新增中文文案必须进入 `app_font_cn_14.c` 和 `app_font_cn_24.c`。
- 不自动调用 UV4/Keil；最终由用户按 F7 编译并下载。
- 不自动提交或推送，保留当前未跟踪的本地辅助目录。

## Review Focus

- 顺时针旋转和触摸逆变换必须成对出现，否则底部按钮可见但点不中。
- PARTIAL flush 旋转后的目标区域必须落在原生 1024×600 帧缓冲内。
- 512×512 预览位加 44px 留白后不得越界或挤压底部流程。
- 正在验证时按钮必须禁用，防止重复启动计时器。
- 新增“拍照”字形若缺失，LCD 会显示方框或空白，必须检查字体生成参数。

---

### Task 1: 建立竖屏显示与触摸坐标基础

**Files:**
- Modify: `App/display_config.h`
- Modify: `LVGL/lv_port_disp.c`
- Modify: `LVGL/lv_port_disp.h`
- Modify: `LVGL/lv_port_indev.c`
- Modify: `Core/Src/ltdc.c`
- Modify: `App/camera/app_camera.c`

**Interfaces:**
- Produces: `DISPLAY_UI_W=600U`
- Produces: `DISPLAY_UI_H=1024U`
- Produces: `DISPLAY_PREVIEW_W=512U`
- Produces: `DISPLAY_PREVIEW_H=512U`
- Produces: `DISPLAY_PREVIEW_X=44`
- Produces: `DISPLAY_PREVIEW_Y=44`

- [x] **Step 1: 添加竖屏几何不变量**

在 `App/display_config.h` 增加编译期检查：预览水平居中，预览底部不越过状态区起点，逻辑分辨率必须是原生分辨率的 90° 交换。

```c
#if ((DISPLAY_PREVIEW_X * 2U + DISPLAY_PREVIEW_W) != DISPLAY_UI_W)
#error "512x512 preview must be horizontally centered"
#endif
#if ((DISPLAY_PREVIEW_Y + DISPLAY_PREVIEW_H) >= DISPLAY_UI_H)
#error "preview must leave room for status and controls"
#endif
```

- [x] **Step 2: 配置原生面板与竖屏逻辑坐标**

保留 `DISPLAY_PANEL_W=1024U`、`DISPLAY_PANEL_H=600U`，新增 `DISPLAY_UI_W=600U`、`DISPLAY_UI_H=1024U`，预览常量固定为 `512×512` 和 `x=y=44`。

- [x] **Step 3: 将 LTDC LAYER 1 调整为全屏**

把 `Core/Src/ltdc.c` 的 LAYER 1 窗口改为 `0..DISPLAY_PANEL_W`、`0..DISPLAY_PANEL_H`；LAYER 2 保持初始关闭。

- [x] **Step 4: 在显示移植层启用实机确认的 270° 旋转**

`lv_display_create()` 使用原生 `1024×600`，随后调用：

```c
lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
```

- [x] **Step 5: 修改 flush 为旋转写入**

在 `lv_port_disp_flush_cb()` 中复制逻辑 area，调用 `lv_display_rotate_area()` 得到物理 area，再通过 `lv_draw_rotate()` 直接写入原生帧缓冲。旋转路径不使用 DMA2D；非旋转路径可保留现有 DMA2D 实现。

- [x] **Step 6: 保持 LVGL 默认触摸旋转处理**

`lv_port_indev_read_cb()` 只上报原始 1024×600 物理坐标。LVGL v9 的输入层会根据显示旋转自动转换；回调内不能再调用旋转函数。

- [x] **Step 7: 移除动态区域切换依赖**

删除 `lv_port_disp_set_area()` 的调用和声明，启动动画与主界面都使用固定 600×1024 逻辑画布。

- [x] **Step 8: 移除相机横屏拼合的过时断言**

在 `App/camera/app_camera.c` 删除 `DISPLAY_LVGL_W + DISPLAY_CAMERA_W == DISPLAY_PANEL_W` 和 `DISPLAY_CAMERA_H == DISPLAY_PANEL_H` 两项旧布局断言；保留全部 OV2640/DMA 几何断言。

- [x] **Step 9: 静态检查旋转 API 与目标区域**

运行：

```powershell
rg -n "lv_display_set_rotation|lv_display_rotate_area|lv_draw_rotate" LVGL
```

期望显示初始化与 flush 使用 `LV_DISPLAY_ROTATION_270`，输入回调直接上报原始物理坐标。

---

### Task 2: 重构为 600×1024 竖屏授权界面

**Files:**
- Modify: `App/lvgl/app_lvgl.c`
- Modify: `App/lvgl/app_font_cn_14.c`
- Modify: `App/lvgl/app_font_cn_24.c`

**Interfaces:**
- Consumes: `DISPLAY_UI_W=600`、`DISPLAY_UI_H=1024`
- Consumes: `DISPLAY_PREVIEW_X=44`、`DISPLAY_PREVIEW_Y=44`、`DISPLAY_PREVIEW_W/H=512`
- Produces: `FaceUiState_t { FACE_UI_IDLE, FACE_UI_PREVIEW, FACE_UI_PHOTO_READY, FACE_UI_SCANNING, FACE_UI_VERIFIED, FACE_UI_REJECTED }`
- Produces: `lv_obj_t *s_progressArc`
- Produces: `lv_obj_t *s_actionButton`
- Produces: 状态标题 `y=584`、环形进度 `x=220, y=624, 160×160`、步骤行 `y=820`、按钮 `x=44, y=888, 512×76`

- [x] **Step 1: 重新生成中文字体**

保留两个字体文件头部原有的字符集合，并追加“拍照”两个字形，分别重新生成 14px 与 24px 字体。生成后确认两个文件头部 `--symbols` 都包含“拍照”。

- [x] **Step 2: 建立固定竖屏页面骨架**

页面底色设为 `#0B0E11`。创建：

- `x=44, y=44, 512×512` 的空预览容器。
- `y=584`、宽度 600、居中的状态标题。
- `x=220, y=624, 160×160` 环形进度。
- `y=820` 的三个步骤标签与连接线。
- `x=44, y=888, 512×76` 主按钮。

- [x] **Step 3: 创建环形进度**

使用 `lv_arc_create()`，范围 `0..100`，背景弧完整 360°，指示弧从顶部开始顺时针增长；线宽 14px、圆角端点。百分比位于圆环中央。

- [x] **Step 4: 建立单步流程显示**

步骤文字固定为“开始上机 / 开始拍照 / 开始验证”。只有当前步骤使用 `UI_COLOR_SAFETY`，已经过步骤使用 `UI_COLOR_OK`，未到达步骤使用 `UI_COLOR_MUTED`。

- [x] **Step 5: 实现六态交互**

- `FACE_UI_IDLE`：标题“等待上机验证”，弧值 0，按钮“开始上机”。
- `FACE_UI_PREVIEW`：标题“请正视摄像头”，弧值 15，按钮“开始拍照”。
- `FACE_UI_PHOTO_READY`：标题“确认抓拍”，弧值 30，主按钮“开始验证”，次要按钮“重拍”。
- `FACE_UI_SCANNING`：按钮禁用，弧值递增；55% 前显示“身份比对”，55% 后显示“上机授权”。
- `FACE_UI_VERIFIED`：弧值 100，标题“授权通过”，按钮“重新验证”。
- `FACE_UI_REJECTED`：标题“验证未通过”，按钮“重新验证”。

- [x] **Step 6: 实现按钮状态转换**

IDLE 点击后进入 PREVIEW；PREVIEW 点击后冻结照片并进入 PHOTO_READY；PHOTO_READY 的主按钮启动验证计时器进入 SCANNING，次按钮“重拍”重新启动预览并回到 PREVIEW；VERIFIED 或 REJECTED 点击后清理计时器并回到 IDLE。SCANNING 状态下按钮保持禁用。

- [x] **Step 7: 保持相机图层关闭**

删除 `ui_camera_timer_cb()` 和 `lv_timer_create(ui_camera_timer_cb, ...)`。UI 设计阶段不再调用 `AppCamera_PollFrame()`，因此 LAYER 2 不会被首帧路径启用。

- [x] **Step 8: 重做启动动画为竖屏**

启动动画使用 600×1024 逻辑坐标，保持现有安全黄、深色底和自检文案；结束时清理屏幕并直接创建主界面，不改变显示分辨率。

- [x] **Step 9: 静态检查禁止残留横屏坐标**

运行：

```powershell
rg -n "424|APP_UI_HOR_RES|APP_UI_FULL_W|lv_port_disp_set_area|ui_camera_timer_cb" App/lvgl LVGL/lv_port_disp.c
```

期望没有横屏 424px 布局、动态区域切换或相机首帧轮询残留。

---

### Task 3: 文档、构建检查与交付

**Files:**
- Modify: `README.md`
- Modify: `Docs/02.LVGL移植方案.md`
- Modify: `Docs/05.高空作业人脸授权UI.md`

**Interfaces:**
- Consumes: Task 1 的 600×1024 显示与触摸转换
- Consumes: Task 2 的六态 UI

- [x] **Step 1: 同步界面文档**

把 README 和两份 LVGL/UI 文档中的 424×600 横向布局更新为 600×1024 竖屏、512×512 预览位和底部单步流程，并标明相机按需启停尚未接入。

- [x] **Step 2: 检查字体生成声明**

运行：

```powershell
Select-String -Path App/lvgl/app_font_cn_14.c,App/lvgl/app_font_cn_24.c -Pattern "拍照"
```

期望两个文件头部都能找到字符声明。

- [x] **Step 3: 检查 Keil 工程无新增文件**

运行：

```powershell
git status --short
```

期望没有新增 `.c` 文件需要手工加入 `.uvprojx`；未跟踪本地辅助目录保持未暂存。

- [ ] **Step 4: 用户手动编译**

用户在 Keil 中按 F7，期望 0 errors；如出现旋转 API 未声明或字体符号缺失，先修复对应移植层/字体文件。

- [ ] **Step 5: 实机方向与触摸验收**

用户下载后检查：

1. 屏幕物理竖放时，文字方向正确，底部按钮位于真实屏幕底部。
2. 触控位置与可见按钮一致。
3. 512×512 区域水平居中，四周留白。
4. 等待状态不出现相机画面。
5. 点击顺序为“开始上机 → 开始拍照 → 开始验证”；拍照后先确认照片，再启动验证。
6. 环形进度连续更新且没有闪屏或撕裂。
