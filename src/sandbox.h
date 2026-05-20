#ifndef __H_SANDBOX__
#define __H_SANDBOX__

#include <stdint.h>

#define VERSION "0.1.6 (2026/05/20)"

#define NUM_SANDS_X (8)
#define NUM_SANDS_Y (12)

#define FIELD_SIZE_X (NUM_SANDS_X * 10)   // 80
#define FIELD_SIZE_Y (NUM_SANDS_Y * 20)   // 240

// 1粒の砂のデータ構造（16ビット）
typedef union {
  
  uint16_t raw; // バルク転送やクリアに使う16bit生データ
    
  struct {
    uint16_t sub_x     : 3; // bit 15-13: 小数点以下x座標 (.0 - .7)
    uint16_t sub_y     : 3; // bit 12-10: 小数点以下y座標 (.0 - .7)
    int16_t  moment_x  : 3; // bit 9-7:   x方向モーメント (-3 - 3) ※符号付き -4はスリープ状態
    uint16_t moment_y  : 3; // bit 6-4:   y方向モーメント (0 - 7)
    uint16_t color     : 4; // bit 3-0:   カラーコード (0 - 15)
  } attr;

} PARTICLE;

// 連結チェック用80(96)ビット構造
typedef struct {
  uint32_t lo;
  uint32_t mi;
  uint32_t hi;
} BITLINE80;

//
//  CRTC / VDC IO Addresses
//
volatile static uint16_t* CRTC_R00 = (uint16_t*)0xE80000;
volatile static uint16_t* CRTC_R12 = (uint16_t*)0xE80018;
volatile static uint16_t* CRTC_R20 = (uint16_t*)0xE80028;
volatile static uint16_t* VDC_R0   = (uint16_t*)0xE82400;
volatile static uint16_t* VDC_R1   = (uint16_t*)0xE82500;
volatile static uint16_t* VDC_R2   = (uint16_t*)0xE82600;

//
//  GPIP / SYSP / SCON IO Addresses
//
volatile static uint8_t* REG_GPIP = (uint8_t*)0xE88001;
volatile static uint8_t* REG_SYSP = (uint8_t*)0xE8E007;

#define WAIT_VSYNC   while(!(REG_GPIP[0] & 0x10))
#define WAIT_VBLANK    while(REG_GPIP[0] & 0x10)

#define SET_SYSP     (REG_SYSP[0] |=  0x02)
#define RESET_SYSP   (REG_SYSP[0] &= ~0x02)

//
//  GVRAM / BG / SP IO Addresses
//
volatile static uint16_t* GVRAM      = (uint16_t*)0xC00000;
volatile static uint16_t* GR0_SCRL   = (uint16_t*)0XE80018;

volatile static uint16_t* PAL_BLK1   = (uint16_t*)0xE82220;
volatile static uint16_t* PAL_BLK2   = (uint16_t*)0xE82240;

volatile static uint16_t* SP_SCRL    = (uint16_t*)0xEB0000;
volatile static uint16_t* BG0_SCRL   = (uint16_t*)0xEB0800;
volatile static uint16_t* BG1_SCRL   = (uint16_t*)0xEB0804;
volatile static uint16_t* BG_CTRL    = (uint16_t*)0xEB0808;
volatile static uint16_t* SP_CTRL    = (uint16_t*)0xEB080A;

volatile static uint16_t* PCG        = (uint16_t*)0xEB8000;
volatile static uint16_t* BG_TEXT0   = (uint16_t*)0xEBC000;
volatile static uint16_t* BG_TEXT1   = (uint16_t*)0xEBE000;

//
//  Font Addresses
//
static const uint8_t* FONT_ADDR_8x8 = ((uint8_t*)0xF3A000);
#define FONT_BYTES_8x8    (8)
#define FONT_REGULAR      (0)
#define FONT_BOLD         (1)

#endif