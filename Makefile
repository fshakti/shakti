# GNU Make build: standalone CLI.
BUILD := .build
SHAKTI := $(BUILD)/shakti
.DEFAULT_GOAL := build
UNAME_S := $(shell uname -s 2>/dev/null || echo unknown)
UNAME_M := $(shell uname -m 2>/dev/null || echo unknown)

ifeq ($(UNAME_S),Darwin)
  CC ?= clang
  OBJC ?= clang
  SHAKTI_USE_ACCELERATE ?= 1
endif
CC ?= gcc

ifeq ($(UNAME_S),Darwin)
  ifneq ($(wildcard /opt/homebrew/opt/libomp/include/omp.h),)
    LIBOMP_PREFIX := /opt/homebrew/opt/libomp
  else ifneq ($(wildcard /usr/local/opt/libomp/include/omp.h),)
    LIBOMP_PREFIX := /usr/local/opt/libomp
  endif
  ifneq ($(LIBOMP_PREFIX),)
    OMP_CFLAGS = -Xpreprocessor -fopenmp -I$(LIBOMP_PREFIX)/include
    OMP_LDFLAGS = -L$(LIBOMP_PREFIX)/lib -lomp
  else
    $(warning libomp not found — OpenMP disabled. Install with: brew install libomp)
  endif
else
  OMP_CFLAGS = -fopenmp
  OMP_LDFLAGS = -lgomp
endif

CFLAGS := -O2 -g -Wall -Wextra -Wno-misleading-indentation -Wno-sign-compare -Wno-unused-result -Wno-format-truncation -Wno-missing-field-initializers -std=gnu11 -D_GNU_SOURCE \
	-I$(BUILD) -Isrc \
	$(OMP_CFLAGS)

# Optional JNI headers for src/shakti_jni.c (Homebrew OpenJDK or JAVA_HOME).
JNI_CFLAGS :=
ifeq ($(JAVA_HOME),)
  ifneq ($(wildcard /opt/homebrew/opt/openjdk@17/include/jni.h),)
    JAVA_HOME := /opt/homebrew/opt/openjdk@17
  else ifneq ($(wildcard /opt/homebrew/opt/openjdk/include/jni.h),)
    JAVA_HOME := /opt/homebrew/opt/openjdk
  else ifneq ($(wildcard /usr/local/opt/openjdk/include/jni.h),)
    JAVA_HOME := /usr/local/opt/openjdk
  endif
endif
ifneq ($(JAVA_HOME),)
  ifneq ($(wildcard $(JAVA_HOME)/include/jni.h),)
    JNI_CFLAGS += -I$(JAVA_HOME)/include
    ifneq ($(wildcard $(JAVA_HOME)/include/darwin),)
      JNI_CFLAGS += -I$(JAVA_HOME)/include/darwin
    endif
    ifneq ($(wildcard $(JAVA_HOME)/include/linux),)
      JNI_CFLAGS += -I$(JAVA_HOME)/include/linux
    endif
  endif
endif

# -Wno-alloc-size-larger-than is a GCC-only flag; clang rejects it as "unknown
# warning option", so only pass it when the compiler is not clang.
ifeq ($(findstring clang,$(shell $(CC) --version 2>/dev/null)),)
  CFLAGS += -Wno-alloc-size-larger-than
endif

LDFLAGS := -lm $(OMP_LDFLAGS)
ifneq ($(filter Linux,$(UNAME_S)),)
  LDFLAGS += -lrt -ldl -Wl,-export-dynamic
endif
ifeq ($(UNAME_S),Darwin)
  ifeq ($(SHAKTI_USE_ACCELERATE),1)
    CFLAGS += -DSHAKTI_USE_ACCELERATE=1
    LDFLAGS += -framework Accelerate
  endif
  # Raw-PCM audio output (src/pcm.c) uses the AudioQueue C API.
  LDFLAGS += -framework AudioToolbox -framework CoreFoundation
endif

