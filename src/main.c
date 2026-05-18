#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <x68k/dos.h>
#include <x68k/iocs.h>
#include "sandbox.h"

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
volatile static uint16_t* PAL_BLK1   = (uint16_t*)0xE82220;

volatile static uint16_t* SP_SCRL    = (uint16_t*)0xEB0000;
volatile static uint16_t* BG0_SCRL   = (uint16_t*)0xEB0800;
volatile static uint16_t* BG1_SCRL   = (uint16_t*)0xEB0804;
volatile static uint16_t* BG_CTRL    = (uint16_t*)0xEB0808;
volatile static uint16_t* SP_CTRL    = (uint16_t*)0xEB080A;

volatile static uint16_t* PCG        = (uint16_t*)0xEB8000;
volatile static uint16_t* PCG_64     = (uint16_t*)0xEBA000;
volatile static uint16_t* PCG_128    = (uint16_t*)0xEBC000;
volatile static uint16_t* PCG_192    = (uint16_t*)0xEBE000;
volatile static uint16_t* BG_TEXT0   = (uint16_t*)0xEBC000;
volatile static uint16_t* BG_TEXT1   = (uint16_t*)0xEBE000;

//
//  PCG pattern (block)
//
static const uint16_t sprite_patterns[] = {
// color 1
0x2222,				0x2222,
0x2222,				0x2222,
0x2222,				0x2222,
0x2222,				0x2222,
0x2222,				0x2222,
0x2222,				0x2222,
0x2222,				0x2222,
0x2222,				0x2222,
0x2222,				0x2222,
0x2222,				0x2222,
0x2222,				0x2222,
0x3333,				0x3333,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,

0x2223,				0x0000,
0x2223,				0x0000,
0x2223,				0x0000,
0x2223,				0x0000,
0x2223,				0x0000,
0x2223,				0x0000,
0x2223,				0x0000,
0x2223,				0x0000,
0x2223,				0x0000,
0x2223,				0x0000,
0x2223,				0x0000,
0x3333,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,

// color 2
0x4444,				0x4444,
0x4444,				0x4444,
0x4444,				0x4444,
0x4444,				0x4444,
0x4444,				0x4444,
0x4444,				0x4444,
0x4444,				0x4444,
0x4444,				0x4444,
0x4444,				0x4444,
0x4444,				0x4444,
0x4444,				0x4444,
0x5555,				0x5555,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
0x4445,				0x0000,
0x4445,				0x0000,
0x4445,				0x0000,
0x4445,				0x0000,
0x4445,				0x0000,
0x4445,				0x0000,
0x4445,				0x0000,
0x4445,				0x0000,
0x4445,				0x0000,
0x4445,				0x0000,
0x4445,				0x0000,
0x5555,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,

// color 3
0x6666,				0x6666,
0x6666,				0x6666,
0x6666,				0x6666,
0x6666,				0x6666,
0x6666,				0x6666,
0x6666,				0x6666,
0x6666,				0x6666,
0x6666,				0x6666,
0x6666,				0x6666,
0x6666,				0x6666,
0x6666,				0x6666,
0x7777,				0x7777,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,

0x6667,				0x0000,
0x6667,				0x0000,
0x6667,				0x0000,
0x6667,				0x0000,
0x6667,				0x0000,
0x6667,				0x0000,
0x6667,				0x0000,
0x6667,				0x0000,
0x6667,				0x0000,
0x6667,				0x0000,
0x6667,				0x0000,
0x7777,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,

// color 4
0x8888,				0x8888,
0x8888,				0x8888,
0x8888,				0x8888,
0x8888,				0x8888,
0x8888,				0x8888,
0x8888,				0x8888,
0x8888,				0x8888,
0x8888,				0x8888,
0x8888,				0x8888,
0x8888,				0x8888,
0x8888,				0x8888,
0x9999,				0x9999,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,

0x8889,				0x0000,
0x8889,				0x0000,
0x8889,				0x0000,
0x8889,				0x0000,
0x8889,				0x0000,
0x8889,				0x0000,
0x8889,				0x0000,
0x8889,				0x0000,
0x8889,				0x0000,
0x8889,				0x0000,
0x8889,				0x0000,
0x9999,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
0x0000,				0x0000,
};

