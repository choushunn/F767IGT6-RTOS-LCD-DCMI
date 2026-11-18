# 状态优先人脸授权界面 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将摄像头扩展到 600x600，并把左侧界面重构为一次只显示一个当前步骤的状态优先授权面板。

**Architecture:** 显示布局仍由两路 LTDC 图层组成，左侧 424x600 使用 LVGL PARTIAL + DMA2D，右侧 600x600 使用独立三缓冲和 DCMI 持续采集。UI 状态机保留四态，但视觉层只显示当前步骤、当前状态、一个主要动作和异常提示。

**Tech Stack:** STM32F767IGT6、LTDC、DCMI、DMA2、DMA2D、FreeRTOS、LVGL v9、Keil AC6。

**Spec:** `docs/superpowers/specs/2026-11-18-status-first-face-auth-design.md`

## Global Constraints

- 面板分辨率为 1024x600，左侧 UI 为 424x600，右侧摄像头为 600x600。
- 摄像头窗口为 `x=424..1024`、`y=0..600`，UI 与相机无间隙。
- 三块相机帧缓冲地址保持不变：`0xC0140000`、`0xC0200000`、`0xC02C0000`。
- 相机 600x600 RGB565、1:1 输出；每行 300 个 32 位字。
- DMA 每块 24 行，共 7200 个字；每帧 25 块。
- 保持 DMA FIFO Full、内存 INCR4、NORMAL 逐块续排。
- 保持 `HSPolarity=LOW`、FMC `WriteRecoveryTime=2`、DMA2D `NLR` 正确顺序。
- 保留 `FACE_UI_DEMO_FLOW=1`，不得把演示流程描述为真实人脸识别。
- 不自动调用 Keil/UV4 编译；最终由用户按 F7 编译。
- 不自动提交或推送现有脏工作区。

## Review Focus

- 600 像素行宽对应的 1200B pitch 不是 64B 整数倍，必须通过实机 `fu/terr` 验证。
- 相机从 576 改为 600 后，每帧 DMA 块数必须从 24 改为 25。
- 三块 768KB 帧缓冲是否足以容纳 600x600 且不重叠。
- 只显示当前步骤时，状态切换是否会遗漏故障提示或重复触发动作。
- UI 与相机窗口必须在 x=424 处无缝衔接，不得出现覆盖或空隙。

---

### Task 1: 扩展到 600x600 摄像头

**Files:**
- Modify: `App/display_config.h`
- Modify: `App/camera/app_camera.c`
- Modify: `Core/Src/ltdc.c`
- Modify: `F767IGT6-RTOS-LVGL-LCD-DCMI.ioc`
- Modify: `README.md`
- Modify: `Docs/04.DCMI摄像头模块方案.md`

**Interfaces:**
- Produces: `DISPLAY_LVGL_W=424`、`DISPLAY_CAMERA_W=600`、`DISPLAY_CAMERA_H=600`
- Produces: `CAM_IMG_W=600`、`CAM_IMG_H=600`
- Produces: `CAM_LINE_WORDS=300`、`CAM_CHUNK_WORDS=7200`、`CAM_CHUNKS_PER_FRAME=25`

- [x] **Step 1: 添加几何不变量检查**

在 `App/camera/app_camera.c` 保留并更新预处理检查：600 可被 4 整除、600 可被 24 整除、单块字数不超过 65535、块地址满足 16B 对齐。

- [x] **Step 2: 修改显示和相机尺寸**

将 `DISPLAY_LVGL_W` 改为 424、`DISPLAY_CAMERA_W/H` 改为 600、相机 X0 改为 424、Y0 改为 0；保留三块帧缓冲地址。

- [x] **Step 3: 修改 OV2640 裁切与输出**

`OV2640_ImageWin_Set(500, 300, 600, 600)`，`OV2640_OutSize_Set(600, 600)`。

- [x] **Step 4: 验证静态语法**

运行 armclang `-fsyntax-only`，期望无诊断。

- [x] **Step 5: 文档同步**

更新 README 与 DCMI 文档中的分辨率、窗口、每行字数、块数和每帧块数。

---

### Task 2: 状态优先且逐项显示的 LVGL 界面

**Files:**
- Modify: `App/lvgl/app_lvgl.c`
- Modify: `Docs/02.LVGL移植方案.md`

**Interfaces:**
- Consumes: `DISPLAY_LVGL_W=424`、`DISPLAY_LVGL_H=600`
- Produces: `FaceUiState_t` 四态状态机保持现有取值
- Produces: 单一当前步骤标签 `s_currentStep`

- [x] **Step 1: 删除完整流程轨道**

移除 `s_stepMarker[3]`、`s_stepLabel[3]`、`create_step()`、`track`、`stepsTitle` 以及相关状态更新。

- [x] **Step 2: 建立单步状态区**

在左侧加入 `当前步骤` 标签和一个动态步骤标签；一次只显示“人脸采集”、“身份比对”或“上机授权”之一。

- [x] **Step 3: 调整四态文案**

- IDLE：步骤“人脸采集”，标题“等待验证”，按钮“开始采集”。
- SCANNING：步骤“人脸采集”，标题“采集中”；进度过半后只切换步骤为“身份比对”、标题“正在比对”。
- VERIFIED：步骤“上机授权”，标题“授权通过”，按钮“重新验证”。
- REJECTED：步骤“人脸采集”，标题“验证未通过”，显示一条必要重试提示。

- [x] **Step 4: 删除冗余内容**

移除“高空作业吊篮”“人脸授权上机终端”、独立结果卡、采集/比对/授权全部并列展示，以及正常状态下的摄像头/触摸文字。设备健康仅以状态点显示，故障时才出现文字。

- [x] **Step 5: 检查交互**

确认按钮在采集中不可重复触发，状态切换只更新单一步骤，不重建整页。

- [x] **Step 6: 静态检查**

运行 armclang `-fsyntax-only`，检查无残留 `s_stepMarker`、`s_stepLabel`、`create_step` 引用。

---

### Task 3: 编译后实机验证

**Files:**
- Read: `App/display_config.h`
- Read: `App/camera/app_camera.c`
- Read: `LVGL/lv_port_disp.c`

**Interfaces:**
- Consumes: Task 1 的相机几何和 DMA 分块
- Consumes: Task 2 的状态优先界面

- [ ] **Step 1: 用户手动编译**

用户在 Keil 中按 F7 并下载；不以命令行 UV4 代替。

- [ ] **Step 2: 串口基线**

连续采集至少 10 秒，确认 600x600、7200 words x 25 chunks、`pub` 增长且无持续错误增长。

- [ ] **Step 3: SWD 显存验证**

读取 UISDRAM 和相机三块帧缓冲，确认 UI 为 424x600、相机为 600x600；检查像素分布不再出现低字节等于高字节的主模式。

- [ ] **Step 4: 硬件性能验证**

读取 LTDC FIFO 下溢、LTDC 传输错误、DCMI OVR、DMA FE。若 1200B pitch 导致持续下溢，再评估 640 像素内部 pitch 和 40 像素裁切方案。

- [ ] **Step 5: 视觉验收**

确认左侧逐项显示，不出现完整三段流程；右侧从 x=424 铺满到屏幕右边界。