# Raw-PCM audio output on Linux uses ALSA when the dev headers are present.
# Define the macro and link the lib together so src/pcm.c never compiles the
# ALSA backend without -lasound also being linked.
ifeq ($(UNAME_S),Linux)
  ifneq ($(wildcard /usr/include/alsa/asoundlib.h),)
    CFLAGS += -DSHAKTI_PCM_ALSA=1
    LDFLAGS += -lasound
  endif
endif

ifeq ($(filter Linux Darwin,$(UNAME_S)),)
else
  CFLAGS += -DSHAKTI_HAVE_LIBEXPAT=1
  LDFLAGS += -lexpat
endif

# Language sources are separate TUs (lex/parse/eval/...). LTO recovers inlining
# that the old single-file shakti_lang.c got for free — tiny eval() parse of
# jupyter cells is parse-bound without it.
ifeq ($(UNAME_S),Darwin)
  CFLAGS += -flto
  LDFLAGS += -flto
else
  CFLAGS += -flto=auto
  LDFLAGS += -flto=auto
endif

LANG_STANDALONE := src/alloc.c src/value.c src/env.c src/lex.c src/ast.c src/parse.c src/vec_ops.c src/eval.c src/import.c src/repl.c src/shakti_lang.c src/builtin.c src/table_sql.c src/mat_simd.c src/vec_kernels.c src/fb_present.c
LIBSRCS_STANDALONE := src/methods.c src/stdlib.c src/json_parse.c src/table_io.c src/table_xml.c src/cli_main.c src/input.c src/isolde_bridge.c src/rest.c src/graph.c src/machine.c src/pcm.c src/subprocess.c

SHAKTI_IPC ?= 1
SHAKTI_RDMA ?= 1

ifeq ($(SHAKTI_IPC),1)
  CFLAGS += -DSHAKTI_HAVE_IPC=1
  LIBSRCS_STANDALONE += src/ipc.c
  ifeq ($(UNAME_S),Linux)
    ifeq ($(SHAKTI_RDMA),1)
      ifneq ($(wildcard /usr/include/infiniband/verbs.h),)
        ifneq ($(wildcard /usr/include/rdma/rdma_cma.h),)
          CFLAGS += -DSHAKTI_HAVE_RDMA=1
          LIBSRCS_STANDALONE += src/ipc_rdma.c
          IPC_LDFLAGS := -lrdmacm -libverbs
        endif
      endif
    endif
  endif
endif

ifeq ($(UNAME_S),Darwin)
  SHAKTI_TALK ?= 1
else
  SHAKTI_TALK ?= 0
endif

ifeq ($(SHAKTI_TALK),1)
  CFLAGS += -DSHAKTI_HAVE_TALK=1
  TALK_LDFLAGS := -framework AVFoundation -framework AudioToolbox -framework Foundation -framework Speech
  OBJC ?= clang
  TALK_OBJC_FLAGS := -x objective-c -O2 -g -Wall -std=gnu11 -fobjc-arc -DSHAKTI_HAVE_TALK=1 \
	-I$(BUILD) -Isrc
  ifeq ($(SHAKTI_USE_ACCELERATE),1)
    TALK_OBJC_FLAGS += -DSHAKTI_USE_ACCELERATE=1
  endif
endif

# Always link synth.c (full UI on Linux+X11+ALSA; stubs elsewhere).
SHAKTI_SYNTH ?= 1
SHAKTI_GFX ?= 1
SHAKTI_SONICPI ?= 1
SHAKTI_DSP ?= 1
SHAKTI_STEM ?= 1
SHAKTI_PDF ?= 1
SHAKTI_MIDI ?= 1
SHAKTI_IEFS ?= 1

ifeq ($(SHAKTI_SYNTH),1)
  CFLAGS += -DSHAKTI_HAVE_SYNTH=1
endif

ifeq ($(SHAKTI_GFX),1)
  CFLAGS += -DSHAKTI_HAVE_GFX=1
endif

ifeq ($(SHAKTI_SONICPI),1)
  CFLAGS += -DSHAKTI_HAVE_SONICPI=1