//
//  palette colors
//
const uint16_t palette_colors[] = {
  0b0000000000000000,   // 0: black
  0b0000000000000000,   // 1: black
  0b1110000111111111,   // 2: blue
  0b1100000100111001,   // 3: blue (dark)
  0b1100011111110001,   // 4: red
  0b1000011100100001,   // 5: red (dark)
  0b1111111000110001,   // 6: green
  0b1110010000100001,   // 7: green (dark)
  0b1111111111100001,   // 8: yellow
  0b1110011100010001,   // 9: yellow (dark)
};

// フレームバッファ
static PARTICLE particles[FIELD_SIZE_Y][FIELD_SIZE_X] __attribute__((aligned(16)));

// ライン単位の書き換えチェック用
static uint8_t invalidates[FIELD_SIZE_Y];

//
//  Set 384x256 16c mode
//
static void set_crtmod_384x256_16() {

  // wait vblank
  WAIT_VSYNC;
  WAIT_VBLANK;

  // 512x256(384x256),31kHz,16 colors
  int current_resolution = CRTC_R20[0] & 0x013;
  if (current_resolution > 0x11) {

    //CRTC_R20[0] = 0x311;    // set first
    CRTC_R20[0] = 0x011;    // set first

    CRTC_R00[1] = 0x0006;
    CRTC_R00[2] = 0x000b;
    CRTC_R00[3] = 0x003b; 
    CRTC_R00[4] = 0x0237;
    CRTC_R00[5] = 0x0005;
    CRTC_R00[6] = 0x0028;
    CRTC_R00[7] = 0x0228; 
    CRTC_R00[8] = 0x001b;

    CRTC_R00[0] = 0x0045;   // set last

  } else {

    CRTC_R00[0] = 0x0045;   // set first

    CRTC_R00[1] = 0x0006;
    CRTC_R00[2] = 0x000b;
    CRTC_R00[3] = 0x003b; 
    CRTC_R00[4] = 0x0237;
    CRTC_R00[5] = 0x0005;
    CRTC_R00[6] = 0x0028;
    CRTC_R00[7] = 0x0228; 
    CRTC_R00[8] = 0x001b;

//    CRTC_R20[0] = 0x311;    // set last
    CRTC_R20[0] = 0x011;    // set last
  }

  SET_SYSP;                 // HRL bit on for dot clock change

//  VDC_R0[0] = 3;            // memory mode 3 (65536 colors)
  VDC_R0[0] = 0;            // memory mode 0 (16 colors)

  SP_CTRL[1] = 0x000b + 4;

  usleep(100000);

  SP_CTRL[0] = 0xff;
  SP_CTRL[2] = 0x28;
  SP_CTRL[3] = 0x11;

  VDC_R2[0] = 0x2f;         // graphic on (512x256)

  CRTC_R12[0] = 0;          // scroll position X
  CRTC_R12[1] = 0;          // scroll position Y
  CRTC_R12[2] = 0;          // scroll position X
  CRTC_R12[3] = 0;          // scroll position Y    
  CRTC_R12[4] = 0;          // scroll position X
  CRTC_R12[5] = 0;          // scroll position Y
  CRTC_R12[6] = 0;          // scroll position X
  CRTC_R12[7] = 0;          // scroll position Y

} 

static const uint16_t MINO_TABLE[7][4] = {
    // 0: Iミノ (4x4)
    { 0x0F00, 0x2222, 0x00F0, 0x4444 }, // 0000111100000000 (横一列) など
    // 1: Oミノ (2x2だけど4x4の枠内に配置)
    { 0x6600, 0x6600, 0x6600, 0x6600 }, 
    // 2: Tミノ (3x3)
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 },
    // 3: Lミノ
    { 0x4460, 0x0E80, 0xC440, 0x2E00 },
    // 4: Jミノ
    { 0x44C0, 0x8E00, 0x6440, 0x0E20 },
    // 5: Sミノ
    { 0x06C0, 0x4620, 0x0360, 0x2310 },
    // 6: Zミノ
    { 0x0C60, 0x2640, 0x0C60, 0x2640 }
};

