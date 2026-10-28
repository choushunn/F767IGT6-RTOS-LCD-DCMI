# STM32F767IGT6-RTOS-LVGL-LCD-DCMI

STM32F767IGT6 上基于 **FreeRTOS + LVGL v9** 的显示框架：LTDC 并口 RGB 屏（ATK-7084，800x480 / RGB565），帧缓冲置于外部 SDRAM。画面分左右两栏——左栏 320 宽为 LVGL 状态面板（图层 0），右栏 480 宽为 OV5640 实时预览（图层 2 硬件扫描），并已接入 GT9xxx 电容触摸。

## 1. 硬件与工具链

| 项目 | 说明 |
|------|------|
| 主控 | STM32F767IGT6（Cortex-M7，216MHz，HSE 25MHz） |
| 显示 | ATK-7084 7 寸 RGB 屏，800x480，RGB565，LTDC 像素时钟 33MHz（PLLSAI 396/3/4） |
| 帧缓冲 | 外部 SDRAM（FMC Bank1，基址 `0xC0000000`，32MB）：LVGL 320x480x2 = 300KB + 相机 3 x 460800B |
| 摄像头 | OV5640，DCMI 8 位并口 + DMA2_Stream1 按行双缓冲，480x480 RGB565，实测 15fps |
| RTOS | FreeRTOS Kernel V11.3.1，`portable/GCC/ARM_CM7/r0p1`，heap_4 |
| GUI | LVGL v9.6.0（lite），`LV_OS_NONE` 单任务内联渲染，DIRECT 单缓冲 |
| 编译器 | Keil MDK AC6 / armclang（6.24） |
| 工程 | `MDK-ARM/F767IGT6-RTOS-LVGL-LCD-DCMI.uvprojx` |

## 2. 时钟与时基

| 层 | 时钟源 | 频率 | 说明 |
|----|--------|------|------|
| 系统 | HSE 25MHz → PLL(M=25,N=432,P=2) + OverDrive | 216MHz | AHB 216MHz，APB1 54MHz，APB2 108MHz |
| HAL | TIM7 | 1kHz | `TIM7_IRQHandler → HAL_TIM_PeriodElapsedCallback → HAL_IncTick()`，`HAL_Delay/HAL_GetTick` 唯一来源 |
| FreeRTOS | SysTick | 1kHz | `SysTick_Handler → xPortSysTickHandler()`，**不得**再调用 `HAL_IncTick()` |
| LVGL | SysTick（`xTaskGetTickCount()`） | 1kHz | `lv_tick_set_cb()` 绑定，不再使用 `lv_tick_inc()` |

## 3. 目录结构

```
Core/                      CubeMX 生成的 HAL 初始化代码（main/ltdc/fmc/dma2d/...）
FreeRTOS/
  Source/                  内核源码（tasks/queue/list/timers/event_groups/stream_buffer）
  Source/portable/GCC/ARM_CM7/r0p1   Cortex-M7 移植层
  Source/portable/MemMang/heap_4.c
  Config/FreeRTOSConfig.h  应用配置（216MHz、1kHz tick、4 位优先级、64KB 堆）
LVGL/
  lvgl/                    LVGL v9.6.0 源码（src/ + include/）
  lv_conf.h                RGB565 / 64KB 内存池 / LV_OS_NONE / DMA2D 暂关闭
  lv_port_disp.c/.h        LTDC + SDRAM 帧缓冲显示移植层（DIRECT）
  lv_port_indev.c/.h       指针型输入设备（GT9xxx 触摸）
App/
  app_config.h             任务优先级/栈/周期集中配置
  app_tasks.c/.h           统一任务创建入口 AppTasks_Init()
  led/app_led.c/.h         LED0（PB1）500ms 翻转任务
  uart/app_uart.c/.h       USART1 调试打印任务 AppUart_Printf()
  touch/touch_detect.c/.h  GT9xxx 电容触摸（软件 I2C）+ LVGL 指针设备接入
  lvgl/app_lvgl.c/.h       LVGL 任务、启动动画与左侧状态面板（320x480）
  lvgl/app_font_cn_14.c    子集中文字库（黑体 14px / 4bpp，约 40KB，lv_font_conv 生成）
  camera/app_camera.c/.h   OV5640 采集、LTDC 第二图层换帧与帧率统计
  camera/ov5640.c/.h       OV5640 寄存器驱动（含 ov5640cfg.h 官方寄存器表）
  camera/sccb.c/.h         软件 SCCB（PB4=SCL / PB3=SDA）
  camera/pcf8574.c/.h      软件 I2C（PH4/PH5）+ PCF8574，摄像头 PWDN 由 P2 控制
  camera/camera_util.c/.h  DWT 微秒延时与 HAL 毫秒延时
Docs/                      移植方案与硬件参考资料
```

## 4. 编译与运行

1. 用 Keil 打开 `MDK-ARM/F767IGT6-RTOS-LVGL-LCD-DCMI.uvprojx`，确认 Options for Target → Target → ARM Compiler 为 **Arm Compiler 6**。
2. Rebuild（首次需编译 FreeRTOS + 全部 LVGL 源码，耗时较长）。
3. 下载并复位。