endif

ifeq ($(SHAKTI_DSP),1)
  CFLAGS += -DSHAKTI_HAVE_DSP=1
endif

ifeq ($(SHAKTI_STEM),1)
  CFLAGS += -DSHAKTI_HAVE_STEM=1
endif

ifeq ($(SHAKTI_PDF),1)
  CFLAGS += -DSHAKTI_HAVE_PDF=1
endif

ifeq ($(SHAKTI_MIDI),1)
  CFLAGS += -DSHAKTI_HAVE_MIDI=1
endif

ifeq ($(SHAKTI_IEFS),1)
  CFLAGS += -DSHAKTI_HAVE_IEFS=1
  # IEFS v3 zstd extents (STAC Basic day shards). Default on when zstd.h is present.
  SHAKTI_WITH_ZSTD ?= 1
  ZSTD_HEADER := $(firstword $(wildcard /usr/include/zstd.h /opt/homebrew/include/zstd.h /usr/local/include/zstd.h))
  ifeq ($(SHAKTI_WITH_ZSTD),1)
    ifneq ($(ZSTD_HEADER),)
      CFLAGS += -DSHAKTI_HAVE_ZSTD=1
      ifneq ($(filter /opt/homebrew/% /usr/local/%,$(ZSTD_HEADER)),)
        CFLAGS += -I$(dir $(ZSTD_HEADER))
        LDFLAGS += -L$(dir $(ZSTD_HEADER))../lib -lzstd
      else
        LDFLAGS += -lzstd
      endif
    endif
  endif
endif

ifeq ($(UNAME_S),Linux)
  ifeq ($(SHAKTI_SYNTH),1)
    SYNTH_LDFLAGS := -lX11 -lpthread
    ifneq ($(wildcard /usr/include/alsa/asoundlib.h),)
      SYNTH_LDFLAGS += -lasound
    endif
  endif
  ifeq ($(SHAKTI_GFX),1)
    GFX_LDFLAGS := -lX11
  endif
  ifeq ($(SHAKTI_MIDI),1)
    MIDI_LDFLAGS := -lpthread
    ifneq ($(wildcard /usr/include/alsa/asoundlib.h),)
      MIDI_LDFLAGS += -lasound
    endif
  endif
endif

ifeq ($(UNAME_S),Darwin)
  ifeq ($(SHAKTI_SYNTH),1)
    SYNTH_LDFLAGS := -framework Cocoa -framework AudioToolbox -framework CoreAudio -framework CoreFoundation
    SYNTH_OBJC_FLAGS := -x objective-c -O2 -g -Wall -std=gnu11 -fobjc-arc -DSHAKTI_HAVE_SYNTH=1 -DSHAKTI_STANDALONE=1 \
	-I$(BUILD) -Isrc
    ifeq ($(SHAKTI_USE_ACCELERATE),1)
      SYNTH_OBJC_FLAGS += -DSHAKTI_USE_ACCELERATE=1
    endif
    OBJC ?= clang
  endif
  ifeq ($(SHAKTI_GFX),1)
    GFX_LDFLAGS := -framework Cocoa
    GFX_OBJC_FLAGS := -x objective-c -O2 -g -Wall -std=gnu11 -fobjc-arc -DSHAKTI_HAVE_GFX=1 -DSHAKTI_STANDALONE=1 \
	-I$(BUILD) -Isrc
    OBJC ?= clang
  endif
  ifeq ($(SHAKTI_MIDI),1)
    MIDI_LDFLAGS := -framework CoreMIDI -framework CoreFoundation
  endif
endif

$(BUILD)/shakti_version.h: src/VERSION
	@mkdir -p $(BUILD)
	@sed 's/.*/#define SHAKTI_PKG_VERSION "&"/' src/VERSION > $@

ifeq ($(SHAKTI_TALK),1)
$(BUILD)/talk.o: src/talk.c src/shakti.h src/a.h $(BUILD)/shakti_version.h | $(BUILD)
	$(OBJC) $(TALK_OBJC_FLAGS) -c -o $@ src/talk.c