static void locate_mino(int16_t pos_x, int16_t pos_y, int16_t pattern, int16_t rotation, int16_t sp_x[], int16_t sp_y[]) {

  uint16_t mino = MINO_TABLE[pattern][rotation];
  int16_t sp_index = 0;
    
  // 4x4の格子をスキャン
  for (int i = 0; i < 16; i++) {
    // ビットが立っているか（最上位ビットからチェック）
    if (mino & (0x8000 >> i)) {
      int block_x = i % 4;
      int block_y = i / 4;
            
      // 画面上の実際のスプライト座標
      sp_x[sp_index] = pos_x + block_x * 12;
      sp_y[sp_index] = pos_y + block_y * 12;
      sp_index++;
    }
  }
}

static void put_particles(int16_t pos_x, int16_t pos_y, int16_t color) {

  // 12x12ドットの範囲に砂を配置
  for (int16_t sy = 0; sy < NUM_SANDS; sy++) {
    for (int16_t sx = 0; sx < NUM_SANDS; sx++) {

      int16_t py = pos_y + sy;
      int16_t px = pos_x + sx;
                      
      if (px >= 0 && px < FIELD_SIZE_X && py >= 0 && py < FIELD_SIZE_Y) {
        particles[py][px].attr.color = ((sy % 12) == 11 || (sx % 12) == 11) ? color * 2 + 3 : color * 2 + 2;
        particles[py][px].attr.moment_y = 0; // 最初は静止、次のフレームから落下
        particles[py][px].attr.moment_x = 0;
        invalidates[py] = 1;
      }
    }
  }
}

