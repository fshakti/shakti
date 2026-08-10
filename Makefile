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
	-I$(BUILD) -Igen -Isrc \
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

LANG_STANDALONE := src/shakti_lang.c src/builtin.c src/table_sql.c src/mat_simd.c src/vec_kernels.c src/fb_present.c
LIBSRCS_STANDALONE := src/methods.c src/stdlib.c src/json_parse.c src/table_io.c src/table_xml.c src/cli_main.c src/input.c src/isolde_bridge.c src/rest.c src/graph.c src/machine.c src/pcm.c

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

# Regenerated from converters/ when scripts/embed_text.py is available.
# src/shakti_lang.c includes these files via ../gen/... explicitly so stale
# src/shakti_*_embed.h copies cannot shadow them.
define make_embed_rule
gen/shakti_$(1)_embed.h: converters/$(1).ie
	@mkdir -p gen
ifneq ($(wildcard scripts/embed_text.py),)
	python3 scripts/embed_text.py converters/$(1).ie shakti_$(1)_source $$@
else
	@test -f $$@ || (echo "error: missing $$@ — restore scripts/embed_text.py to regenerate from converters/$(1).ie" >&2; exit 1)
endif
endef

$(foreach stem,p2s c2s cs2s j2s,$(eval $(call make_embed_rule,$(stem))))

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
$(BUILD)/stem.o: src/stem.c src/stem.h src/shakti.h src/a.h $(BUILD)/shakti_version.h | $(BUILD)
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -c -o $@ src/stem.c
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
STEM_OBJ := $(if $(filter 1,$(SHAKTI_STEM)),$(BUILD)/stem.o)
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

$(SHAKTI): $(BUILD)/shakti_version.h gen/shakti_p2s_embed.h gen/shakti_c2s_embed.h gen/shakti_cs2s_embed.h gen/shakti_j2s_embed.h src/a.h $(LANG_STANDALONE) $(LIBSRCS_STANDALONE) $(if $(filter 1,$(SHAKTI_TALK)),$(BUILD)/talk.o) $(if $(filter 1,$(SHAKTI_SYNTH)),$(BUILD)/synth.o $(BUILD)/synth_ui.o) $(SYNTH_MAC_OBJ) $(if $(filter 1,$(SHAKTI_GFX)),$(BUILD)/gfx.o) $(GFX_MAC_OBJ) $(GFX_X11_OBJ) $(SONICPI_OBJ) $(DSP_OBJ) $(STEM_OBJ) $(PDF_OBJ) $(MIDI_OBJ) $(IEFS_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -o $@ $(LIBSRCS_STANDALONE) $(LANG_STANDALONE) $(if $(filter 1,$(SHAKTI_TALK)),$(BUILD)/talk.o) $(if $(filter 1,$(SHAKTI_SYNTH)),$(BUILD)/synth.o $(BUILD)/synth_ui.o) $(SYNTH_MAC_OBJ) $(if $(filter 1,$(SHAKTI_GFX)),$(BUILD)/gfx.o) $(GFX_MAC_OBJ) $(GFX_X11_OBJ) $(SONICPI_OBJ) $(DSP_OBJ) $(STEM_OBJ) $(PDF_OBJ) $(MIDI_OBJ) $(IEFS_OBJ) $(LDFLAGS) $(IPC_LDFLAGS) $(if $(filter 1,$(SHAKTI_TALK)),$(TALK_LDFLAGS)) $(if $(filter 1,$(SHAKTI_SYNTH)),$(SYNTH_LDFLAGS)) $(if $(filter 1,$(SHAKTI_GFX)),$(GFX_LDFLAGS)) $(if $(filter 1,$(SHAKTI_MIDI)),$(MIDI_LDFLAGS))

SHAKTI_LIB_DIR := lib
SHAKTI_TESTS := $(wildcard tests/*.ie)
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

.PHONY: all build test clean prod prod-size prod-speed clean-shakti-artifacts install uninstall

test: shakti
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