endif

ifeq ($(SHAKTI_SYNTH),1)
$(BUILD)/synth.o: src/synth.c src/synth_ui.c src/fb_present.h src/shakti.h src/a.h $(BUILD)/shakti_version.h | $(BUILD)
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -c -o $@ src/synth.c

$(BUILD)/synth_ui.o: src/synth_ui.c src/synth_ui.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ src/synth_ui.c
endif

ifeq ($(SHAKTI_GFX),1)
$(BUILD)/gfx.o: src/gfx.c src/gfx.h src/gfx_platform.h src/fb_present.h src/shakti.h src/a.h $(BUILD)/shakti_version.h | $(BUILD)
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -c -o $@ src/gfx.c
endif

ifeq ($(SHAKTI_SONICPI),1)
$(BUILD)/sonicpi.o: src/sonicpi.c src/sonicpi.h src/shakti.h src/a.h $(BUILD)/shakti_version.h | $(BUILD)
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -c -o $@ src/sonicpi.c
endif

ifeq ($(SHAKTI_DSP),1)
$(BUILD)/dsp.o: src/dsp.c src/dsp.h src/shakti.h src/a.h $(BUILD)/shakti_version.h | $(BUILD)
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -c -o $@ src/dsp.c
endif

ifeq ($(SHAKTI_STEM),1)
$(BUILD)/stem.o: src/stem.c src/stem.h src/stem_stats.h src/shakti.h src/a.h $(BUILD)/shakti_version.h | $(BUILD)
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -c -o $@ src/stem.c

$(BUILD)/stem_stats.o: src/stem_stats.c src/stem_stats.h src/vec_kernels.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ src/stem_stats.c
endif

ifeq ($(SHAKTI_PDF),1)
$(BUILD)/pdf.o: src/pdf.c src/pdf.h src/shakti.h src/a.h $(BUILD)/shakti_version.h | $(BUILD)
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -c -o $@ src/pdf.c
endif

ifeq ($(SHAKTI_MIDI),1)
$(BUILD)/midi.o: src/midi.c src/midi.h src/shakti.h src/a.h $(BUILD)/shakti_version.h | $(BUILD)
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -c -o $@ src/midi.c
endif

ifeq ($(SHAKTI_IEFS),1)
$(BUILD)/iefs_io.o: src/iefs_io.c src/iefs_io.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ src/iefs_io.c

$(BUILD)/iefs_format.o: src/iefs_format.c src/iefs_format.h src/iefs_io.h src/iefs_map.h src/shakti.h src/a.h $(BUILD)/shakti_version.h | $(BUILD)
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -c -o $@ src/iefs_format.c

$(BUILD)/iefs_map.o: src/iefs_map.c src/iefs_map.h src/iefs_format.h src/shakti.h src/a.h $(BUILD)/shakti_version.h | $(BUILD)
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -c -o $@ src/iefs_map.c
endif

ifeq ($(UNAME_S),Darwin)
ifeq ($(SHAKTI_SYNTH),1)
$(BUILD)/synth_mac.o: src/synth_mac.m src/fb_present.h $(BUILD)/shakti_version.h | $(BUILD)
	$(OBJC) $(SYNTH_OBJC_FLAGS) -c -o $@ src/synth_mac.m
endif
ifeq ($(SHAKTI_GFX),1)
$(BUILD)/gfx_mac.o: src/gfx_mac.m src/fb_present.h $(BUILD)/shakti_version.h | $(BUILD)
	$(OBJC) $(GFX_OBJC_FLAGS) -c -o $@ src/gfx_mac.m
endif
endif

ifeq ($(UNAME_S),Linux)
ifeq ($(SHAKTI_GFX),1)
$(BUILD)/gfx_x11.o: src/gfx_x11.c src/gfx_platform.h src/gfx.h $(BUILD)/shakti_version.h | $(BUILD)
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -c -o $@ src/gfx_x11.c
endif
endif

