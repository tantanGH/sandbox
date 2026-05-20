#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <x68k/dos.h>
#include <x68k/iocs.h>

#include "sandbox.h"
#include "pattern.h"
#include "keyboard.h"
//#include "pcm_data.h"

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

// 色別 1x2 グリッドバッファ
static BITLINE80 coarse_map[4][FIELD_SIZE_Y/2];

// VSYNC割り込みが「いま現在描画中」の面
volatile static int16_t page_render = 0;

// メインループが「いま現在書き込み中」の面
volatile static int16_t page_calc = 1;

// VSYNC割り込みが「次に描画する」の面
volatile static int16_t page_next = 2;

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
static volatile int16_t block_event_next = 0;

static volatile int16_t sp_color = -1;
static int16_t sp_x[4];          // 各ブロックの位置
static int16_t sp_y[4];          // 各ブロックの位置

static int16_t sp_color_next = -1;
static int16_t sp_x_next[4];          // 各ブロックの位置(NEXT)
static int16_t sp_y_next[4];          // 各ブロックの位置(NEXT)

// VSYNC割り込みハンドラ
static void __attribute__((interrupt)) refresh_screen() {

  // スプライトの処理
  if (block_event_next) {

    // 次のミノを表示
    SP_SCRL[ 16 ] = sp_x_next[0];
    SP_SCRL[ 17 ] = sp_y_next[0];
    SP_SCRL[ 18 ] = 0x100 + sp_color_next;
    SP_SCRL[ 19 ] = 3;

    SP_SCRL[ 20 ] = sp_x_next[1];
    SP_SCRL[ 21 ] = sp_y_next[1];
    SP_SCRL[ 22 ] = 0x100 + sp_color_next;
    SP_SCRL[ 23 ] = 3;

    SP_SCRL[ 24 ] = sp_x_next[2];
    SP_SCRL[ 25 ] = sp_y_next[2];
    SP_SCRL[ 26 ] = 0x100 + sp_color_next;
    SP_SCRL[ 27 ] = 3;

    SP_SCRL[ 28 ] = sp_x_next[3];
    SP_SCRL[ 29 ] = sp_y_next[3];
    SP_SCRL[ 30 ] = 0x100 + sp_color_next;
    SP_SCRL[ 31 ] = 3;

    block_event_next = 0;
  }

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

// ミノを置けるかのチェック
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

// ミノを設置する
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

// 8x12ドットの範囲に砂を配置する
static void put_particles(int16_t pos_x, int16_t pos_y, int16_t color) {
  
  int16_t c_group = color & 3;

  //　ループに入る前に、画面外にはみ出さないようにループの範囲自体をクリップする
  int16_t start_y = (pos_y < 0) ? -pos_y : 0;
  int16_t end_y   = (pos_y + NUM_SANDS_Y > FIELD_SIZE_Y) ? (FIELD_SIZE_Y - pos_y) : NUM_SANDS_Y;

  int16_t start_x = (pos_x < 0) ? -pos_x : 0;
  int16_t end_x   = (pos_x + NUM_SANDS_X > FIELD_SIZE_X) ? (FIELD_SIZE_X - pos_x) : NUM_SANDS_X;

  for (int16_t sy = start_y; sy < end_y; sy++) {

    int16_t py = pos_y + sy;
    int16_t cy = py >> 1; // 縦2ドット単位のインデックス

    for (int16_t sx = start_x; sx < end_x; sx++) {

      int16_t px = pos_x + sx;

      // 輝度(t)の決定ロジック
      int16_t t = ((sy % NUM_SANDS_Y) == (NUM_SANDS_Y - 1) || (sx % NUM_SANDS_X) == (NUM_SANDS_X - 1)) ? 0 : ((quickrand() & 7) < 6) ? 1 : 2;
      int16_t c = (t << 2) + c_group + 1; // 輝度を混ぜたカラーコード

      // 粒子データと描画バッファの更新
      particles[py][px].attr.color = c;
      particles[py][px].attr.moment_y = 0;
      particles[py][px].attr.moment_x = 0;
      screen_buffers[page_calc][py][px] = c;
      invalidates[page_calc][py] = 1;

      // 色グループ（c_group）のビットマップに書き込む
      if (px < 32) {
        coarse_map[c_group][cy].lo |= (1 << px);
      } else if (px < 64) {
        coarse_map[c_group][cy].mi |= (1 << (px - 32));
      } else {
        coarse_map[c_group][cy].hi |= (1 << (px - 64));
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

  // PCGパターン(ブロック) 16x16が4つ
  for (int16_t i = 0; i < 4; i++) {
    memcpy((void*)&PCG[i * 64], (void*)(&block_patterns[i * 64]), 128);
  }
  
  // PCGパターン(パイプ) 8x8が4つ
  for (int16_t i = 0; i < 1; i++) {
    memcpy((void*)&PCG[(i+4) * 64], (void*)(&pipe_patterns[i * 64]), 128);
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
    int16_t p = (y == 0) ? 17 : (y == 29) ? 18 : 18;    // 19 : 18
    BG_TEXT1[ y * 64 + 2 ] = 0x200 + p;
    BG_TEXT1[ y * 64 + 2 + 11 ] = 0x200 + p;
  }

  *BG_CTRL = 0x203;   // SP/BG ON, BG1-BGTEXT0, BG0-BGTEXT1, BG1 OFF, BG0 ON 

  // グラフィックパレット設定
  for (int16_t i = 0; i < 16; i++) {
    _iocs_gpalet(i, graphic_palette_colors[i]);
  }

  // テキスト表示
  put_text(144,8,2,1,"HI SCORE");
  put_text(144,40,2,1,"SCORE");
  put_text(144,72,2,1,"NEXT");

  // ハイスコア
  uint32_t hi_score = 76500;

game_start:

  // ハイスコア表示
  static uint8_t score_mes[256];
  sprintf(score_mes,"%8d",hi_score);
  put_text(144,16,3,1,score_mes);

  // スコア
  uint32_t score = 0;
  sprintf(score_mes,"%8d",score);
  put_text(144,48,3,1,score_mes);
  
  // 物理演算バッファ初期化
  memset(particles, 0, FIELD_SIZE_Y * FIELD_SIZE_X * sizeof(PARTICLE));

  // 画面描画ダブルバッファ初期化
  memset(screen_buffers, 0, 2 * FIELD_SIZE_Y * FIELD_SIZE_X * sizeof(uint16_t));
  memset(invalidates, 1, 2 * FIELD_SIZE_Y * sizeof(uint8_t));

  // 色別グリッドバッファ初期化
  memset(coarse_map, 0, 4 * FIELD_SIZE_Y / 2 * sizeof(BITLINE80));

  // 画面オフセット
  int16_t grid_x = 6;
  int16_t grid_y = 0;

  // カウンタ
  uint32_t counter = 0;
  uint32_t clear_freeze_counter = 0;
  uint32_t deploy_freeze_counter = 0;
  uint32_t mino_counter = 0;

  // ゲームオーバー判定
  int16_t game_over = 0;

  // 連鎖カウンタ
  int16_t combo_count = 0;         // 現在の連鎖数（0 = 連鎖なし、1 = 2連鎖目...
  int16_t combo_valid_counter = 0;   // 連鎖を受け付ける残りフレーム数（0で連鎖終了）

  // スプライト色と位置
  int16_t sp_mino = -1;
  int16_t sp_rotation = -1;
  int16_t sp_pos_x = -1;    // 基準位置
  int16_t sp_pos_y = -1;    // 基準位置

  // ボタン押しっぱなし判定用
  int16_t trigger_a = 0;
  int16_t trigger_b = 0;

  // 最初の次のミノ
  int16_t sp_mino_next = rand() % 7;
  int16_t sp_rotation_next = rand() % 4;
  int16_t sp_pos_x_next = 160 + 16;
  int16_t sp_pos_y_next = 88 + 16;
  sp_color_next = rand() % 4;
  locate_mino(sp_pos_x_next, sp_pos_y_next, sp_mino_next, sp_rotation_next, sp_x_next, sp_y_next);

  // グローバル変数初期化
  page_render = 0;
  page_calc = 1;
  page_next = 0;
  block_event_new = 0;
  block_event_delete = 0;
  block_event_new = 0;

  // 探索用の「火が燃え広がっているマップ」
  static BITLINE80 fire[FIELD_SIZE_Y/2];
  int16_t fire_c = -1;    // 左右連結した色

  // 開始待ち
  put_text(8,128,3,1,"PUSH SPACE KEY");
  for (;;) {
    if (_iocs_b_keysns() != 0) {
      int16_t scan_code = _iocs_b_keyinp() >> 8;
      if (scan_code == KEY_SCAN_CODE_SPACE) {
        break;
      } else if (scan_code == KEY_SCAN_CODE_ESC) {
        goto exit;
      }
    }
    if ((_iocs_joyget(0) & 0x20) == 0) break;
  }
  put_text(8,128,3,1,"              ");

  // VSYNC割り込み開始
  int16_t vsync = 0;
  if (_iocs_vdispst((uint8_t*)refresh_screen, 0, 1) != 0) {
    printf("VSYNC割り込みが使用中です。\n");
    goto exit;
  }
  vsync = 1;

  // タイマーAをリセットしておかないと、ハードリセット後にVSYNC割り込みがすぐに開始しない
  _iocs_b_bpoke((uint8_t*)0xe88019,0x00);  // stop Timer-A  
  _iocs_b_bpoke((uint8_t*)0xe8801f,0x01);  // set Timer-A counter        
  _iocs_b_bpoke((uint8_t*)0xe88019,0x18);  // restart Timer-A    

  for (;;) {

    // ESCキーが押されたら終了
    if (_iocs_b_keysns() != 0) {
      int16_t scan_code = _iocs_b_keyinp() >> 8;
      if (scan_code == KEY_SCAN_CODE_ESC) {
        goto exit;
      }
    }

    // このフレームで物理を動かすかどうか
    int16_t run_physics = 1;

    // --- 連鎖受付タイマーのカウントダウン ---
    if (combo_valid_counter > 0) {
      combo_valid_counter--;
      if (combo_valid_counter == 0) {
        combo_count = 0; // 時間切れになったら連鎖数をリセット
      }
    }

    // 左右開通時のフリーズタイマーチェック
    if (clear_freeze_counter > 0) {

      clear_freeze_counter--;

      // ========================================================
      // 【追加】カウントダウン途中（タイマー > 0）の明滅演出
      // ========================================================
      if (clear_freeze_counter > 0 && fire_c >= 0 && (clear_freeze_counter % 4) == 0) {

        // タイマーのビット3を使って、4フレームごとに「点滅のON/OFF」を切り替える
        int16_t flash_on = (clear_freeze_counter & 4); 

        // 開通した「火のルート」だけを走査
        for (int16_t fy = FIELD_SIZE_Y/2 - 1; fy >= 0; fy--) {
          BITLINE80 f = fire[fy];
          if (f.lo == 0 && f.mi == 0 && f.hi == 0) continue;

          int16_t py0 = fy << 1;
          int16_t py1 = py0 + 1;

          for (int16_t px = 0; px < FIELD_SIZE_X; px++) {
            int is_fired = 0;
            if (px < 32)      { if (f.lo & (1 << px)) is_fired = 1; }
            else if (px < 64) { if (f.mi & (1 << (px - 32))) is_fired = 1; }
            else              { if (f.hi & (1 << (px - 64))) is_fired = 1; }

            if (is_fired) {
              // 火が通っているドットの色が、開通した色グループ（fire_c）と一致するか確認
              // ※ py0のチェック
              if (particles[py0][px].raw != 0 && ((particles[py0][px].attr.color - 1) & 3) == fire_c) {
                if (flash_on) {
                  // ONのとき：元の色グループ（0〜3）に対応する白パレット（13, 14, 15など）をぶち込む
                  screen_buffers[page_calc][py0][px] = 13 + ((particles[py0][px].attr.color - 1) >> 2);
                } else {
                  // OFFのとき：元の砂の色（particlesに保存されている色）に戻す
                  screen_buffers[page_calc][py0][px] = particles[py0][px].attr.color;
                }
                invalidates[page_calc][py0] = 1; // 画面書き換えフラグを立てる
              }

              // ※ py1のチェック
              if (py1 < FIELD_SIZE_Y && particles[py1][px].raw != 0 && ((particles[py1][px].attr.color - 1) & 3) == fire_c) {
                if (flash_on) {
                  screen_buffers[page_calc][py1][px] = 13 + ((particles[py1][px].attr.color - 1) >> 2);
                } else {
                  screen_buffers[page_calc][py1][px] = particles[py1][px].attr.color;
                }
                invalidates[page_calc][py1] = 1;
              }
            }
          } // px
        } // fy
      }

      if (clear_freeze_counter == 0 && fire_c >= 0) {
        
        int32_t deleted_pixels = 0; // ★ 今回一度に消したドット数のカウンタ

        // 【修正1】消去処理を完全に全行回しきるため、ループ構造を整理
        for (int16_t fy = FIELD_SIZE_Y/2 - 1; fy >= 0; fy--) {

          BITLINE80 f = fire[fy];

          // この行に火（繋がっているドット）が1つもなければ、行丸ごとスキップ
          if (f.lo == 0 && f.mi == 0 && f.hi == 0) continue;

          // 縦2ドット分の画面Y座標を復元
          int16_t py0 = fy << 1;
          int16_t py1 = py0 + 1;

          // 横80ドットを走査
          for (int16_t px = 0; px < FIELD_SIZE_X; px++) {
            int is_fired = 0;

            if (px < 32) {
              if (f.lo & (1 << px)) is_fired = 1;
            } else if (px < 64) {
              if (f.mi & (1 << (px - 32))) is_fired = 1;
            } else {
              if (f.hi & (1 << (px - 64))) is_fired = 1;
            }

            if (is_fired) {
              
              // --- ① 下段（py0）の個別消去 ＆ ウェイクアップ ---
              if (particles[py0][px].raw != 0) {
                int16_t current_p_color0 = (particles[py0][px].attr.color - 1) & 3;
                
                if (current_p_color0 == fire_c) {
                  // ★ py0が消えるので、その「真上（py0 - 1）」を叩き起こす
                  int16_t upper_y0 = py0 - 1;
                  if (upper_y0 >= 0 && particles[upper_y0][px].attr.moment_x == -4) {
                    particles[upper_y0][px].attr.moment_x = 0; 
                    invalidates[page_calc][upper_y0] = 1;
                  }

                  // py0を消去
                  particles[py0][px].raw = 0;
                  screen_buffers[page_calc][py0][px] = 0;
                  invalidates[page_calc][py0] = 1;
                  deleted_pixels++;
                }
              }

              // --- ② 上段（py1）の個別消去 ＆ ウェイクアップ ---
              if (py1 < FIELD_SIZE_Y && particles[py1][px].raw != 0) {
                int16_t current_p_color1 = (particles[py1][px].attr.color - 1) & 3;
                
                if (current_p_color1 == fire_c) {
                  // ★ py1が消えるので、その「真上（つまりpy0）」を叩き起こす！
                  // ※ただし、py0が「このフレームで一緒に消える対象」なら起こす必要はないですが、
                  // もし別色で生き残る砂なら、ここで起こしてあげることで足場を失って即座に下に落ちます。
                  if (particles[py0][px].attr.moment_x == -4) {
                    particles[py0][px].attr.moment_x = 0; 
                    invalidates[page_calc][py0] = 1;
                  }

                  // py1を消去
                  particles[py1][px].raw = 0;
                  screen_buffers[page_calc][py1][px] = 0;
                  invalidates[page_calc][py1] = 1;
                  deleted_pixels++;
                }
              }

            }
          }
        }

        // 【修正】すべての色（4色分）の coarse_map を一旦完全に真っ黒（0）にする！
        memset(coarse_map, 0, sizeof(coarse_map));

        // その代わり、今画面に生き残っているすべての粒子データ（particles）を走査して、
        // 今の正しい座標で coarse_map を一から完全に再構築（リビルド）する！
        for (int16_t py = 0; py < FIELD_SIZE_Y; py++) {
          int16_t cy = py >> 1; // 縦2ドット単位
          for (int16_t px = 0; px < FIELD_SIZE_X; px++) {
            if (particles[py][px].raw != 0) {
              int16_t col_group = (particles[py][px].attr.color - 1) & 3;
              if (px < 32)        coarse_map[col_group][cy].lo |= (1 << px);
              else if (px < 64)   coarse_map[col_group][cy].mi |= (1 << (px - 32));
              else                coarse_map[col_group][cy].hi |= (1 << (px - 64));
            }
          }
        }

        // スコアアップ
        if (deleted_pixels > 0) {
            int32_t base_score = deleted_pixels * 10;
            
            // 連鎖ボーナス倍率（例：単発 = ×1、2連鎖 = ×2、3連鎖 = ×4 と指数関数的に上げる）
            int32_t combo_bonus = 1 << combo_count; 
            
            score += base_score * combo_bonus;
            sprintf(score_mes,"%8d",score);
            put_text(144,48,3,1,score_mes);

            // ★物理が再開するここから「120フレーム（2秒）」、次の連鎖を受け付ける！
            // 砂が上からドサッと落ちてきて下に定着するまでの猶予時間になります。
            // 2連鎖、3連鎖と続くほど、受付時間を少し長め（150など）にしてあげると親切です。
            combo_valid_counter = 120 + (combo_count * 20); 
        }

        fire_c = -1;

      }

      run_physics = 0;

    }

    if (run_physics) {

      // ブロック操作
      if (deploy_freeze_counter == 0 && sp_mino >= 0) {

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

        if (mino_counter < 10) {
          if ((counter % 2) == 0) {
            sp_pos_y++;
            sp_y[0]++;
            sp_y[1]++;
            sp_y[2]++;
            sp_y[3]++;
          }
        } else if (mino_counter < 20) {
          sp_pos_y++;
          sp_y[0]++;
          sp_y[1]++;
          sp_y[2]++;
          sp_y[3]++;          
        } else {
          sp_pos_y += 2;
          sp_y[0] += 2;
          sp_y[1] += 2;
          sp_y[2] += 2;
          sp_y[3] += 2;         
        }

      }

      // 新規ブロック
      if ((counter % 200) == 0 && sp_mino < 0) {

        sp_mino = sp_mino_next;
        sp_rotation = sp_rotation_next;
        sp_color = sp_color_next;
        sp_pos_x = grid_x * NUM_SANDS_X + 16;
        sp_pos_y = grid_y * NUM_SANDS_Y + 16;
        locate_mino(sp_pos_x, sp_pos_y, sp_mino, sp_rotation, sp_x, sp_y);
        mino_counter++;

        sp_mino_next = rand() % 7;
        sp_rotation_next = rand() % 4;
        sp_color_next = rand() % 4;
        locate_mino(sp_pos_x_next, sp_pos_y_next, sp_mino_next, sp_rotation_next, sp_x_next, sp_y_next);

        block_event_new = 1;
        block_event_next = 1;
      }      

      // ブロックの着地判定
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

          if (sp_pos_y < 24) {
            // ゲームオーバー
            game_over = 1;
          } else {

          //_iocs_adpcmout(pcm_noise1,4*256+3,sizeof(pcm_noise1));

          deploy_freeze_counter = 8;
          
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

          // スコアアップ
          score += 100;
          sprintf(score_mes,"%8d",score);
          put_text(144,48,3,1,score_mes);

          block_event_delete = 1;
        }
      }

      counter++;

      // 砂の動き(下から)
      for (int16_t y = FIELD_SIZE_Y-2; y >= 0; y--) {

        for (int16_t x = 0; x < FIELD_SIZE_X; x++) {
          
          // 砂がない、またはスリープ状態ならスキップ
          if (particles[y][x].attr.color == 0) {
            continue;
          }
          if (particles[y][x].attr.moment_x == -4) {
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
              //screen_buffers[page_calc][y][x] = particles[y][x].attr.color;

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

              // 色別グリッドバッファに書き込む (元のグリッドからは消さない) ---
              int16_t c       = (tmp.attr.color - 1) & 3; // 4色グループへのマスク
              int16_t next_cy = next_y_hi >> 1;     // 縦は2ドット単位なので「>> 1」(120行)

              // 移動先のX座標を基準に、lo / mi / hi へ綺麗に振り分ける
              if (next_x_hi < 32) {
                // 0 〜 31 ドット目：loレジスタ
                coarse_map[c][next_cy].lo |= (1 << next_x_hi);
              } 
              else if (next_x_hi < 64) {
                // 32 〜 63 ドット目：miレジスタ（32を引いて 0〜31番目のビットにする）
                coarse_map[c][next_cy].mi |= (1 << (next_x_hi - 32));
              } 
              else {
                // 64 〜 79 ドット目：hiレジスタ（64を引いて 0〜15番目のビットにする）
                coarse_map[c][next_cy].hi |= (1 << (next_x_hi - 64));
              }
              
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

      // 左右が繋がったかチェック (4フレームに1回)
      if ((counter % 3) == 0) {

        for (int16_t c = 0; c < 4; c++) {   // 4色

          // 【超高速事前チェック】左端の縦一列に、この色の砂が1粒でもあるか？
          //uint32_t left_wall_check = 0;
          //for (int16_t y = 0; y < FIELD_SIZE_Y/2; y++) {
          //  left_wall_check |= coarse_map[c][y].lo & 1;
          //}

          // 左端に1粒も触れていなければ、その色は開通の可能性ゼロ！
          // 100回の重い延焼ループを丸ごとスルーして次の色へ！
          //if (left_wall_check == 0) {
          //  continue;
          //}

          // 【初期化】各行の「左端（ビット0）に砂がある場所」だけに火をつける
          for (int16_t y = 0; y < FIELD_SIZE_Y/2; y++) {
            fire[y].lo = coarse_map[c][y].lo & 1;
            fire[y].mi = 0;
            fire[y].hi = 0; 
          }

          // 延焼ループ（横80bitを流し切るため100回）
          for (int16_t iter = 0; iter < 100; iter++) {

            int changed = 0; // 変化があったかのフラグ

            for (int16_t y = FIELD_SIZE_Y/2 - 1; y >= 0; y--) {

              BITLINE80 current_sand = coarse_map[c][y]; 
              BITLINE80 f = fire[y];

              // その行に砂が1粒もなければ、上下左右の計算をすべて飛ばす
              if (current_sand.lo == 0 && current_sand.mi == 0 && current_sand.hi == 0 &&
                  f.lo == 0 && f.mi == 0 && f.hi == 0) {
                  continue;
              }

              BITLINE80 next_f;

              // --- 火を「右（ビットが増える方向 = << 1）」へ広げる ---
              uint32_t carry_lo = f.lo >> 31; // loの最上位は、miの最下位(ビット0)へ
              uint32_t carry_mi = f.mi >> 31; // miの最上位は、hiの最下位(ビット0)へ
              
              next_f.lo = (f.lo << 1);
              next_f.mi = (f.mi << 1) | carry_lo;
              next_f.hi = (f.hi << 1) | carry_mi;

              // --- 火を「左（ビットが減る方向 = >> 1）」へ広げる（折り返し用） ---
              uint32_t borrow_mi = (f.mi & 1) << 31; // miの最下位は、loの最上位(ビット31)へ
              uint32_t borrow_hi = (f.hi & 1) << 31; // hiの最下位は、miの最上位(ビット31)へ

              next_f.lo |= (f.lo >> 1) | borrow_mi;
              next_f.mi |= (f.mi >> 1) | borrow_hi;
              next_f.hi |= (f.hi >> 1);

              // 自分の今の位置の火も保持
              next_f.lo |= f.lo;
              next_f.mi |= f.mi;
              next_f.hi |= f.hi;

              // 砂がない場所の火を消す
              next_f.lo &= current_sand.lo; 
              next_f.mi &= current_sand.mi; 
              next_f.hi &= current_sand.hi; 

              // --- 上下の行から火をもらってくる ---
              // 上下の行で「お互いに砂が存在するX座標」だけを抽出し、そこを通ってきた火だけを混ぜる
              if (y > 0)  {
                BITLINE80 upper_sand = coarse_map[c][y-1];
                next_f.lo |= (fire[y-1].lo & upper_sand.lo & current_sand.lo);
                next_f.mi |= (fire[y-1].mi & upper_sand.mi & current_sand.mi);
                next_f.hi |= (fire[y-1].hi & upper_sand.hi & current_sand.hi);
              }
              if (y < (FIELD_SIZE_Y/2 - 1)) {
                BITLINE80 lower_sand = coarse_map[c][y+1];
                next_f.lo |= fire[y+1].lo & lower_sand.lo & current_sand.lo;
                next_f.mi |= fire[y+1].mi & lower_sand.mi & current_sand.mi;
                next_f.hi |= fire[y+1].hi & lower_sand.hi & current_sand.hi;
              }

              // もし前回の火のマップから変化があったらフラグを立てる
              if (next_f.lo != f.lo || next_f.mi != f.mi || next_f.hi != f.hi) {
                changed = 1;
              }
              
              // 火のマップを更新
              fire[y].lo = next_f.lo;
              fire[y].mi = next_f.mi;
              fire[y].hi = next_f.hi;
            }

            // 今回の走査で全く変化がなければ中止
            if (!changed) break;
          }

          // 【最終判定】どこか2行が右端（ビット79=hiのビット15）に火が到達していれば開通！
          int connected = 0;
          for (int16_t y = FIELD_SIZE_Y/2 - 2; y >= 0; y--) {
            if ((fire[y].hi & (1 << 15)) && (fire[y+1].hi & (1 << 15))) {

              clear_freeze_counter = 55;  // タイマーセット
              fire_c = c;

              if (combo_valid_counter > 0) {
                // 前回の消去から時間内なら、連鎖数をアップ！
                combo_count++; 
              } else {
                // 時間外（単発消去）なら、ここから連鎖スタート（combo_count = 0）
                combo_count = 0; 
              }

              // フリーズ演出に入るので、受付タイマーは一旦止める（フリーズ中は減算しない）
              combo_valid_counter = 0;

              connected = 1;
              break; // 内側の判定yループを抜ける
            }
          }

          // 【修正3】この色で消去が発生したら、他の色の処理をせず今回の判定フレームを完了する
          if (connected) break; 
        }
      }
    
    } // if (run_physics) { 

    // 追い越しガード
    while (page_calc == page_render) {
        // 次のVSYNC割り込みが page_next を受け取ってくれるまで待機
    }

    // ブロック着地後の振動
    if (deploy_freeze_counter > 0) {

      switch (deploy_freeze_counter) {
        case  7: GR0_SCRL[1] = 510; BG0_SCRL[1] = 510; break;
        case  5: GR0_SCRL[1] = 509; BG0_SCRL[1] = 509; break;
        case  3: GR0_SCRL[1] = 510; BG0_SCRL[1] = 510; break;
        case  1: GR0_SCRL[1] =   0; BG0_SCRL[1] =   0; break;
      }

      deploy_freeze_counter--;
    }

    // バッファ間の差分コピー
    int16_t page_calc_next = page_render;   // このページはもう表示済みなので、次の計算用に使う
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
    page_next = page_calc;         // 今回書き上げた面を「次回の表示待ち」にする
    page_calc = page_calc_next;    // 割り込みが「使い終わった面」を、次の計算面として回収する

    if (game_over) break;

  }

  if (vsync > 0) {
    _iocs_vdispst(0, 0, 0);
  }

  put_text(28,128,3,1,"GAME OVER");
  for (;;) {
    if (_iocs_b_keysns() != 0) {
      int16_t scan_code = _iocs_b_keyinp() >> 8;
      if (scan_code == KEY_SCAN_CODE_SPACE) {
        break;
      } else if (scan_code == KEY_SCAN_CODE_ESC) {
        goto exit;
      }
    }
    if ((_iocs_joyget(0) & 0x20) == 0) break;
  }
  put_text(28,128,3,1,"         ");

  if (score > hi_score) hi_score = score;

  usleep(500);

  goto game_start;

  rc = 0;

exit:

  if (vsync > 0) {
    _iocs_vdispst(0, 0, 0);
  }

  if (usp > 0) {
    _iocs_b_super(usp);
  }

  _iocs_crtmod(16);
  _iocs_g_clr_on();

  if (funckey_mode >= 0) {
    _dos_c_fnkmod(funckey_mode);
  }

  _dos_c_curon();

  // キーバッファフラッシュ
  _dos_kflushio(0xff);

  return rc;
}
