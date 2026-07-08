# GNU Make build: standalone CLI.
BUILD := .build
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

LANG_STANDALONE := src/shakti_lang.c src/builtin.c src/table_sql.c src/mat_simd.c src/vec_kernels.c
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

ifeq ($(SHAKTI_SYNTH),1)
  CFLAGS += -DSHAKTI_HAVE_SYNTH=1
endif

ifeq ($(UNAME_S),Linux)
  ifeq ($(SHAKTI_SYNTH),1)
    SYNTH_LDFLAGS := -lX11 -lpthread
    ifneq ($(wildcard /usr/include/alsa/asoundlib.h),)
      SYNTH_LDFLAGS += -lasound
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
endif

$(BUILD)/shakti_version.h: src/VERSION
	@mkdir -p $(BUILD)
	@sed 's/.*/#define SHAKTI_PKG_VERSION "&"/' src/VERSION > $@



ifeq ($(SHAKTI_TALK),1)
talk.o: src/talk.c src/shakti.h src/a.h $(BUILD)/shakti_version.h
	$(OBJC) $(TALK_OBJC_FLAGS) -c -o $@ src/talk.c
endif

ifeq ($(SHAKTI_SYNTH),1)
synth.o: src/synth.c src/synth_ui.c src/shakti.h src/a.h $(BUILD)/shakti_version.h
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -c -o $@ src/synth.c

synth_ui.o: src/synth_ui.c src/synth_ui.h
	$(CC) $(CFLAGS) -c -o $@ src/synth_ui.c
endif

ifeq ($(UNAME_S),Darwin)
ifeq ($(SHAKTI_SYNTH),1)
synth_mac.o: src/synth_mac.m $(BUILD)/shakti_version.h
	$(OBJC) $(SYNTH_OBJC_FLAGS) -c -o $@ src/synth_mac.m
endif
endif

SYNTH_MAC_OBJ := $(if $(and $(filter Darwin,$(UNAME_S)),$(filter 1,$(SHAKTI_SYNTH))),synth_mac.o)

shakti: $(BUILD)/shakti_version.h src/a.h $(LANG_STANDALONE) $(LIBSRCS_STANDALONE) $(if $(filter 1,$(SHAKTI_TALK)),talk.o) $(if $(filter 1,$(SHAKTI_SYNTH)),synth.o synth_ui.o) $(SYNTH_MAC_OBJ)
	@if [ -d shakti ] && [ ! -f shakti ]; then \
		echo "error: ./shakti is a directory (stale build tree). Run: rm -rf shakti/" >&2; exit 1; \
	fi
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -o $@ $(LIBSRCS_STANDALONE) $(LANG_STANDALONE) $(if $(filter 1,$(SHAKTI_TALK)),talk.o) $(if $(filter 1,$(SHAKTI_SYNTH)),synth.o synth_ui.o) $(SYNTH_MAC_OBJ) $(LDFLAGS) $(IPC_LDFLAGS) $(if $(filter 1,$(SHAKTI_TALK)),$(TALK_LDFLAGS)) $(if $(filter 1,$(SHAKTI_SYNTH)),$(SYNTH_LDFLAGS))