//
//  main
//
int32_t main(int32_t argc, uint8_t* argv[]) {

  int32_t rc = 1;
  int32_t usp = -1;
  int32_t funckey_mode = -1;
 
  // random seed初期化
  srand(_iocs_ontime().sec);

  // ファンクションキー表示モード取得
  funckey_mode = _dos_c_fnkmod(-1);

  // スーパーバイザ移行
  usp = _iocs_b_super(0);

  // ファンクションキー表示OFF
  _dos_c_fnkmod(3);

  // カーソル表示OFF
  _dos_c_curoff();

  // いったん 512x512 16色モードで初期化
  _iocs_crtmod(4);
  _iocs_g_clr_on();

  // テキスト消去
  _dos_c_cls_al();

  // 384x256 16色 モードに切り替え
  set_crtmod_384x256_16();

  WAIT_VBLANK;

  // グラフィックパレット設定
  for (int16_t i = 0; i < 10; i++) {
    _iocs_gpalet(i, palette_colors[i]);
  }

  // SP:ON TX:ON GR0:ON
  *VDC_R2 |= 0x61;                        

  // Priority TX > SP > GR
  *VDC_R1 = (*VDC_R1 & 0xff) | 0x1200;    
  
  // BG0スクロール位置初期化
  BG0_SCRL[0] = 0;
  BG0_SCRL[1] = 0;
  BG1_SCRL[0] = 0;
  BG1_SCRL[1] = 0;

  // スプライト全消去(128枚)
  for (int16_t i = 0; i < 128; i++) {
    SP_SCRL[ i * 4 + 3 ] = 0;               
  }

  // PCGパターン
  for (int16_t i = 0; i < 4; i++) {
    memcpy((void*)PCG + i * 128, (void*)(&sprite_patterns[i * 64]), 128);
  }

  // スプライトパレット
  for (int16_t i = 0; i < 10; i++) {
    PAL_BLK1[i] = palette_colors[i];
  }

  // SP/BG ON, BG1-BGTEXT1, BG0-BGTEXT0, BG1 OFF, BG0 OFF 
  *BG_CTRL = 0x210;   

  // フレームバッファ初期化
  memset(particles, 0, FIELD_SIZE_Y * FIELD_SIZE_X * sizeof(PARTICLE));
  memset(invalidates, 0, FIELD_SIZE_Y);

  // 画面オフセット
  int16_t grid_x = 4;
  int16_t grid_y = 0;
  uint32_t counter = 0;

  // スプライト色と位置
  int16_t sp_pattern = -1;
  int16_t sp_rotation = -1;
  int16_t sp_color = -1;
  int16_t sp_pos_x = -1;
  int16_t sp_pos_y = -1;
  int16_t sp_x[4];
  int16_t sp_y[4];

  // ボタン押しっぱなし判定用
  int16_t trigger_a = 0;
  int16_t trigger_b = 0;

  for (;;) {

    // SHIFTキーが押されたら終了
    if (_iocs_b_sftsns() & 0x01) break; 

    // ブロック操作
    if (sp_color >= 0) {

      uint32_t j = _iocs_joyget(0);
      if ((j & 4) == 0 && sp_x[0] > 16 && sp_x[1] > 16 && sp_x[2] > 16 && sp_x[3] > 16) {
        sp_pos_x -= 2;
        sp_x[0] -= 2;
        sp_x[1] -= 2;
        sp_x[2] -= 2; 
        sp_x[3] -= 2;               
      }
      if ((j & 8) == 0 && sp_x[0] < 16+120-12 && sp_x[1] < 16+120-12 && sp_x[2] < 16+120-12 && sp_x[3] < 16+120-12) {
        sp_pos_x += 2;
        sp_x[0] += 2;
        sp_x[1] += 2;
        sp_x[2] += 2;
        sp_x[3] += 2;
      }
      if ((j & 2) == 0 && sp_y[0] < 16+240-12 && sp_y[1] < 16+240-12 && sp_y[2] < 16+240-12 && sp_y[3] < 16+240-12) {
        sp_pos_y += 3;
        sp_y[0] += 3;
        sp_y[1] += 3;
        sp_y[2] += 3;
        sp_y[3] += 3;
      }
      if ((j & 0x40) == 0 && trigger_b == 0) {
        sp_rotation = (sp_rotation + 1) % 4;
        locate_mino(sp_pos_x, sp_pos_y, sp_pattern, sp_rotation, sp_x, sp_y);
        trigger_b = 1;
      }
      if ((j & 0x40)) {
        trigger_b = 0;
      }
      if ((j & 0x20) == 0 && trigger_a == 0) {
        sp_rotation = (sp_rotation + 3) % 4;
        locate_mino(sp_pos_x, sp_pos_y, sp_pattern, sp_rotation, sp_x, sp_y);
        trigger_a = 1;    
      }
      if ((j & 0x20)) {
        trigger_a = 0;
      }

      if ((counter % 2) == 0) {
        sp_pos_y++;
        sp_y[0]++;
        sp_y[1]++;
        sp_y[2]++;
        sp_y[3]++;
      }

    }

    // 新規ブロック
    int16_t block_new = 0;
    if ((counter % 200) == 0 && sp_pattern < 0) {

      sp_pattern = rand() % 7;
      sp_rotation = rand() % 4;
      sp_color = rand() % 4;
      sp_pos_x = grid_x * 12 + 16;
      sp_pos_y = grid_y * 12 + 16;

      uint16_t pattern = MINO_TABLE[sp_pattern][sp_rotation];
      int16_t sp_index = 0;
    
      // 4x4の格子をスキャン
      for (int16_t i = 0; i < 16; i++) {
        // ビットが立っているか（最上位ビットからチェック）
        if (pattern & (0x8000 >> i)) {
          int16_t block_x = i % 4;
          int16_t block_y = i / 4;                
          // 画面上の実際のスプライト座標
          sp_x[sp_index] = sp_pos_x + block_x * 12;
          sp_y[sp_index] = sp_pos_y + block_y * 12;
          sp_index++;
        }
      }

      block_new = 1;
    }      

    // ブロックの砂化
    int16_t block_delete = 0;
    if (sp_pattern >= 0 && sp_y[0] > 120 && sp_y[1] > 120 && sp_y[2] > 120 && sp_y[3] > 120) {

      uint16_t pattern = MINO_TABLE[sp_pattern][sp_rotation];

      // 4x4の格子をスキャン
      for (int16_t i = 0; i < 16; i++) {
        // ビットが立っているか（最上位ビットからチェック）
        if (pattern & (0x8000 >> i)) {
          int16_t block_x = i % 4;
          int16_t block_y = i / 4;                
          put_particles(sp_pos_x - 16 + block_x * 12, sp_pos_y - 16 + block_y * 12, sp_color);
        }
      }

      sp_pattern = -1;
      sp_rotation = -1;
      sp_color = -1;

      counter = 150;

      block_delete = 1;
    }

    counter++;

    // 砂の動き(下から)
    for (int16_t y = 238; y >= 0; y--) {
      for (int16_t x = 0; x < 120; x++) {
        
        // この位置はボイド
        if (particles[y][x].attr.color == 0) continue;

        // --- 物理パラメータの更新 ---
        // 直下が空いているか、またはすでに落下モーメントがある場合
        if (y < 239 && particles[y+1][x].attr.color == 0) {
          if (particles[y][x].attr.moment_y == 0) {
            // 動き出しのランダムXモーメント
            int16_t r = rand() % 100;
            particles[y][x].attr.moment_x = r <  5 ? -3 : 
                                            r < 15 ? -2 :
                                            r < 30 ? -1 :
                                            r < 70 ?  0 :
                                            r < 85 ?  1 :
                                            r < 95 ?  2 : 3;
          }
          // Y方向加速 (最大速度を7に制限してループさせない)
          if (particles[y][x].attr.moment_y < 7) {
            particles[y][x].attr.moment_y++;
          }
        } else {

          // 下が詰まっていて、かつY速度が0なら、山を崩すための横滑り判定
          if (particles[y][x].attr.moment_y == 0 && particles[y][x].attr.moment_x == 0) {
            
            // 最下段（y == 239）にいる場合は、これ以上下に滑れないので何もしない
            if (y < 239) {
              // 「斜め下」のグリッドの空き状況をチェック
              int16_t left_down_empty  = (x > 0   && particles[y+1][x-1].attr.color == 0);
              int16_t right_down_empty = (x < 119 && particles[y+1][x+1].attr.color == 0);

              if (left_down_empty && right_down_empty) {

                if (rand() % 100 < 30) { 
                    // 30%の確率で「引っかかって止まる」
                    particles[y][x].attr.moment_x = 0; 
                } else {
                    // 両方空いているなら、ランダムでどちらかに滑り出す
                    particles[y][x].attr.moment_x = (rand() % 2 == 0) ? -1 : 1;
                }

                // 【重要】滑り出す瞬間、サブピクセル位置を中央(.0)にリセットすると
                // 挙動がガタつかず滑らかになります
                particles[y][x].attr.sub_x = 0;
                particles[y][x].attr.sub_y = 0;
                
              } else if (left_down_empty) {
                particles[y][x].attr.moment_x = -1; // 左斜め下へ
                particles[y][x].attr.sub_x = 0;
                particles[y][x].attr.sub_y = 0;
                
              } else if (right_down_empty) {
                particles[y][x].attr.moment_x = 1;  // 右斜め下へ
                particles[y][x].attr.sub_x = 0;
                particles[y][x].attr.sub_y = 0;
              }
              // 両方埋まっているなら、moment_x=0 のまま完全に静止（山が確定）
            }
          }

        }

        // --- 次の予測座標の計算 (ビット演算で高速化) ---
        int16_t current_py = (y << 3) | particles[y][x].attr.sub_y;
        int16_t current_px = (x << 3) | particles[y][x].attr.sub_x;

        int16_t next_py = current_py + particles[y][x].attr.moment_y;
        int16_t next_px = current_px + particles[y][x].attr.moment_x;

        // 整数座標（グリッド位置）を算出 ( /8 は >>3、 %8 は &7 に置換)
        int16_t next_y_hi = next_py >> 3;
        int16_t next_x_hi = next_px >> 3;

        // 画面外チェック
        if (next_y_hi > 239) { next_y_hi = 239; particles[y][x].attr.moment_y = 0; }
        if (next_x_hi < 0)   { next_x_hi = 0;   particles[y][x].attr.moment_x = 0; }
        if (next_x_hi > 119) { next_x_hi = 119; particles[y][x].attr.moment_x = 0; }

        // --- 運命の衝突判定 ---
        // 移動先が「自分自身と同じ場所」か、あるいは「移動先が空っぽ」なら移動成功！
        if ((next_y_hi == y && next_x_hi == x) || particles[next_y_hi][next_x_hi].attr.color == 0) {
          
          // 一旦、現在のデータを退避
          PARTICLE tmp = particles[y][x];
          tmp.attr.sub_y = next_py & 7;
          tmp.attr.sub_x = next_px & 7;

          // 元の場所を消して、新しい場所に書き込む
          particles[y][x].raw = 0;
          particles[next_y_hi][next_x_hi].raw = tmp.raw;

          invalidates[y] = 1;
          invalidates[next_y_hi] = 1;

        } else {
          // 衝突した（直下が埋まっていた）
          particles[y][x].attr.moment_y = 0; // 縦の勢いは止まる

          // 左右の空き状況を見て、斜面に沿って転がす
          int16_t left_empty  = (x > 0   && particles[y+1][x-1].attr.color == 0);
          int16_t right_empty = (x < 119 && particles[y+1][x+1].attr.color == 0);

          if (left_empty && right_empty) {
            // 両方空いていたらランダムでどちらかに滑る
            particles[y][x].attr.moment_x = (rand() % 2 == 0) ? -1 : 1;
          } else if (left_empty) {
            particles[y][x].attr.moment_x = -1; // 左に滑る
          } else if (right_empty) {
            particles[y][x].attr.moment_x = 1;  // 右に滑る
          } else {
            particles[y][x].attr.moment_x = 0;  // どこにも行けないので完全に静止
          }

        }
      }
    }

    // グラフィック画面にコピー
    WAIT_VBLANK;
    uint64_t* gp = (uint64_t*)0xC00000;
    uint64_t* fp = (uint64_t*)particles;
    for (uint16_t y = 0; y < 240; y++) {
      if (invalidates[y]) {
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;

        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;

        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;
        *gp++ = *fp++;

        gp += 512/4 - 120/4;

        invalidates[y] = 0;

      } else {

        gp += 512/4;
        fp += 120/4;

      }
    }

    if (block_new) {

      SP_SCRL[ 0 ] = sp_x[0];
      SP_SCRL[ 1 ] = sp_y[0];
      SP_SCRL[ 2 ] = 0x100 + sp_color;
      SP_SCRL[ 3 ] = 3;

      SP_SCRL[ 4 ] = sp_x[1];
      SP_SCRL[ 5 ] = sp_y[1];
      SP_SCRL[ 6 ] = 0x100 + sp_color;
      SP_SCRL[ 7 ] = 3;

      SP_SCRL[ 8 ] = sp_x[2];
      SP_SCRL[ 9 ] = sp_y[2];
      SP_SCRL[ 10 ] = 0x100 + sp_color;
      SP_SCRL[ 11 ] = 3;

      SP_SCRL[ 12 ] = sp_x[3];
      SP_SCRL[ 13 ] = sp_y[3];
      SP_SCRL[ 14 ] = 0x100 + sp_color;
      SP_SCRL[ 15 ] = 3;

    } else if (block_delete) {

      SP_SCRL[3] = 0;
      SP_SCRL[7] = 0;
      SP_SCRL[11] = 0;
      SP_SCRL[15] = 0;

    } else {

      SP_SCRL[0] = sp_x[0];
      SP_SCRL[1] = sp_y[0];
      SP_SCRL[4] = sp_x[1];
      SP_SCRL[5] = sp_y[1];
      SP_SCRL[8] = sp_x[2];
      SP_SCRL[9] = sp_y[2];
      SP_SCRL[12] = sp_x[3];
      SP_SCRL[13] = sp_y[3];

    }
  }

  rc = 0;

exit:

  if (usp > 0) {
    _iocs_b_super(usp);
  }

  if (funckey_mode >= 0) {
    _dos_c_fnkmod(funckey_mode);
  }

  _dos_c_curon();

  return rc;
}
