# STM32F767IGT6-RTOS-LVGL-LCD-DCMI

STM32F767IGT6 上基于 **FreeRTOS + LVGL v9** 的显示框架：LTDC 并口 RGB 屏（ATK-7016，1024x600 / RGB565）、帧缓冲置于外部 SDRAM，LVGL 以 FreeRTOS 任务方式周期刷新；DCMI 摄像头与触摸驱动预留，后续阶段接入。

## 1. 硬件与工具链

| 项目 | 说明 |
|------|------|
| 主控 | STM32F767IGT6（Cortex-M7，216MHz，HSE 25MHz） |
| 显示 | ATK-7016 7 寸 RGB 屏，1024x600，RGB565，LTDC 像素时钟 45MHz（PLLSAI 360/2/4） |
| 帧缓冲 | 外部 SDRAM（FMC Bank1，基址 `0xC0000000`，32MB），1024x600x2 = 1170KB |
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
  lv_port_indev.c/.h       指针型输入设备（触摸驱动后续接入）
App/
  app_config.h             任务优先级/栈/周期集中配置
  app_tasks.c/.h           统一任务创建入口 AppTasks_Init()
  led/app_led.c/.h         LED0（PB1）500ms 翻转任务
  uart/app_uart.c/.h       USART1 调试打印任务 AppUart_Printf()
  lvgl/app_lvgl.c/.h       LVGL 刷新任务与基础测试界面
Docs/                      移植方案与硬件参考资料
```

## 4. 编译与运行

1. 用 Keil 打开 `MDK-ARM/F767IGT6-RTOS-LVGL-LCD-DCMI.uvprojx`，确认 Options for Target → Target → ARM Compiler 为 **Arm Compiler 6**。
2. Rebuild（首次需编译 FreeRTOS + 全部 LVGL 源码，耗时较长）。
3. 下载并复位。

运行现象：

- LCD 背光点亮（PB5 高电平），显示 LVGL 演示界面：
  - 顶部：标题与运行时间
  - 左侧（板载控制）：LED1 开关（PB0）、LED0 实际电平指示灯、滑块联动进度条、按钮点击计数
  - 右侧：200ms 刷新一次的实时曲线（40 点）与圆弧指示器
  - 底部：FreeRTOS 剩余堆与 LVGL 内存池状态
- LED0（DS0 / PB1）以 500ms 周期闪烁（LED 低电平点亮）。
- USART1（PA9/PA10，115200 8N1）输出 `[LVGL] init done, free heap = ...`，并可用于各模块调试打印。

## 5. 关键配置

- **MPU**：FMC 数据区 `0x60000000`（64MB）全访问；SDRAM `0xC0000000`（32MB）全访问且**非缓存、可共享**，保证 CPU 与 LTDC/DMA2D 数据一致。FMC/QSPI 寄存器区（`0xA0000000`）走默认 Device 映射，切勿配置为无访问权限，否则 `MX_FMC_Init()` 会触发 MemManage 异常。
- **SDRAM 上电序列**：`HAL_SDRAM_Init()` 只配置控制器，`Core/Src/fmc.c` 的 `USER CODE 2` 区补齐 CLK_ENABLE → PALL → 8 次 AUTOREFRESH → LOAD_MODE(`0x230`) → 刷新计数 824（SDCLK 108MHz）。该序列必须先于 `MX_LTDC_Init()`。
- **LTDC**：时序 1024x600（HSW=20/HBP=140/HFP=160，VSW=3/VBP=20/VFP=12），图层 0 为 RGB565、帧缓冲 `0xC0000000`。
- **FreeRTOS 中断桥接**：`Core/Src/stm32f7xx_it.c` 中 SVC→`vPortSVCHandler`、PendSV→`xPortPendSVHandler`、SysTick→`xPortSysTickHandler`，三者缺一任务即无法切换。
- **DMA2D**：`MX_DMA2D_Init()` 已按 RGB565 配置；LVGL 侧 `LV_USE_DRAW_DMA2D` 当前为 0，确认显示链路正常后可置 1 并重新编译。

## 6. 后续阶段

- 触摸驱动（GT1151，软件 I2C）接入 `LVGL/lv_port_indev.c`。
- DCMI 摄像头采集与显示（帧缓冲需与 LVGL 缓冲错开分配）。
- LVGL 界面模块化与更复杂的 UI 示例。

## 7. 参考资料

- `Docs/01.FreeRTOS移植方案.md`（FreeRTOS 移植方案）
- `Docs/02.LVGL移植方案.md`（LVGL 移植与排错）
- `Docs/03.触摸移植方案.md`（触摸，后续阶段）
- `Docs/实验46 照相机实验_引脚配置分析.md`（板级引脚说明）
- FreeRTOS Kernel V11.3.1（MIT）、LVGL v9.6.0（MIT）原始许可证见各自目录。