SHAKTI_LIB_DIR := lib
SHAKTI_TESTS := $(wildcard tests/*.ie)

ifneq ($(SHAKTI_TESTS),)
test: shakti
	@for f in $(SHAKTI_TESTS); do \
	  echo "Running $$f..."; case "$$f" in \
	    *synth*) SHAKTI_LIB=$$PWD/$(SHAKTI_LIB_DIR) SHAKTI_SYNTH_HEADLESS=1 ./shakti "$$f" || exit 1 ;; \
	    *) SHAKTI_LIB=$$PWD/$(SHAKTI_LIB_DIR) ./shakti "$$f" || exit 1 ;; \
	  esac; \
	done
endif

ifneq ($(wildcard scripts/bench_check.py),)
bench: prod
	SHAKTI_LIB=$$PWD/$(SHAKTI_LIB_DIR) python3 scripts/bench_check.py --check

bench-update: prod
	SHAKTI_LIB=$$PWD/$(SHAKTI_LIB_DIR) python3 scripts/bench_check.py --update

bench-report: shakti
	SHAKTI_LIB=$$PWD/$(SHAKTI_LIB_DIR) python3 scripts/bench_check.py --report
endif

ifneq ($(wildcard tests/macros_smoke.c),)
test-macros: src/a.h
	gcc $(CFLAGS) -I$(BUILD) -o $(BUILD)/macros_smoke tests/macros_smoke.c
	$(BUILD)/macros_smoke
endif

ifneq ($(wildcard scripts/parse_golden.sh),)
test-parse: shakti
	@bash scripts/parse_golden.sh
endif

ifeq ($(UNAME_S),Darwin)
test-mac: prod test test-parse
	@echo "test-mac: all macOS checks passed"
else
test-mac:
	@echo "test-mac: skipped (Darwin only)"
endif

clean:
	rm -f shakti shakti-standalone *.o talk.o synth.o synth_ui.o synth_mac.o *.tmp
	rm -f $(BUILD)/shakti_version.h $(BUILD)/macros_smoke
	rm -rf build/ shakti/ *.dSYM shakti.zip

PROD_RELEASE_CFLAGS := -fno-stack-protector

prod: shakti
	strip shakti

PROD_SIZE_CFLAGS := $(filter-out -O2 -g,$(CFLAGS)) -Os -DNDEBUG -DSHAKTI_MINSIZE=1 $(PROD_RELEASE_CFLAGS)
PROD_SIZE_LDFLAGS := $(LDFLAGS)

prod-size: CFLAGS := $(PROD_SIZE_CFLAGS)
prod-size: LDFLAGS := $(PROD_SIZE_LDFLAGS)
prod-size: clean-shakti-artifacts shakti
	strip shakti

SHAKTI_PORTABLE_CPU ?= 0
ifeq ($(SHAKTI_PORTABLE_CPU),1)
  ifeq ($(UNAME_M),arm64)
    PROD_SPEED_ARCH := -mcpu=apple-m1
  else
    PROD_SPEED_ARCH := -march=x86-64-v2 -mtune=generic
  endif
else
  ifeq ($(UNAME_M),arm64)
    PROD_SPEED_ARCH := -mcpu=native
  else
    PROD_SPEED_ARCH := -march=native
  endif
endif
PROD_SPEED_CFLAGS := $(filter-out -O2 -g,$(CFLAGS)) -O3 -DNDEBUG $(PROD_RELEASE_CFLAGS) $(PROD_SPEED_ARCH)
PROD_SPEED_LDFLAGS := $(LDFLAGS)

prod-speed: CFLAGS := $(PROD_SPEED_CFLAGS)
prod-speed: LDFLAGS := $(PROD_SPEED_LDFLAGS)
prod-speed: clean-shakti-artifacts shakti
	strip shakti

clean-shakti-artifacts:
	rm -f shakti talk.o synth.o synth_mac.o

ifneq ($(wildcard scripts/size_check.py),)
size-check: prod
	SHAKTI_SYNTH=1 SHAKTI_TALK=0 python3 scripts/size_check.py --check

size-update: prod
	SHAKTI_SYNTH=1 SHAKTI_TALK=0 python3 scripts/size_check.py --update

size-report: prod
	SHAKTI_SYNTH=1 SHAKTI_TALK=0 python3 scripts/size_check.py --report
endif

check-deps:
ifeq ($(UNAME_S),Darwin)
	@missing=; \
	if [ ! -f /opt/homebrew/opt/libomp/include/omp.h ] && [ ! -f /usr/local/opt/libomp/include/omp.h ]; then \
	  missing="$$missing libomp"; \
	fi; \
	if ! command -v brew >/dev/null 2>&1 || ! brew list expat >/dev/null 2>&1; then \
	  missing="$$missing expat"; \
	fi; \
	if [ -n "$$missing" ]; then \
	  echo "Missing Homebrew packages:$$missing"; \
	  echo "Install with: brew install$$missing"; \
	  exit 1; \
	fi; \
	echo "macOS dependencies OK"
else
	@echo "check-deps: no-op on $(UNAME_S)"
endif

.PHONY: test test-macros test-parse test-mac bench bench-update bench-report clean prod prod-size prod-speed clean-shakti-artifacts shakti size-check size-update size-report check-deps
