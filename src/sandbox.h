#ifndef __H_SANDBOX__
#define __H_SANDBOX__

#include <stdint.h>

#define VERSION "0.1.7 (2026/05/21)"

#define NUM_SANDS_X (8)
#define NUM_SANDS_Y (12)

#define FIELD_SIZE_X (NUM_SANDS_X * 10)   // 80
#define FIELD_SIZE_Y (NUM_SANDS_Y * 20)   // 240

#define SP_INIT_POS_X (16 + 6 * NUM_SANDS_X)
#define SP_INIT_POS_Y (8)   // 画面外から

#define SP_OFS_X (16 + 24)
#define SP_OFS_Y (16)

#define SP_OFS_X_NEXT (16 + 168)
#define SP_OFS_Y_NEXT (16 + 88)

#define DEFAULT_HI_SCORE (76500)

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

#endif