SYNTH_MAC_OBJ := $(if $(and $(filter Darwin,$(UNAME_S)),$(filter 1,$(SHAKTI_SYNTH))),$(BUILD)/synth_mac.o)
GFX_MAC_OBJ := $(if $(and $(filter Darwin,$(UNAME_S)),$(filter 1,$(SHAKTI_GFX))),$(BUILD)/gfx_mac.o)
GFX_X11_OBJ := $(if $(and $(filter Linux,$(UNAME_S)),$(filter 1,$(SHAKTI_GFX))),$(BUILD)/gfx_x11.o)
SONICPI_OBJ := $(if $(filter 1,$(SHAKTI_SONICPI)),$(BUILD)/sonicpi.o)
DSP_OBJ := $(if $(filter 1,$(SHAKTI_DSP)),$(BUILD)/dsp.o)
STEM_OBJ := $(if $(filter 1,$(SHAKTI_STEM)),$(BUILD)/stem.o $(BUILD)/stem_stats.o)
PDF_OBJ := $(if $(filter 1,$(SHAKTI_PDF)),$(BUILD)/pdf.o)
MIDI_OBJ := $(if $(filter 1,$(SHAKTI_MIDI)),$(BUILD)/midi.o)
IEFS_OBJ := $(if $(filter 1,$(SHAKTI_IEFS)),$(BUILD)/iefs_io.o $(BUILD)/iefs_format.o $(BUILD)/iefs_map.o)

$(BUILD):
	@mkdir -p $@

all: build
build: shakti

# PATH-friendly name at repo root (Isolde-style: workspace dir on PATH finds ./shakti).
# The real binary stays under $(BUILD)/; ./shakti is a relative symlink.
shakti: $(SHAKTI)
	@if [ -e shakti ] && [ ! -L shakti ]; then \
	  if [ -d shakti ]; then \
	    echo "error: ./shakti is a directory (stale build tree). Run: rm -rf shakti/" >&2; exit 1; \
	  fi; \
	  echo "error: ./shakti exists and is not a symlink to $(SHAKTI). Remove it first." >&2; exit 1; \
	fi
	ln -sfn $(SHAKTI) shakti

