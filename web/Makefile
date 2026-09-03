# Shakti → WASM (browser). Host: make wasm
ROOT := ..
SRC := $(ROOT)/src
BUILD := $(ROOT)/.build/wasm
DIST := $(BUILD)
SITE := /home/f/workspace/shakti.com
CC := emcc

CFLAGS := -O2 -std=gnu11 -Wall -Wno-unused-parameter -Wno-unused-function \
	-Wno-unused-variable -Wno-unused-but-set-variable -Wno-sign-compare \
	-I$(BUILD) -I$(SRC) \
	-DSHAKTI_WASM=1 -DSHAKTI_HAVE_SYNTH=1 -DSHAKTI_HAVE_GFX=1 \
	-DSHAKTI_STANDALONE=1 -DSHAKTI_NO_MAIN=1 -D_GNU_SOURCE \
	-include sys/wait.h -include unistd.h -include pthread.h

SRCS := \
	$(SRC)/alloc.c $(SRC)/value.c $(SRC)/env.c $(SRC)/lex.c $(SRC)/ast.c \
	$(SRC)/parse.c $(SRC)/vec_ops.c $(SRC)/eval.c $(SRC)/import.c $(SRC)/repl.c \
	$(SRC)/shakti_lang.c $(SRC)/builtin.c $(SRC)/table_sql.c $(SRC)/mat_simd.c \
	$(SRC)/vec_kernels.c $(SRC)/fb_present.c \
	$(SRC)/methods.c $(SRC)/stdlib.c $(SRC)/json_parse.c $(SRC)/table_io.c \
	$(SRC)/table_xml.c $(SRC)/input.c $(SRC)/isolde_bridge.c \
	$(SRC)/rest.c $(SRC)/graph.c $(SRC)/machine.c $(SRC)/pcm.c $(SRC)/subprocess.c \
	$(SRC)/ipc.c \
	$(SRC)/gfx.c $(SRC)/gfx_wasm.c \
	$(SRC)/synth.c $(SRC)/synth_ui.c $(SRC)/synth_audio_wasm.c \
	$(SRC)/wasm_host.c

EXPORTS := _malloc,_free,_shakti_boot,_shakti_start,_shakti_tick,_shakti_eval,_shakti_fb_ptr,_shakti_fb_w,_shakti_fb_h,_shakti_mouse,_shakti_mouse_move,_shakti_resize,_shakti_audio_buf,_shakti_audio_frames,_shakti_render

LDFLAGS := -s MODULARIZE=1 \
	-s EXPORT_NAME=Shakti \
	-s ENVIRONMENT=web \
	-s ALLOW_MEMORY_GROWTH=1 \
	-s INITIAL_MEMORY=134217728 \
	-s STACK_SIZE=2097152 \
	-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','HEAPU32','HEAPF32','UTF8ToString','stringToUTF8'] \
	-s EXPORTED_FUNCTIONS='[$(EXPORTS)]' \
	--preload-file $(ROOT)/lib@/shakti/lib \
	--preload-file $(ROOT)/IE.txt@/shakti/IE.txt \
	--preload-file $(SITE)/site@/site

.PHONY: all clean

all: $(DIST)/shakti.js

$(BUILD)/shakti_version.h: $(ROOT)/src/VERSION
	@mkdir -p $(BUILD)
	@sed 's/.*/#define SHAKTI_PKG_VERSION "&"/' $< > $@

$(DIST)/shakti.js: $(SRCS) $(BUILD)/shakti_version.h Makefile $(SITE)/site/main.ie $(ROOT)/IE.txt
	@mkdir -p $(DIST)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)

clean:
	rm -rf $(DIST)
