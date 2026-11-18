#ifndef OV2640_H
#define OV2640_H

#include <stdint.h>

/* OV2640 SCCB 8-bit write address: 7-bit address 0x30. */
#define OV2640_ADDR             0x60U
#define OV2640_MID              0x7FA2U
#define OV2640_PID              0x2642U

#define OV2640_SENSOR_MIDH      0x1CU
#define OV2640_SENSOR_MIDL      0x1DU
#define OV2640_SENSOR_PIDH      0x0AU
#define OV2640_SENSOR_PIDL      0x0BU

/* Reset pin PA15: 1 releases reset, 0 holds reset. */
void OV2640_RST_Init(void);
void OV2640_RST_Set(uint8_t sta);

/* Power-down through PCF8574 P2: 1 powers down, 0 powers up. */
void OV2640_PWDN_Set(uint8_t sta);

uint8_t OV2640_WR_Reg(uint8_t reg, uint8_t data);
uint8_t OV2640_RD_Reg(uint8_t reg);

/* Initializes UXGA mode and validates manufacturer/product IDs. */
uint8_t OV2640_Init(void);

/* Selects RGB565 output. OV2640 uses the DSP register bank. */
void OV2640_RGB565_Mode(void);

/* Selects the sensor image window used by the DSP scaler. */
uint8_t OV2640_ImageWin_Set(uint16_t offx,
                            uint16_t offy,
                            uint16_t width,
                            uint16_t height);

/* Selects the final RGB565 output width and height. */
uint8_t OV2640_OutSize_Set(uint16_t width, uint16_t height);

/* 0x04 bit[7] mirrors horizontally, bit[6] flips vertically. */
void OV2640_SetMirrorFlip(uint8_t mirror, uint8_t flip);

#endif /* OV2640_H */
