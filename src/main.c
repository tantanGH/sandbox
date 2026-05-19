#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <x68k/dos.h>
#include <x68k/iocs.h>
#include "sandbox.h"
#include "pattern.h"

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

// 8x8 normal font data
static struct iocs_fntbuf font_data_8x8[ 256 ];

// 8x8 bold font data
static struct iocs_fntbuf font_data_8x8_bold[ 256 ];

// 物理演算用バッファ
static PARTICLE particles[FIELD_SIZE_Y][FIELD_SIZE_X]; 

// 描画用トリプルバッファ
static uint16_t screen_buffers[3][FIELD_SIZE_Y][FIELD_SIZE_X] __attribute__((aligned(16)));

// ライン単位の書き換えチェック用
static uint8_t invalidates[3][FIELD_SIZE_Y];

// VSYNC割り込みが「いま現在描画中」の面
volatile static int16_t page_render = 0;

// メインループが「いま現在書き込み中」の面
volatile static int16_t page_calc = 1;

// VSYNC割り込みが「次に描画する」の面
volatile static int16_t page_next = 2;

// ミノパターン
static const uint16_t MINO_TABLE[7][4] = {
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

// グローバル変数として乱数の状態を持つ（初期値は0以外なら何でもOK）
static uint32_t quickrand_seed = 2463534242;

// Xorshift32をインライン関数で定義
static inline uint32_t quickrand(void) {
    quickrand_seed ^= (quickrand_seed << 13);
    quickrand_seed ^= (quickrand_seed >> 17);
    quickrand_seed ^= (quickrand_seed << 5);
    return quickrand_seed;
}

static volatile int16_t block_event_new = 0;
static volatile int16_t block_event_delete = 0;
static volatile int16_t sp_color = -1;
static int16_t sp_x[4];          // 各ブロックの位置
static int16_t sp_y[4];          // 各ブロックの位置

// VSYNC割り込みハンドラ
static void __attribute__((interrupt)) refresh_screen() {

  // スプライトの処理
  if (block_event_delete) {

    // 消去
    SP_SCRL[  3 ] = 0;
    SP_SCRL[  7 ] = 0;
    SP_SCRL[ 11 ] = 0;
    SP_SCRL[ 15 ] = 0;

    block_event_delete = 0;

  } else if (block_event_new) {

    // 新規作成
    SP_SCRL[  0 ] = sp_x[0];
    SP_SCRL[  1 ] = sp_y[0];
    SP_SCRL[  2 ] = 0x100 + sp_color;
    SP_SCRL[  3 ] = 3;

    SP_SCRL[  4 ] = sp_x[1];
    SP_SCRL[  5 ] = sp_y[1];
    SP_SCRL[  6 ] = 0x100 + sp_color;
    SP_SCRL[  7 ] = 3;

    SP_SCRL[  8 ] = sp_x[2];
    SP_SCRL[  9 ] = sp_y[2];
    SP_SCRL[ 10 ] = 0x100 + sp_color;
    SP_SCRL[ 11 ] = 3;

    SP_SCRL[ 12 ] = sp_x[3];
    SP_SCRL[ 13 ] = sp_y[3];
    SP_SCRL[ 14 ] = 0x100 + sp_color;
    SP_SCRL[ 15 ] = 3;

    block_event_new = 0;

  } else if (sp_color >= 0) {

    // 通常落下
    SP_SCRL[  0 ] = sp_x[0];
    SP_SCRL[  1 ] = sp_y[0];

    SP_SCRL[  4 ] = sp_x[1];
    SP_SCRL[  5 ] = sp_y[1];

    SP_SCRL[  8 ] = sp_x[2];
    SP_SCRL[  9 ] = sp_y[2];

    SP_SCRL[ 12 ] = sp_x[3];
    SP_SCRL[ 13 ] = sp_y[3];

  }

  // グラフィック画面差分描画
  if (page_render != page_next) {
    page_render = page_next;
  }
  uint64_t* gp = (uint64_t*)0xC00030;
  uint64_t* fp = (uint64_t*)&screen_buffers[page_render];
  for (uint16_t y = 0; y < FIELD_SIZE_Y; y++) {
    if (invalidates[page_render][y]) {
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

      gp += 512/4 - FIELD_SIZE_X/4;

      invalidates[page_render][y] = 0;

    } else {

      gp += 512/4;
      fp += FIELD_SIZE_X/4;

    }
  }

}

static int16_t locate_mino_check(int16_t pos_x, int16_t pos_y, int16_t mino, int16_t rotation) {

  uint16_t pattern = MINO_TABLE[mino][rotation];
  int16_t sp_index = 0;
  int16_t x[4];
  int16_t y[4];

  // 4x4の格子をスキャン
  for (int i = 0; i < 16; i++) {
    // ビットが立っているか（最上位ビットからチェック）
    if (pattern & (0x8000 >> i)) {
      int block_x = i % 4;
      int block_y = i / 4;
            
      // 画面上の実際のスプライト座標
      x[sp_index] = pos_x + block_x * NUM_SANDS_X;
      y[sp_index] = pos_y + block_y * NUM_SANDS_Y;
      sp_index++;
    }
  }

  if (x[0] < 16+24 || x[1] < 16+24 || x[2] < 16+24 || x[3] < 16+24) return 0;
  if (x[0]+8 > 16+24+80 || x[1]+8 > 16+24+80 || x[2]+8 > 16+24+80 || x[3]+8 > 16+24+80) return 0;
  if (y[0]+12 > 16+240 || y[1]+12 > 16+240 || y[2]+12 > 16+240 || y[3]+12 > 16+240) return 0;

  return 1;
}

static void locate_mino(int16_t pos_x, int16_t pos_y, int16_t mino, int16_t rotation, int16_t sp_x[], int16_t sp_y[]) {

  uint16_t pattern = MINO_TABLE[mino][rotation];
  int16_t sp_index = 0;
    
  // 4x4の格子をスキャン
  for (int i = 0; i < 16; i++) {
    // ビットが立っているか（最上位ビットからチェック）
    if (pattern & (0x8000 >> i)) {
      int block_x = i % 4;
      int block_y = i / 4;
            
      // 画面上の実際のスプライト座標
      sp_x[sp_index] = pos_x + block_x * NUM_SANDS_X;
      sp_y[sp_index] = pos_y + block_y * NUM_SANDS_Y;
      sp_index++;
    }
  }
}

static void put_particles(int16_t pos_x, int16_t pos_y, int16_t color) {

  // 8x12ドットの範囲に砂を配置
  for (int16_t sy = 0; sy < NUM_SANDS_Y; sy++) {
    for (int16_t sx = 0; sx < NUM_SANDS_X; sx++) {

      int16_t py = pos_y + sy;
      int16_t px = pos_x + sx;
                      
      if (px >= 0 && px < FIELD_SIZE_X && py >= 0 && py < FIELD_SIZE_Y) {
        int16_t c = ((sy % NUM_SANDS_Y) == (NUM_SANDS_Y - 1) || (sx % NUM_SANDS_X) == (NUM_SANDS_X - 1)) ? color * 3 + 1 : ((quickrand() & 7) < 6) ? color * 3 + 2 : color * 3 + 3;
        particles[py][px].attr.color = c;
        particles[py][px].attr.moment_y = 0;
        particles[py][px].attr.moment_x = 0;
        screen_buffers[page_calc][py][px] = c;
        invalidates[page_calc][py] = 1;
      }
    }
  }
}

// put text in 8x8 font
void put_text(uint16_t x, uint16_t y, uint16_t color, uint16_t bold, const uint8_t* text) {

  int16_t len = strlen(text);

  for (int16_t i = 0; i < len; i++) {
    struct iocs_fntbuf* font_data = (bold == FONT_BOLD) ? 
                                    &(font_data_8x8_bold[text[i]]) : 
                                    &(font_data_8x8[text[i]]);
    if (color & 0x01) {
      _iocs_tcolor(1);
      _iocs_textput(x + i * 8, y, font_data);
    }
    if (color & 0x02) {
      _iocs_tcolor(2);
      _iocs_textput(x + i * 8, y, font_data);    
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
  quickrand_seed = rand();

  // ファンクションキー表示モード取得
  funckey_mode = _dos_c_fnkmod(-1);

  // スーパーバイザ移行
  usp = _iocs_b_super(0);

  // ファンクションキー表示OFF
  _dos_c_fnkmod(3);

  // カーソル表示OFF
  _dos_c_curoff();

  // 256x256 16色モード (グラフィック横512)
  _iocs_crtmod(6);
  _iocs_g_clr_on();

  // テキスト消去
  _dos_c_cls_al();

  // 8x8 フォントの初期化
  for (int16_t i = 0; i < 256; i++) {

    // 8x8 regular font
    font_data_8x8[i].xl = 8;
    font_data_8x8[i].yl = 8;
    memcpy(font_data_8x8[i].buffer, FONT_ADDR_8x8 + FONT_BYTES_8x8 * i, FONT_BYTES_8x8);

    // 8x8 bold font
    font_data_8x8_bold[i].xl = 8;
    font_data_8x8_bold[i].yl = 8;
    memcpy(font_data_8x8_bold[i].buffer, FONT_ADDR_8x8 + FONT_BYTES_8x8 * i, FONT_BYTES_8x8);
    for (int16_t j = 0; j < FONT_BYTES_8x8; j++) {
      font_data_8x8_bold[i].buffer[j] |= ( font_data_8x8_bold[i].buffer[j] >> 1 ) & 0xff;
    }

  }
  
  WAIT_VBLANK;

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

  // PCGパターン(ブロック)
  for (int16_t i = 0; i < 4; i++) {
    memcpy((void*)PCG + i * 128, (void*)(&block_patterns[i * 64]), 128);
  }
  
  // PCGパターン(パイプ)
  for (int16_t i = 0; i < 4; i++) {
    memcpy((void*)PCG + (i+4) * 128, (void*)(&pipe_patterns[i * 64]), 128);
  }

  // スプライトパレット設定
  for (int16_t i = 0; i < 16; i++) {
    PAL_BLK1[i] = palette_colors1[i];
    PAL_BLK2[i] = palette_colors2[i];
  }

  // BG TEXT1クリア
  for (int16_t y = 0; y < 64; y++) {
    for (int16_t x = 0; x < 64; x++) {
      BG_TEXT1[ y * 64 + x ] = 0x200 + 16;
    }
  }
  for (int16_t y = 0; y < 30; y++) {
    int16_t p = (y == 0) ? 20 : (y == 29) ? 28 : 24;
    BG_TEXT1[ y * 64 + 2 ] = 0x200 + p;
    BG_TEXT1[ y * 64 + 2 + 11 ] = 0x200 + p;
  }

  //*BG_CTRL = 0x210;   // SP/BG ON, BG1-BGTEXT1, BG0-BGTEXT0, BG1 OFF, BG0 OFF 
  *BG_CTRL = 0x203;   // SP/BG ON, BG1-BGTEXT0, BG0-BGTEXT1, BG1 OFF, BG0 ON 

  put_text(144,8,2,1,"SCORE");
  put_text(144,16,3,1,"    0");

  // グラフィックパレット設定
  for (int16_t i = 0; i < 16; i++) {
    _iocs_gpalet(i, palette_colors1[i]);
  }
  
  // 物理演算バッファ初期化
  memset(particles, 0, FIELD_SIZE_Y * FIELD_SIZE_X * sizeof(PARTICLE));

  // 画面描画ダブルバッファ初期化
  memset(screen_buffers, 0, 2 * FIELD_SIZE_Y * FIELD_SIZE_X * sizeof(uint16_t));
  memset(invalidates, 0, 2 * FIELD_SIZE_Y * sizeof(uint8_t));

  // 画面オフセット
  int16_t grid_x = 6;
  int16_t grid_y = 0;
  uint32_t counter = 0;

  // スプライト色と位置
  int16_t sp_mino = -1;
  int16_t sp_rotation = -1;
  int16_t sp_pos_x = -1;    // 基準位置
  int16_t sp_pos_y = -1;    // 基準位置

  // ボタン押しっぱなし判定用
  int16_t trigger_a = 0;
  int16_t trigger_b = 0;

  // グローバル変数初期化
  page_render = 0;
  page_calc = 1;
  page_next = 0;
  block_event_new = 0;
  block_event_delete = 0;

  // VSYNC割り込み開始
  int16_t vsync = 0;
  if (_iocs_vdispst((uint8_t*)refresh_screen, 0, 1) != 0) {
    printf("VSYNC割り込みが使用中です。\n");
    goto exit;
  }
  vsync = 1;
  _iocs_b_bpoke((uint8_t*)0xe88019,0x00);  // stop Timer-A  
  _iocs_b_bpoke((uint8_t*)0xe8801f,0x01);  // set Timer-A counter        
  _iocs_b_bpoke((uint8_t*)0xe88019,0x18);  // restart Timer-A    

  for (;;) {

    // SHIFTキーが押されたら終了
    if (_iocs_b_sftsns() & 0x01) break; 

    // ブロック操作
    if (sp_mino >= 0) {

      //uint32_t j = _iocs_joyget(0);
      uint8_t j = *((volatile uint8_t*)(0x0e9a001));
      if ((j & 4) == 0 && sp_x[0] > 16+24 && sp_x[1] > 16+24 && sp_x[2] > 16+24 && sp_x[3] > 16+24) {
        sp_pos_x -= 2;
        sp_x[0] -= 2;
        sp_x[1] -= 2;
        sp_x[2] -= 2; 
        sp_x[3] -= 2;               
      }
      if ((j & 8) == 0 && sp_x[0] < 16+24+80-8 && sp_x[1] < 16+24+80-8 && sp_x[2] < 16+24+80-8 && sp_x[3] < 16+24+80-8) {
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
        if (locate_mino_check(sp_pos_x, sp_pos_y, sp_mino, (sp_rotation + 1) % 4)) {
          sp_rotation = (sp_rotation + 1) % 4;
          locate_mino(sp_pos_x, sp_pos_y, sp_mino, sp_rotation, sp_x, sp_y);
          trigger_b = 1;
        }
      }
      if ((j & 0x40)) {
        trigger_b = 0;
      }
      if ((j & 0x20) == 0 && trigger_a == 0) {
        if (locate_mino_check(sp_pos_x, sp_pos_y, sp_mino, (sp_rotation + 3) % 4)) {
          sp_rotation = (sp_rotation + 3) % 4;
          locate_mino(sp_pos_x, sp_pos_y, sp_mino, sp_rotation, sp_x, sp_y);
          trigger_a = 1;    
        }
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
    if ((counter % 200) == 0 && sp_mino < 0) {

      sp_mino = quickrand() % 7;
      sp_rotation = quickrand() & 3;
      sp_color = rand() % 4;
      sp_pos_x = grid_x * NUM_SANDS_X + 16;
      sp_pos_y = grid_y * NUM_SANDS_Y + 16;

      locate_mino(sp_pos_x, sp_pos_y, sp_mino, sp_rotation, sp_x, sp_y);

      block_event_new = 1;
    }      

    // ブロックの砂化
    int16_t block_delete = 0;
    if (sp_mino >= 0 && (
        (sp_y[0] - 16 + 12 >= 240 || sp_y[1] - 16 + 12 >= 240 || sp_y[2] - 16 + 12 >= 240 || sp_y[3] - 16 + 12 >= 240) ||

        (particles[sp_y[0] - 16 + 12][sp_x[0] - 16 - 24    ].attr.color > 0 ||
         particles[sp_y[0] - 16 + 12][sp_x[0] - 16 - 24 + 2].attr.color > 0 || 
         particles[sp_y[0] - 16 + 12][sp_x[0] - 16 - 24 + 4].attr.color > 0 || 
         particles[sp_y[0] - 16 + 12][sp_x[0] - 16 - 24 + 6].attr.color > 0) ||

        (particles[sp_y[1] - 16 + 12][sp_x[1] - 16 - 24    ].attr.color > 0 ||
         particles[sp_y[1] - 16 + 12][sp_x[1] - 16 - 24 + 2].attr.color > 0 || 
         particles[sp_y[1] - 16 + 12][sp_x[1] - 16 - 24 + 4].attr.color > 0 || 
         particles[sp_y[1] - 16 + 12][sp_x[1] - 16 - 24 + 6].attr.color > 0) ||

        (particles[sp_y[2] - 16 + 12][sp_x[2] - 16 - 24    ].attr.color > 0 ||
         particles[sp_y[2] - 16 + 12][sp_x[2] - 16 - 24 + 2].attr.color > 0 || 
         particles[sp_y[2] - 16 + 12][sp_x[2] - 16 - 24 + 4].attr.color > 0 || 
         particles[sp_y[2] - 16 + 12][sp_x[2] - 16 - 24 + 6].attr.color > 0) ||

        (particles[sp_y[3] - 16 + 12][sp_x[3] - 16 - 24    ].attr.color > 0 ||
         particles[sp_y[3] - 16 + 12][sp_x[3] - 16 - 24 + 2].attr.color > 0 || 
         particles[sp_y[3] - 16 + 12][sp_x[3] - 16 - 24 + 4].attr.color > 0 || 
         particles[sp_y[3] - 16 + 12][sp_x[3] - 16 - 24 + 6].attr.color > 0))) {

      uint16_t pattern = MINO_TABLE[sp_mino][sp_rotation];

      // 4x4の格子をスキャン
      for (int16_t i = 0; i < 16; i++) {
        // ビットが立っているか（最上位ビットからチェック）
        if (pattern & (0x8000 >> i)) {
          int16_t block_x = i % 4;
          int16_t block_y = i / 4;                
          put_particles(sp_pos_x - 16 - 24 + block_x * NUM_SANDS_X, sp_pos_y - 16 + block_y * NUM_SANDS_Y, sp_color);
        }
      }

      sp_mino = -1;
      sp_rotation = -1;
      sp_color = -1;

      counter = 150;

      block_event_delete = 1;
    }

    counter++;

    uint16_t count_void = 0;
    uint16_t count_sleep = 0;

    // 砂の動き(下から)
    for (int16_t y = FIELD_SIZE_Y-2; y >= 0; y--) {

      for (int16_t x = 0; x < FIELD_SIZE_X; x++) {
        
        // 砂がない、またはスリープ状態ならスキップ
        if (particles[y][x].attr.color == 0) {
          count_void++;
          continue;
        }
        if (particles[y][x].attr.moment_x == -4) {
          count_sleep++;
          continue;
        }

        // --- 物理パラメータの更新 ---
        // 直下が空いているか、またはすでに落下モーメントがある場合
        if (y < FIELD_SIZE_Y-1 && particles[y+1][x].attr.color == 0) {
          if (particles[y][x].attr.moment_y == 0) {
            // 動き出しのランダムXモーメント (4に設定するとスリープ状態)
            int16_t r = quickrand() & 127;
            particles[y][x].attr.moment_x = r <  2  ? -3 : 
                                            r < 15  ? -2 :
                                            r < 45  ? -1 :
                                            r < 83  ?  0 :
                                            r < 113 ?  1 :
                                            r < 126 ?  2 : 3;
          }
          // Y方向加速 (最大速度を7に制限してループさせない)
          if (particles[y][x].attr.moment_y < 7) {
            particles[y][x].attr.moment_y++;
          }

        } else {

          // 下が詰まっていて、かつY速度が0なら、山を崩すための横滑り判定
          if (particles[y][x].attr.moment_y == 0 && particles[y][x].attr.moment_x == 0) {
            
            // 最下段にいる場合は、これ以上下に滑れないので何もしない
            if (y < FIELD_SIZE_Y-1) {

              // 「斜め下」のグリッドの空き状況をチェック
              int16_t left_down_empty  = (x > 0              && particles[y+1][x-1].attr.color == 0);
              int16_t right_down_empty = (x < FIELD_SIZE_X-1 && particles[y+1][x+1].attr.color == 0);

              if (left_down_empty && right_down_empty) {

                if ((quickrand() & 127) < 35) { 
                    // 30%の確率で「引っかかって止まる」
                    particles[y][x].attr.moment_x = 0; 
                } else {
                    // 両方空いているなら、ランダムでどちらかに滑り出す
                    particles[y][x].attr.moment_x = (quickrand() & 1) ? -1 : 1;
                }

                // 【重要】滑り出す瞬間、サブピクセル位置を中央にリセットすると
                // 挙動がガタつかず滑らかになります
                particles[y][x].attr.sub_x = (quickrand() & 1) ? 3 : 4;
                particles[y][x].attr.sub_y = 0;
                
              } else if (left_down_empty) {
                particles[y][x].attr.moment_x = -1; // 左斜め下へ
                particles[y][x].attr.sub_x = 0;
                particles[y][x].attr.sub_y = 0;
                
              } else if (right_down_empty) {
                particles[y][x].attr.moment_x = 1;  // 右斜め下へ
                particles[y][x].attr.sub_x = 0;
                particles[y][x].attr.sub_y = 0;
              } else {
                // 両方埋まっているなら、スリープ状態へ
                particles[y][x].attr.moment_x = -4;
                continue;
              }
            } else {
              // 最下段にいるので、スリープ状態へ
              particles[y][x].attr.moment_x = -4;
              continue;
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
        if (next_y_hi > FIELD_SIZE_Y - 1) { next_y_hi = FIELD_SIZE_Y - 1; particles[y][x].attr.moment_y = 0; }
        if (next_x_hi < 0)                { next_x_hi = 0;                particles[y][x].attr.moment_x = 0; }
        if (next_x_hi > FIELD_SIZE_X - 1) { next_x_hi = FIELD_SIZE_X - 1; particles[y][x].attr.moment_x = 0; }

        // --- 運命の衝突判定 ---
        // 移動先が「自分自身と同じ場所」か、あるいは「移動先が空っぽ」なら移動成功！
        if ((next_y_hi == y && next_x_hi == x) || particles[next_y_hi][next_x_hi].attr.color == 0) {

          if (next_y_hi == y && next_x_hi == x) {
            // 【パターンA】同じマス内でのサブピクセル移動（微細な動き）
            // データの退避やクリアは不要！ 小数点座標だけをダイレクトに更新する
            particles[y][x].attr.sub_y = next_py & 7;
            particles[y][x].attr.sub_x = next_px & 7;
            
            // 完全に同じマス内の動きなので、GVRAMへの転送（invalidate）は不要！！
            // （ドット単位の描画は次のグリッド移動時に反映されるため、ここでラインを汚す必要はありません）
            screen_buffers[page_calc][y][x] = particles[y][x].attr.color;

          } else {

            // 【パターンB】別のマスへの本当の移動！
            PARTICLE tmp = particles[y][x];
            tmp.attr.sub_y = next_py & 7;
            tmp.attr.sub_x = next_px & 7;

            // 元の場所を消して、新しい場所に書き込む
            particles[y][x].raw = 0;
            particles[next_y_hi][next_x_hi].raw = tmp.raw;

            // 画面描画用バッファにカラーコードを書き込む
            screen_buffers[page_calc][y][x] = 0; 
            screen_buffers[page_calc][next_y_hi][next_x_hi] = tmp.attr.color;

            // マスが変わったので、元いたラインと、移動先のラインを再描画対象にする
            invalidates[page_calc][y] = 1;
            invalidates[page_calc][next_y_hi] = 1;
            
            // ★【前述のウェイクアップ処理を入れるならここ！】
            // 実際に別のマスへ移動が起きたので、周囲の砂を起こす
            if (y > 0) {
              if (x > 0)                { if (particles[y-1][x-1].attr.moment_x == -4) particles[y-1][x-1].attr.moment_x = 0; }
              { if (particles[y-1][x].attr.moment_x == -4)   particles[y-1][x].attr.moment_x = 0; }
              if (x < FIELD_SIZE_X - 1) { if (particles[y-1][x+1].attr.moment_x == -4) particles[y-1][x+1].attr.moment_x = 0; }
            }
            if (x > 0)                { if (particles[y][x-1].attr.moment_x == -4) particles[y][x-1].attr.moment_x = 0; }
            if (x < FIELD_SIZE_X - 1) { if (particles[y][x+1].attr.moment_x == -4) particles[y][x+1].attr.moment_x = 0; }
          }

          // 実際に元の座標(y, x)から別の座標へ移動が起きた場合のみ、周囲を起こす
          if (next_y_hi != y || next_x_hi != x) {
            // 1. 上の行の3粒を起こす
            if (y > 0) {
              if (x > 0)                { if (particles[y-1][x-1].attr.moment_x == -4) particles[y-1][x-1].attr.moment_x = 0; }
              { if (particles[y-1][x].attr.moment_x == -4)   particles[y-1][x].attr.moment_x = 0; }
              if (x < FIELD_SIZE_X - 1) { if (particles[y-1][x+1].attr.moment_x == -4) particles[y-1][x+1].attr.moment_x = 0; }
            }
            // 2. 自分の真横の2粒を起こす
            if (x > 0)                { if (particles[y][x-1].attr.moment_x == -4) particles[y][x-1].attr.moment_x = 0; }
            if (x < FIELD_SIZE_X - 1) { if (particles[y][x+1].attr.moment_x == -4) particles[y][x+1].attr.moment_x = 0; }
          }

        } else {
          // 衝突した（直下が埋まっていた）
          particles[y][x].attr.moment_y = 0; // 縦の勢いは止まる

          // 左右の空き状況を見て、斜面に沿って転がす
          int16_t left_empty  = (x > 0  && particles[y+1][x-1].attr.color == 0);
          int16_t right_empty = (x < FIELD_SIZE_X - 1 && particles[y+1][x+1].attr.color == 0);

          if (left_empty && right_empty) {
            // 両方空いていたらランダムでどちらかに滑る
            particles[y][x].attr.moment_x = (quickrand() & 1) ? -1 : 1;
          } else if (left_empty) {
            particles[y][x].attr.moment_x = -1; // 左に滑る
          } else if (right_empty) {
            particles[y][x].attr.moment_x = 1;  // 右に滑る
          } else {
            if ((quickrand() & 63) == 0) {
              particles[y][x].attr.moment_x = -4; // 睡眠状態へ
            } else {
              particles[y][x].attr.moment_x = 0;  // まだ起きて微振動のチャンスを残す
            }
          }

        }
      }
    }

    // 追い越しガード
    while (page_calc == page_render) {
        // 060が速すぎる場合、次のVSYNCが来て割り込みが page_next を
        // 受け取ってくれるまで、ここで数ミリ秒だけ安全に待機します。
    }

    // バッファ間の差分コピー
    int16_t page_calc_next = page_render;
    uint64_t* src = (uint64_t*)&screen_buffers[page_calc]; // 直前に完成した画面
    uint64_t* dst = (uint64_t*)&screen_buffers[page_calc_next]; // これから計算する画面
    for (uint16_t y = 0; y < FIELD_SIZE_Y; y++) {
      if (invalidates[page_calc][y]) {
        *dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
        *dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
        *dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
        *dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
      } else {
        src += 20;
        dst += 20;
      }
    }
    
    // バッファの入れ替え
    int16_t old_calc = page_calc;
    page_calc = page_render; // 割り込みが「使い終わった面」を、次の計算面として回収する！
    page_next = old_calc;    // 今回書き上げた面を「次回の表示待ち」にする

  }

  rc = 0;

exit:

  if (vsync > 0) {
    _iocs_vdispst(0, 0, 0);
  }

  if (usp > 0) {
    _iocs_b_super(usp);
  }

  if (funckey_mode >= 0) {
    _dos_c_fnkmod(funckey_mode);
  }

  _dos_c_curon();

  // キーバッファフラッシュ
  _dos_kflushio(0xff);

  return rc;
}
