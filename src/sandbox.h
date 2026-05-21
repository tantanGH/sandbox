#ifndef __H_SANDBOX__
#define __H_SANDBOX__

#include <stdint.h>

#define VERSION "0.2.0 (2026/05/22)"

#define NUM_SANDS_X (8)
#define NUM_SANDS_Y (12)

#define FIELD_SIZE_X (NUM_SANDS_X * 10)   // 80
#define FIELD_SIZE_Y (NUM_SANDS_Y * 20)   // 240

#define SP_OFS_X (16 + 24)
#define SP_OFS_Y (16)

#define MINO_INIT_POS_X (16 + 6 * NUM_SANDS_X)
#define MINO_INIT_POS_Y (8)   // 画面外から

#define MINO_NEXT_POS_X (16 + 168)
#define MINO_NEXT_POS_Y (16 + 88)

#define MINO_OVER_POS_Y (16)  // ゲームオーバーとなる基準

#define DEFAULT_HI_SCORE (76500)

// ミノパターン
static const uint16_t MINO_PATTERNS[7][4] = {
    // 0: Iミノ (4x4)
    { 0x0F00, 0x2222, 0x00F0, 0x2222 },
    // 1: Oミノ (2x2だけど4x4の枠内に配置)
    { 0x6600, 0x6600, 0x6600, 0x6600 }, 
    // 2: Tミノ (3x3)
    { 0x04E0, 0x4640, 0x0E40, 0x4C40 },
    // 3: Lミノ
    { 0x4460, 0x0E80, 0xC440, 0x02E0 },
    // 4: Jミノ
    { 0x44C0, 0x08E0, 0x6440, 0x0E20 },
    // 5: Sミノ
    { 0x06C0, 0x8C40, 0x06C0, 0x8C40 },
    // 6: Zミノ
    { 0x0C60, 0x2640, 0x0C60, 0x2640 }
};

// ミノ構造
typedef struct {
  int16_t type;             // 0:I 1:O 2:T 3:L 4:J 5:S 6:Z
  int16_t rotation;         // 0:上 1:下 2:左 3:右
  int16_t color;            // 0:青 1:赤 2:緑 3:黄
  int16_t pos_x;            // スプライト画面上の表示基準位置
  int16_t pos_y;            // スプライト画面上の表示基準位置
  int16_t block_pos_x[4];   // ミノを構成するブロックごとのX表示位置
  int16_t block_pos_y[4];   // ミノを構成するブロックごとのY表示位置
} MINO;

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