$(SHAKTI): $(BUILD)/shakti_version.h src/a.h src/shakti.h src/shakti_internal.h $(LANG_STANDALONE) $(LIBSRCS_STANDALONE) $(if $(filter 1,$(SHAKTI_TALK)),$(BUILD)/talk.o) $(if $(filter 1,$(SHAKTI_SYNTH)),$(BUILD)/synth.o $(BUILD)/synth_ui.o) $(SYNTH_MAC_OBJ) $(if $(filter 1,$(SHAKTI_GFX)),$(BUILD)/gfx.o) $(GFX_MAC_OBJ) $(GFX_X11_OBJ) $(SONICPI_OBJ) $(DSP_OBJ) $(STEM_OBJ) $(PDF_OBJ) $(MIDI_OBJ) $(IEFS_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -o $@ $(LIBSRCS_STANDALONE) $(LANG_STANDALONE) $(if $(filter 1,$(SHAKTI_TALK)),$(BUILD)/talk.o) $(if $(filter 1,$(SHAKTI_SYNTH)),$(BUILD)/synth.o $(BUILD)/synth_ui.o) $(SYNTH_MAC_OBJ) $(if $(filter 1,$(SHAKTI_GFX)),$(BUILD)/gfx.o) $(GFX_MAC_OBJ) $(GFX_X11_OBJ) $(SONICPI_OBJ) $(DSP_OBJ) $(STEM_OBJ) $(PDF_OBJ) $(MIDI_OBJ) $(IEFS_OBJ) $(LDFLAGS) $(IPC_LDFLAGS) $(if $(filter 1,$(SHAKTI_TALK)),$(TALK_LDFLAGS)) $(if $(filter 1,$(SHAKTI_SYNTH)),$(SYNTH_LDFLAGS)) $(if $(filter 1,$(SHAKTI_GFX)),$(GFX_LDFLAGS)) $(if $(filter 1,$(SHAKTI_MIDI)),$(MIDI_LDFLAGS))

# Optional JNI object for Java/Android hosts (tests/build_guards.sh).
# Lives under $(BUILD)/ so `make test` does not drop a .o in the repo root.
$(BUILD)/shakti_jni.o: src/shakti_jni.c src/a.h | $(BUILD)
	$(CC) $(CFLAGS) $(JNI_CFLAGS) -DSHAKTI_STANDALONE=1 -c -o $@ src/shakti_jni.c
shakti_jni.o: $(BUILD)/shakti_jni.o

SHAKTI_LIB_DIR := lib
SHAKTI_TESTS := $(wildcard tests/*.ie)
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

.PHONY: all build test clean prod prod-size prod-speed clean-shakti-artifacts install uninstall shakti_jni.o

test: shakti
	@if [ -f qa/tests/assert_prec.sh ]; then \
	  SHAKTI=$(SHAKTI) bash qa/tests/assert_prec.sh || exit 1; \
	elif [ -f tests/assert_prec.sh ]; then \
	  SHAKTI=$(SHAKTI) bash tests/assert_prec.sh || exit 1; \
	else \
	  export SHAKTI_LIB=$$PWD/$(SHAKTI_LIB_DIR); \
	  fail=0; \
	  for src in 'assert 1 = 2' 'assert 1 < 0' 'assert 1 > 1' 'assert 1 != 1' 'assert 1 <= 0' 'assert 1 >= 2' 'assert type(1) = "str"' 'assert(1 = 2)'; do \
	    set +e; out=`$(SHAKTI) -c "$$src" 2>&1`; rc=$$?; set -e; \
	    if [ $$rc -eq 0 ]; then echo "FAIL: $$src — expected AssertionError, got exit 0" >&2; fail=1; \
	    elif ! printf '%s\n' "$$out" | grep -q AssertionError; then echo "FAIL: $$src — no AssertionError" >&2; fail=1; \
	    else echo "ok: $$src (AssertionError)"; fi; \
	  done; \
	  for src in 'assert 1 = 1' 'assert 0 < 1' 'assert type(1) = "int"' 'assert(1 = 1)'; do \
	    set +e; out=`$(SHAKTI) -c "$$src" 2>&1`; rc=$$?; set -e; \
	    if [ $$rc -ne 0 ]; then echo "FAIL: $$src — expected exit 0, got $$rc" >&2; echo "$$out" >&2; fail=1; \
	    else echo "ok: $$src"; fi; \
	  done; \
	  dump=`$(SHAKTI) --parse-dump -c 'assert 1 = 2' 2>/dev/null || true`; \
	  case "$$dump" in \
	    *'(call `assert (n8 '*) echo "ok: parse-dump assert 1 = 2 is a call of a comparison";; \
	    *) echo "FAIL: parse-dump unexpected AST: $$dump" >&2; fail=1;; \
	  esac; \
	  [ $$fail -eq 0 ] || exit 1; \
	fi
	@if [ -f qa/tests/stem_wav.sh ]; then \
	  SHAKTI=$(SHAKTI) bash qa/tests/stem_wav.sh || exit 1; \
	elif [ -f tests/stem_wav.sh ]; then \
	  SHAKTI=$(SHAKTI) bash tests/stem_wav.sh || exit 1; \
	else \
	  export SHAKTI_LIB=$$PWD/$(SHAKTI_LIB_DIR); \
	  fail=0; \
	  src=`printf '%s\n' 'import stem' 'ident : stem.si_sdr([1.0, 0.0, -1.0], [1.0, 0.0, -1.0])' 'assert(ident > 40.0)'`; \
	  set +e; out=`$(SHAKTI) -c "$$src" 2>&1`; rc=$$?; set -e; \
	  if [ $$rc -ne 0 ]; then echo "FAIL: stem.si_sdr identity" >&2; echo "$$out" >&2; fail=1; else echo "ok: stem.si_sdr identity"; fi; \
	  src=`printf '%s\n' 'import stem' 'src : [0.0, 0.5, -0.5, 0.25]' 'stem.write_wav("/tmp/shakti_stem_canary_f32.wav", src, 44100, "f32")' 'rf : stem.read_wav("/tmp/shakti_stem_canary_f32.wav")' 'assert(rf["n"] = 4)' 'd : rf["samples"][1] - 0.5' 'if d < 0:' '    d : -d' 'assert(d <= 1e-6)'`; \
	  set +e; out=`$(SHAKTI) -c "$$src" 2>&1`; rc=$$?; set -e; \
	  if [ $$rc -ne 0 ]; then echo "FAIL: stem f32 wav round-trip" >&2; echo "$$out" >&2; fail=1; else echo "ok: stem f32 wav round-trip"; fi; \
	  if [ -e /dev/full ]; then \
	    src=`printf '%s\n' 'import stem' 'print(stem.write_wav("/dev/full", [0.1, -0.1, 0.2], 44100, "pcm16"))'`; \
	    set +e; out=`$(SHAKTI) -c "$$src" 2>&1`; rc=$$?; set -e; \
	    if [ $$rc -ne 0 ]; then echo "FAIL: stem write /dev/full" >&2; echo "$$out" >&2; fail=1; \
	    elif ! printf '%s\n' "$$out" | grep -q Error; then echo "FAIL: stem write /dev/full — expected Error" >&2; echo "$$out" >&2; fail=1; \
	    else echo "ok: stem write /dev/full fails"; fi; \
	  fi; \
	  [ $$fail -eq 0 ] || exit 1; \
	fi
ifneq ($(SHAKTI_TESTS),)
	@for f in $(SHAKTI_TESTS); do \
	  echo "Running $$f..."; case "$$f" in \
	    *synth*|*mac_synth*) SHAKTI_SYNTH_HEADLESS=1 SHAKTI_LIB=$$PWD/$(SHAKTI_LIB_DIR) ./shakti "$$f" || exit 1 ;; \
	    *) SHAKTI_LIB=$$PWD/$(SHAKTI_LIB_DIR) ./shakti "$$f" || exit 1 ;; \
	  esac; \
	done
	@if [ -x tests/exe_realpath.sh ]; then bash tests/exe_realpath.sh || exit 1; fi
	@if [ -x tests/build_guards.sh ]; then bash tests/build_guards.sh || exit 1; fi
	@if [ -x tests/repl_q.sh ]; then bash tests/repl_q.sh || exit 1; fi
	@if [ -x tests/repl_d.sh ]; then bash tests/repl_d.sh || exit 1; fi
else
	@echo "test: no tests/*.ie present (ok)"
endif

clean:
	rm -f shakti shakti-standalone *.o *.tmp *.plist
	rm -rf $(BUILD) build/ shakti/ *.dSYM shakti.zip

PROD_RELEASE_CFLAGS := -fstack-protector-strong

# On Apple Silicon, `strip` mutates the Mach-O and invalidates the linker-signed
# ad-hoc code signature, so the kernel SIGKILLs the binary at launch
# ("Code Signature Invalid"). Re-sign ad-hoc after every strip. No-op elsewhere.
ifeq ($(UNAME_S),Darwin)
  MACOS_RESIGN := codesign --force --sign - $(SHAKTI)
else
  MACOS_RESIGN := :
endif

prod: shakti
	strip $(SHAKTI)
	$(MACOS_RESIGN)

PROD_SIZE_CFLAGS := $(filter-out -O2 -g,$(CFLAGS)) -Os -DNDEBUG -DSHAKTI_MINSIZE=1 $(PROD_RELEASE_CFLAGS)
PROD_SIZE_LDFLAGS := $(LDFLAGS)

prod-size: CFLAGS := $(PROD_SIZE_CFLAGS)
prod-size: LDFLAGS := $(PROD_SIZE_LDFLAGS)
prod-size: clean-shakti-artifacts shakti
	strip $(SHAKTI)
	$(MACOS_RESIGN)

SHAKTI_PORTABLE_CPU ?= 0
ifeq ($(SHAKTI_PORTABLE_CPU),1)
  ifeq ($(UNAME_M),arm64)
    # Floor for redistributable arm64 builds. Current Xcode clang rejects
    # -mcpu=apple-m5; bump when the toolchain adds it.
    PROD_SPEED_ARCH := -mcpu=apple-m4
  else
    PROD_SPEED_ARCH := -march=x86-64-v2 -mtune=generic
  endif
else
  ifeq ($(UNAME_M),arm64)
    # Host tuning (Apple Silicon M5 and peers) — use native until clang
    # exposes -mcpu=apple-m5.
    PROD_SPEED_ARCH := -mcpu=native
  else
    PROD_SPEED_ARCH := -march=x86-64-v2 -mtune=native
  endif
endif
PROD_SPEED_CFLAGS := $(filter-out -O2 -g,$(CFLAGS)) -O3 -DNDEBUG $(PROD_RELEASE_CFLAGS) $(PROD_SPEED_ARCH)
PROD_SPEED_LDFLAGS := $(LDFLAGS)
# ObjC units hardcode -O2 -g; mirror C prod-speed so talk/synth/gfx match.
ifneq ($(TALK_OBJC_FLAGS),)
  PROD_SPEED_TALK_OBJC_FLAGS := $(filter-out -O2 -g,$(TALK_OBJC_FLAGS)) -O3 -DNDEBUG $(PROD_RELEASE_CFLAGS) $(PROD_SPEED_ARCH)
endif
ifneq ($(SYNTH_OBJC_FLAGS),)
  PROD_SPEED_SYNTH_OBJC_FLAGS := $(filter-out -O2 -g,$(SYNTH_OBJC_FLAGS)) -O3 -DNDEBUG $(PROD_RELEASE_CFLAGS) $(PROD_SPEED_ARCH)
endif
ifneq ($(GFX_OBJC_FLAGS),)
  PROD_SPEED_GFX_OBJC_FLAGS := $(filter-out -O2 -g,$(GFX_OBJC_FLAGS)) -O3 -DNDEBUG $(PROD_RELEASE_CFLAGS) $(PROD_SPEED_ARCH)
endif

prod-speed: CFLAGS := $(PROD_SPEED_CFLAGS)
prod-speed: LDFLAGS := $(PROD_SPEED_LDFLAGS)
ifneq ($(PROD_SPEED_TALK_OBJC_FLAGS),)
prod-speed: TALK_OBJC_FLAGS := $(PROD_SPEED_TALK_OBJC_FLAGS)
endif
ifneq ($(PROD_SPEED_SYNTH_OBJC_FLAGS),)
prod-speed: SYNTH_OBJC_FLAGS := $(PROD_SPEED_SYNTH_OBJC_FLAGS)
endif
ifneq ($(PROD_SPEED_GFX_OBJC_FLAGS),)
prod-speed: GFX_OBJC_FLAGS := $(PROD_SPEED_GFX_OBJC_FLAGS)
endif
prod-speed: clean-shakti-artifacts shakti
	strip $(SHAKTI)
	$(MACOS_RESIGN)

clean-shakti-artifacts:
	rm -f shakti $(SHAKTI) $(BUILD)/talk.o $(BUILD)/synth.o $(BUILD)/synth_ui.o $(BUILD)/synth_mac.o $(BUILD)/gfx.o $(BUILD)/gfx_mac.o $(BUILD)/gfx_x11.o

# Install a stable PATH entry (symlink → this tree's .build/shakti).
install: shakti
	install -d "$(BINDIR)"
	ln -sfn "$(CURDIR)/$(SHAKTI)" "$(BINDIR)/shakti"
	@echo "installed $(BINDIR)/shakti -> $(CURDIR)/$(SHAKTI)"

uninstall:
	rm -f "$(BINDIR)/shakti"
