# elf2x68k インストールパス
ifndef ELF2X68K_DIR
	ELF2X68K_DIR=/opt/homebrew/Cellar/elf2x68k/20260331
endif

# デフォルトサフィックスを削除
.SUFFIXES:

# バージョン
VERSION = 020

# 実行ファイル名
TARGET_FILE = SANDBOX.X

# ドキュメントファイル名
DOCUMENT_FILE = SANDBOX.TXT

# 配布パッケージファイル名 
PACKAGE_FILE = ../SANDB$(VERSION).ZIP

# 各種コマンド短縮名
CC = m68k-xelf-gcc
AS = m68k-xelf-as

# ヘッダ検索パス
INCLUDE_FLAGS = -I${ELF2X68K_DIR}/lib/gcc/m68k-elf/13.4.0/include \
				-I${ELF2X68K_DIR}/m68k-elf/include

# リンカオプションとライブラリ検索パス
LDFLAGS = 

# *.c ソースファイル
C_SRCS = main.c

# *.s ソースファイル
ASM_SRCS = 

# *.h source files
HEADER_SRCS = keyboard.h crtc.h pattern.h sandbox.h

# リンク対象のライブラリファイル
LIBS =

# 中間ファイル生成用ディレクトリ
INTERMEDIATE_DIR = _build

# オブジェクトファイル
OBJS =	$(addprefix $(INTERMEDIATE_DIR)/,$(patsubst %.c,%.o,$(C_SRCS))) \
	   	$(addprefix $(INTERMEDIATE_DIR)/,$(patsubst %.s,%.o,$(ASM_SRCS)))

# コンパイルフラグ
COMMON_FLAGS = -m68060 -O2 -msoft-float -specs=nano.specs $(INCLUDE_FLAGS) -fno-defer-pop
#COMMON_FLAGS = -m68060 -O2 -msoft-float $(INCLUDE_FLAGS) -fno-defer-pop
CFLAGS = $(COMMON_FLAGS)

# デフォルトのターゲット
all : ${INTERMEDIATE_DIR}/$(TARGET_FILE)

# 中間生成物の削除
clean : 
	rm -f $(PACKAGE_FILE)
	rm -rf $(INTERMEDIATE_DIR)

# 実行ファイルの生成
${INTERMEDIATE_DIR}/$(TARGET_FILE) : $(OBJS)
	mkdir -p $(INTERMEDIATE_DIR)
	$(CC) $(COMMON_FLAGS) $(LDFLAGS) -o ${INTERMEDIATE_DIR}/${TARGET_FILE} $(OBJS) $(LIBS)

# *.c ソースのコンパイル
$(INTERMEDIATE_DIR)/%.o : %.c Makefile $(HEADER_SRCS)
	mkdir -p $(INTERMEDIATE_DIR)
	$(CC) $(CFLAGS) -c $*.c -o $(INTERMEDIATE_DIR)/$*.o

# 配布用パッケージ作成
package:
	zip -j ${PACKAGE_FILE} \
			${DOCUMENT_FILE} \
			${INTERMEDIATE_DIR}/${TARGET_FILE}
