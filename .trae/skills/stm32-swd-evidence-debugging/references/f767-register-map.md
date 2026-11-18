# STM32F767 显示与相机寄存器速查

以下地址适用于本项目 `STM32F767IGT6`。先用当前 AXF 的符号表确认 RAM 变量地址，不沿用旧构建地址。

## SWD 连接

```powershell
pyocd list
pyocd commander -t stm32f767zi -u <UID> -f 1M -M attach `
  --elf '<repo>\MDK-ARM\F767IGT6-RTOS-LVGL-LCD-DCMI\F767IGT6-RTOS-LVGL-LCD-DCMI.axf'
```

PowerShell 下每个 `-c "read32 ADDR"` 放一条命令。

## DCMI

基址：`0x50050000`

| 偏移 | 寄存器 | 诊断用途 |
|---:|---|---|
| `0x00` | CR | CAPTURE、ENABLE、PCKPOL、HSPOL、VSPOL、EDM、BSM |
| `0x04` | SR | HSYNC/VSYNC 与 FIFO 状态 |
| `0x08` | RISR | 原始帧/行/VSYNC/OVR/ERR 标志 |
| `0x0C` | IER | 中断使能 |
| `0x28` | DR | 原始 32 位数据字；连续读取会消耗数据 |

8 位 DCMI 模式下，首个字节在 DR 的 LSB，四个 PCLK 填满一个 32 位字。正常 RGB565 的内存 word 不应长期呈现 `0xA5A5A5A5`、`0x35353535` 这类四个字节完全相同的数据。

## DMA2 Stream1

DMA2 基址：`0x40026400`；Stream1 基址：`0x40026428`

| 偏移 | 寄存器 | 诊断用途 |
|---:|---|---|
| `0x00` | CR | EN、DBM、CIRC、方向、宽度、优先级、中断使能 |
| `0x04` | NDTR | 剩余传输量 |
| `0x08` | PAR | 外设地址，应为 `0x50050028` |
| `0x0C` | M0AR | 当前内存目标，切勿误当 FCR |
| `0x10` | M1AR | 双缓冲第二地址 |
| `0x14` | FCR | FIFO 模式、阈值、状态 |

DMA2 中断状态：

- `0x40026400` LISR
- `0x40026408` LIFCR
- Stream1 的 FEIF/TCIF 等位需按 F7 映射解码。

NVIC：

- DMA2 Stream1：IRQ 57，`NVIC_ISER1` 地址 `0xE000E104`，位 25
- DCMI：IRQ 78，`NVIC_ISER2` 地址 `0xE000E108`，位 14
- 关闭中断使用 `ICER1=0xE000E184`、`ICER2=0xE000E188`

读取 `NVIC_ISPR1/ISPR2` 区分“外设未置标志”和“中断 pending 但未进入”。

## FMC SDRAM

基址：`0xA0000000`

| 偏移 | 寄存器 |
|---:|---|
| `0x144` | SDCR1 |
| `0x148` | SDTR1 |
| `0x150` | SDSR |

SDTR1 字段：

- `TMRD[3:0]`
- `TXSR[7:4]`
- `TRAS[11:8]`
- `TRC[15:12]`
- `TWR[19:16]`
- `TRP[23:20]`
- `TRCD[27:24]`

本项目曾读取到 `0x011F5571`，说明 `TWR` 字段为 15（配置值 16），与同板参考值不一致。

## LTDC

基址：`0x40016800`

全局寄存器：

| 偏移 | 寄存器 |
|---:|---|
| `0x08` | SSCR |
| `0x0C` | BPCR |
| `0x10` | AWCR |
| `0x14` | TWCR |
| `0x18` | GCR |
| `0x34` | ISR |
| `0x38` | ICR |

Layer1 寄存器起点：`0x40016884`

Layer2 寄存器起点：`0x40016904`

层内偏移：

| 偏移 | 寄存器 |
|---:|---|
| `0x00` | CR/LEN |
| `0x04` | WHPCR |
| `0x08` | WVPCR |
| `0x10` | PFCR |
| `0x2C` | CFBAR |
| `0x30` | CFBLR |

HAL 在写入 LTDC 窗口寄存器时会自动加入水平/垂直消隐偏移，因此寄存器值与应用层相对窗口坐标不完全相同。

DMA2D：

- 基址 `0x4002B000`
- `NLR` 字段：[29:16]=每行像素数，[15:0]=行数
- 不要把 `NLR` 写成 `height << 16 | width`

## 本项目帧缓冲

| 区域 | 地址 | 当前尺寸 |
|---|---|---|
| UI | `0xC0000000` | 424x600 RGB565 |
| Camera FB0 | `0xC0140000` | 600x600 RGB565 |
| Camera FB1 | `0xC0200000` | 600x600 RGB565 |
| Camera FB2 | `0xC02C0000` | 600x600 RGB565 |

UI 和相机的当前尺寸、窗口、pitch 以 `App/display_config.h`、`Core/Src/ltdc.c` 和 `App/camera/app_camera.c` 为准。
