#ifndef SCCB_H
#define SCCB_H

#include <stdint.h>

void SCCB_Init(void);
void SCCB_Start(void);
void SCCB_Stop(void);
void SCCB_No_Ack(void);

/* 返回值：0 表示收到 ACK，1 表示未收到 ACK */
uint8_t SCCB_WR_Byte(uint8_t dat);

/* 读一个字节（内部完成 SDA 输入/输出切换） */
uint8_t SCCB_RD_Byte(void);

#endif /* SCCB_H */
