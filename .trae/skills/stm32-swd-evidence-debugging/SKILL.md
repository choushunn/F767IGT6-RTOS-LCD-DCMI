---
name: stm32-swd-evidence-debugging
description: Diagnose STM32 display, camera, DMA, and LTDC failures from serial logs, live pyOCD/SWD registers, and framebuffer snapshots. Use when visible symptoms need hardware evidence; do not use for basic environment setup, ordinary building, or flashing.
---

# STM32 SWD 证据驱动诊断

用于 STM32F767 等 MCU 的显示、摄像头、DMA、LTDC 和缓存一致性问题。核心原则：**先取得现场证据，再提出修复；不得根据代码外观或历史经验直接下结论。**

编译、烧录、RDP、串口工具安装等问题使用 `stm32-embedded-dev` 技能，不在这里重复。

## 必须遵守

- 先执行 `git status --short --branch`，保留用户未提交的改动。
- 对照源码、AXF/HEX 和日志时间，确认测试固件包含待验证修改。
- 读取最新非空日志；不要拿旧日志证明新固件。
- 需要具体寄存器值时通过 SWD 实读，不把注释、文档或先前推断当成硬件事实。
- 一次只改一个可能影响时序的变量；记录修改前、修改后和恢复值。
- 对运行中的设备做临时寄存器写入前确认可逆；结束后恢复 NVIC、DCMI、DMA 和显示层状态。
- 不以“关闭功能、降低分辨率、降低帧率”冒充根因修复；确需权衡时向用户说明证据和代价。
- 没有实机验证就明确写“未验证”，不要使用“已修复”。

## 工作流

### 1. 建立测试基线

```powershell
git status --short --branch
Get-ChildItem logs -Filter 'serial_*.log' | Sort-Object LastWriteTime | Select-Object -Last 5
Get-Item App\camera\app_camera.c,Core\Src\ltdc.c,LVGL\lv_port_disp.c,MDK-ARM\F767IGT6-RTOS-LVGL-LCD-DCMI\F767IGT6-RTOS-LVGL-LCD-DCMI.axf
```

先回答：

- 源码修改时间是否早于最新 AXF？
- 用户是否已下载这次 AXF？
- 最新日志是否晚于下载时间？
- 故障发生在启动、进入双图层后、运行一段时间后，还是特定操作后？

### 2. 先读串口边界数据

优先使用项目现有串口日志。需要直接采集时：

```powershell
python .trae\skills\stm32-embedded-dev\scripts\serial_monitor.py --list
python .trae\skills\stm32-embedded-dev\scripts\serial_monitor.py --port COMx --duration 10
```

对相机/显示问题至少比较两个统计窗口：

- `pub`：完整发布帧数
- `vsync`：DCMI 帧中断计数
- `eovr/esync`：DCMI 溢出/同步错误
- `dmaerr/desync/fe`：DMA 总线错误、错位重排、FIFO 错误
- `fu/terr`：LTDC FIFO 下溢和取数错误

若 COM 口被占用，不要擅自结束未知进程；优先读取现有日志，或请用户关闭占用程序。

### 3. 连接 SWD

```powershell
pyocd list
pyocd list --targets | Select-String stm32f767
```

Codex 沙箱可能看不到 USB 探针。若 `pyocd list` 报无探针而用户确认已连接，使用提权/沙箱外只读方式重试。枚举成功后再附着，不要得出“硬件未连接”的结论。

只读附着示例：

```powershell
pyocd commander -t stm32f767zi -u <UID> -f 1M -M attach `
  --elf '<repo>\MDK-ARM\F767IGT6-RTOS-LVGL-LCD-DCMI\F767IGT6-RTOS-LVGL-LCD-DCMI.axf' `
  -c "read32 0x50050000" `
  -c "read32 0x40026428" `
  -c "read32 0x40016904"
```

PowerShell 下每个 `-c` 放一条命令。读取多点数据时可重复采样：

```text
read32 ADDR
sleep 500
read32 ADDR
```

需要 PC 或核心寄存器时，`attach` 不会自动停止 CPU；先显式 `halt`/交互式暂停，并在读取后继续运行。

### 4. 抓取真实显存

```powershell
python .trae\skills\stm32-swd-evidence-debugging\scripts\swd_capture.py `
  --uid <UID> `
  --out .tmp_diag\swd-capture

python .trae\skills\stm32-swd-evidence-debugging\scripts\render_rgb565.py `
  .tmp_diag\swd-capture\ui.bin 424 600 `
  --output .tmp_diag\swd-capture\ui.png
```

必须同时比较：

- UI 显存是否在预期地址、尺寸和 pitch 下呈现可读界面。
- 相机三块缓冲是否都有图像，还是 LTDC 扫描了错误地址。
- 帧缓冲是否仍出现每个 16 位像素高低字节相同、整帧只有少量值、块重复或条带。
- 换帧期间连续抓取是否能观察到地址或内容不一致。

### 5. 按数据通路定位边界

顺序固定为：

```text
传感器寄存器/时序
  -> DCMI CR/SR/RISR/DR
  -> DMA2 流 CR/NDTR/M0AR/LISR/FCR
  -> FMC/SDRAM 时序
  -> 帧缓冲内容
  -> LTDC 层窗口/CFBAR/CFBLR/ISR
  -> LVGL 局部缓冲与 DMA2D 搬运
```

只有当上游边界已有证据时才进入下游。例如：

- `DCMI_DR` 四个字节完全相同：先查输出格式、同步极性、采样沿和字节顺序。
- DMA 已完成但 TC ISR 未进入：读 `LISR`、NVIC `ISER/ISPR` 和当前 AXF 的中断入口地址。
- `pub` 慢但 `vsync/eovr` 增长：说明采集吞吐或 DMA 服务不足，不要先改 LVGL。
- UI 正确而相机错误：问题不在 LTDC 全局时序；继续隔离第二图层、DCMI、DMA 和相机内存。
- UI 局部错位：检查 DMA2D `NLR`、`OOR`、目标 pitch 和区域坐标。

### 6. 需要时检查 OV2640 寄存器

通过现有 SCCB 引脚做临时读取时，先确认 GPIO 模式未被其他功能占用。读取后再读同一寄存器验证写入生效。`OV2640_ImageWin_Set`、`OV2640_OutSize_Set` 和实际 PCLK/HREF/VSYNC 应一起核对。

实验完成后恢复所有改动过的传感器寄存器。

### 7. 最小验证与回归

一次实验只改变一个变量：

1. 记录修复前计数和快照。
2. 应用一个改动。
3. 立即读取相同寄存器/显存。
4. 恢复临时状态或保留已确认修复。
5. 再跑至少 10 秒串口和一次新显存抓取。

固件改动完成后先做静态语法检查；Keil 编译由用户按 F7 执行。未经用户新固件实测，不得宣称问题解决。

## 已知失败签名和本项目修正

详细寄存器地址、字段和已验证结论见：

- [references/f767-register-map.md](references/f767-register-map.md)
- [references/failure-signatures.md](references/failure-signatures.md)

这些资料中的数值是调试起点，不是永久硬件契约。每次仍以当前源码、AXF、日志和实时寄存器为准。

## 工具

- `scripts/swd_capture.py`：附着目标，读取关键寄存器，导出 UI/相机帧缓冲。
- `scripts/render_rgb565.py`：把 RGB565 帧缓冲转 PNG，并统计重复字节、颜色数和行结构。
- `scripts/` 中的读写仅用于诊断；修改 live 寄存器前先确认恢复路径。