运行现象：

- 上电先显示**全屏启动动画**（800x480：系统启动 + 进度条 + 阶段文案，约 0.65s），期间图层 0 临时扩到整屏；加载完成后图层 0 收回左栏 320 宽。
- LCD 背光点亮（PB5 高电平），左栏 320x480 为 LVGL 状态面板：
  - 相机：帧率、累计帧数、错误计数（同步/溢出/DMA）、采集状态
  - 系统：运行时间、FreeRTOS 剩余堆、LVGL 内存池、触摸芯片状态
  - 板载：LED0 闪烁指示灯、LED1 触摸开关、蜂鸣器（PCF8574 P0）触摸开关
- 右栏（x 320~800）为 OV5640 实时画面（480x480 RGB565），由 LTDC 第二图层直接扫描
  `0xC0100000`/`0xC0180000`/`0xC0200000` 三块帧缓冲，首帧写满后才使能图层，换帧在 DMA 行中断内完成。
- LED0（DS0 / PB1）以 500ms 周期闪烁（LED 低电平点亮）。
- 触摸屏（GT9xxx，软件 I2C，SCL=PH6 / SDA=PI3 / RST=PI8 / INT=PH7）探测成功后可操作左栏开关。
- USART1（PA9/PA10，115200 8N1）输出：
  - `[LVGL] init done, 320x480, free heap = ...`
  - `[cam] ov5640 id ok`、`[cam] sensor out=480x480 hts=... vts=...`、`[cam] capture started`、`[cam] init done`
  - 每 2s 一条 `[cam] fps=.. pub=.. vsync=.. esync=.. eovr=.. dmaerr=.. fu=.. terr=..`

## 5. 关键配置

- **MPU**：FMC 数据区 `0x60000000`（64MB）全访问；SDRAM `0xC0000000`（32MB）全访问且**非缓存、可共享**，保证 CPU 与 LTDC/DMA2D 数据一致。FMC/QSPI 寄存器区（`0xA0000000`）走默认 Device 映射，切勿配置为无访问权限，否则 `MX_FMC_Init()` 会触发 MemManage 异常。
- **SDRAM 上电序列**：`HAL_SDRAM_Init()` 只配置控制器，`Core/Src/fmc.c` 的 `USER CODE 2` 区补齐 CLK_ENABLE → PALL → 8 次 AUTOREFRESH → LOAD_MODE(`0x230`) → 刷新计数 824（SDCLK 108MHz）。该序列必须先于 `MX_LTDC_Init()`。
- **LTDC**：时序 800x480（HSW=1/HBP=46/HFP=210，VSW=1/VBP=23/VFP=22），像素时钟 33MHz；图层 0 窗口 0~320（RGB565，帧缓冲 `0xC0000000`），图层 2 窗口 320~800 显示相机，混合因子必须 `PAxCA` + `Alpha0=0`，否则左栏会被盖黑。
- **相机换帧**：三缓冲 + 在 DMA 行中断内写 `CFBAR` 并置 `LTDC_SRCR_VBR`；双缓冲或任务轮询都会撕裂。LVGL 只渲染 320 宽，避免两图层 + DMA 压满 SDRAM 带宽（`fu` 计数增长即为带宽不足）。
- **FreeRTOS 中断桥接**：`Core/Src/stm32f7xx_it.c` 中 SVC→`vPortSVCHandler`、PendSV→`xPortPendSVHandler`、SysTick→`xPortSysTickHandler`，三者缺一任务即无法切换。
- **DMA2D**：`MX_DMA2D_Init()` 已按 RGB565 配置；LVGL 侧 `LV_USE_DRAW_DMA2D` 当前为 0，确认显示链路正常后可置 1 并重新编译。

## 6. 后续阶段

- 界面文案新增汉字时，需用 lv_font_conv 重新生成 `App/lvgl/app_font_cn_14.c`（否则缺字显示为空白）：
  `lv_font_conv --font C:/Windows/Fonts/simhei.ttf --size 14 --bpp 4 --format lvgl --no-compress -r 0x20-0x7E --symbols "界面用到的汉字" -o App/lvgl/app_font_cn_14.c`
- LVGL 界面模块化与更复杂的 UI 示例。
- 相机参数在线调节（亮度/对比度/特效）与拍照存储。

## 7. 参考资料

- `Docs/01.FreeRTOS移植方案.md`（FreeRTOS 移植方案）
- `Docs/02.LVGL移植方案.md`（LVGL 移植与排错）
- `Docs/03.触摸移植方案.md`（GT9xxx 触摸移植）
- `Docs/04.DCMI摄像头模块方案.md`（OV5640 采集与 LTDC 第二图层显示）
- `Docs/实验46 照相机实验_引脚配置分析.md`（板级引脚说明）
- FreeRTOS Kernel V11.3.1（MIT）、LVGL v9.6.0（MIT）原始许可证见各自目录。
