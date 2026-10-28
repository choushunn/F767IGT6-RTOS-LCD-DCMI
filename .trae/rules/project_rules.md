# 项目规则（F429IGT6-RTOS-LVGL-BASE）

> 本文件由项目记忆沉淀生成，所有代码变更必须遵守。全局工程规则 + 本项目硬件/架构约束。

## 1. 工程规则（全局）

1. 所有项目必须使用 git 进行管理
2. 所有项目必须添加 README
3. git 提交使用 Conventional Commits 标准（`type(scope): description`）
4. 所有项目在实施前必须确定使用哪些 skill
5. **编译验证**：不要自动调用 UV4 命令行编译；需要编译验证时提示用户在 Keil IDE 中手动编译（F7），由用户贴回编译结果

## 3. 文档分层约束

- `01.FreeRTOS移植方案.md` 仅依赖 00，不暴露后续文件
- `02.LVGL移植方案.md` 仅依赖 00、01，不暴露后续文件
- `03.触摸移植方案.md` 仅依赖 00、01、02，不暴露后续文件
- `04.DCMI摄像头模块方案.md` 仅依赖 00~03，不暴露后续文件
- 02~05 文档中经过验证的静态配置项可写入 .ioc；运行时动态配置、时序序列、初始化逻辑必须保留在代码中
- 项目文档更新后需将修复内容写入对应移植文档，保持文档与代码状态同步

## 4. 显示与相机架构约束

- LTDC 层寄存器为影子寄存器，单独操作层使能位（LEN）不会立即生效，必须通过 `HAL_LTDC_ConfigLayer()` 配置并触发 SRCR 重载
- 摄像头画面固定使用 LTDC layer2 硬件图层输出（窗口 320~800、480x480 RGB565），不得关闭；LVGL 不参与该区域绘制
- LTDC 顶层混合因子必须使用 PAxCA 模式并配合 Alpha0=0，`Core/Src/ltdc.c` 顶层配置与 `app_camera.c` 的 `cam_display_show()` 两处必须同步保持一致（CA 模式会导致左栏全黑）
- DCMI DMA 必须使用按行搬运的循环双缓冲模式（NDTR=240 字/行），禁止整帧长度配置（NDTR 16 位上限 65535，整帧 115200 会被静默截断）
- DCMI 帧中断必须在重开中断前先清除 FRAME 标志，否则中断无限重入占满 CPU
- DCMI DMA 必须配置为高优先级 + FIFO 半满模式，避免与 LTDC/LVGL 抢 SDRAM 总线导致 DCMI 内部溢出丢点
- 首帧写满后再挂接帧缓冲到 layer2，避免启动瞬间显示逐行填图
- 帧率统计以 pub（行 DMA 累计 480 行计数）为权威依据；DCMI 帧中断计数存在竞态漏计（约为实际 1/4）
- OV5640 PCLK 实测 84MHz；帧率公式 VTS = PCLK / (HTS × 目标帧率)，曝光上限 VTS-4

## 5. 时基与 RTOS 约束

- HAL 时基由 TIM7 提供；SysTick 只喂 FreeRTOS `xPortSysTickHandler()`，严禁再调 `HAL_IncTick`（否则 uwTick 2kHz、HAL_Delay 减半）
- LVGL tick 经 `lv_tick_set_cb` 绑定 `xTaskGetTickCount()`
- LVGL 必须配置 `LV_USE_OS = LV_OS_NONE` 单任务内联渲染；LV_OS_FREERTOS 的独立 swdraw 线程在双时基下会出现握手死锁（画面冻结）
- LVGL v9 配置中无 `LV_COLOR_DEPTH` 宏，颜色深度由 `LV_COLOR_FORMAT_DEFAULT` 配置
- LVGL 首次渲染 HardFault 通常是资源不足：需同步增大 LVGL 内存池、FreeRTOS 堆、LVGL 任务栈


## 7. LVGL API 注意事项

- LVGL v9 中字距函数为 `lv_obj_set_style_text_letter_space`，不存在 `lv_obj_set_style_letter_space`

## 8. 日志约定

- 关键步骤必须使用串口打印日志（`AppUart_Printf`），覆盖初始化链路、时基/中断桥接、任务创建、调度器启动、LVGL 初始化/主循环、触摸探测与坐标读取等关键节点；临时调试插桩可清理，关键路径日志保留
