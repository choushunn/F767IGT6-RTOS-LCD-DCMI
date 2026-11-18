# 已验证故障签名

本文件记录本项目实际出现过的现象、证据和验证过的修复方向。它是排错起点，不是替代现场证据的结论。

## 1. 相机帧缓冲每个 16 位像素高低字节相同

现象：

- 帧缓冲大量为 `0x35353535`、`0xB8B8B8B8`、`0x14141414`。
- `low==high` 接近 100%。
- 图像呈块状或横条，没有正常空间结构。

现场检查：

- 连续读 `DCMI_DR` 是否仍然四个字节相同。
- 比较 DCMI HSYNC 极性和 OV2640 输出模式。
- 验证 `0xDA=0x09`、`0xC2`、`0xD3`、`COM10` 等寄存器，但每次只改一个变量。

本项目验证：

- HSYNC 高有效时 DR 常为 `BBBBBBBB`。
- HSYNC 低有效时 DR 出现 `AD36AD36` 这类两个独立 16 位像素，缓冲 `low==high` 降到约 3%。
- 因此当前固件使用 `DCMI_HSPOLARITY_LOW`。

## 2. DMA 已到完成边界但吞吐只有 2–6fps

现象：

- `pub` 增长很慢。
- `fe` 持续增长，`eovr` 也逐渐增加。
- 摄像头有画面，但明显卡顿。

现场检查：

- 读 `DMA2_Stream1->CR/M0AR/NDTR/FCR`。
- 两秒内读取 `s_tcTotal` 或 NDTR，计算块吞吐。
- 确认正式固件的 DMA FIFO/突发配置，不要只按注释判断。

本项目验证：

- Direct mode + WORD 传输时只有 2–6fps，FE 持续增加。
- FIFO Full + `DMA_MBURST_INC4` 两秒完成 696 个 24 行块，约 14.5fps。
- 最终采用 `FIFOMode=ENABLE`、`FIFOThreshold=FULL`、`MemBurst=INC4`、`PeriphBurst=SINGLE`。

## 3. DMA 已到 TC，但 ISR 未执行

现象：

- NDTR 变为 0，CR 的 EN 可能仍置位。
- LISR 保留 HT/TC 标志，但 `s_tcTotal` 不变。

排查：

- 先读 LISR 区分中断源。
- 再读 NVIC ISER/ISPR。
- 检查调试脚本关闭 NVIC 后是否用错了 ICER 地址。
- 用当前 AXF 的符号地址设置硬件断点，不能使用旧构建地址。

本项目曾因把 NVIC 启用写入 ICER 而导致 DMA/DCMI 中断一直关闭；恢复 ISER 后正常。

## 4. SDRAM Write Recovery 明显偏大

现象：

- DCMI/DMA 写 SDRAM 慢，误差和溢出偏高。

现场检查：

- 读 `FMC_SDTR1` 并解析 `TWR[19:16]`。
- 与同板参考驱动比较。

本项目验证：

- 旧值 `WriteRecoveryTime=16` 对应寄存器字段 15。
- 同板参考值为 2，对应字段 1。
- 修正后 DMA 写 SDRAM 的吞吐明显改善。

## 5. UI 局部块错位、残影或撕裂

现场检查：

- 优先抓 UI 帧缓冲，确认内容是否在显存层已经错误。
- 检查 DMA2D `NLR`：高 16 位是每行像素数，低 16 位是行数。
- 检查 `OOR`、目标地址、UI pitch 与 PARTIAL flush 区域。
- UI 正确而相机错误时，不要继续修改 LVGL。

本项目验证：

- 原代码将 `NLR` 写成 `(height << 16) | width`，与硬件定义相反。
- 改为 `(width << DMA2D_NLR_PL_Pos) | (height << DMA2D_NLR_NL_Pos)` 后 UI 显存与实际截图正常。

## 6. 采集信号正确但行宽/帧边界不对应

检查：

- `g_wordsPerLine` 是否为 `图像宽度 × 2 / 4`。
- 每个块的字节数是否是 4 的整数倍并满足 DMA 对齐。
- 每帧块数是否为 `图像高度 / 每块行数`。

当前项目 600x600 RGB565：

```text
每行 = 600 * 2 / 4 = 300 words
每块 = 300 * 24 = 7200 words
每帧 = 600 / 24 = 25 blocks
```

## 7. 日志结论必须与新固件绑定

任何时候都要核对：

- 源码修改时间
- AXF/HEX 生成时间
- 用户下载时间
- 日志开始时间

如果 AXF 早于源码，旧日志不能证明新修复是否有效。
