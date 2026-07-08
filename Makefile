# GNU Make build: standalone CLI.
BUILD := .build
UNAME_S := $(shell uname -s 2>/dev/null || echo unknown)
UNAME_M := $(shell uname -m 2>/dev/null || echo unknown)

ifeq ($(UNAME_S),Darwin)
  CC ?= clang
  OBJC ?= clang
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

CFLAGS := -Ofast -O2 -Os -g3 -Wall -Wextra -Wno-misleading-indentation -Wno-sign-compare -Wno-unused-result -Wno-format-truncation -Wno-alloc-size-larger-than -Wno-missing-field-initializers -std=gnu11 -D_GNU_SOURCE \
	-I$(BUILD) -Isrc $(OMP_CFLAGS) $(CFLAGS)

LDFLAGS := -lm $(OMP_LDFLAGS)
ifneq ($(filter Linux,$(UNAME_S)),)
  LDFLAGS += -lrt -ldl -Wl,-export-dynamic
endif

ifeq ($(filter Linux Darwin,$(UNAME_S)),)
else
  CFLAGS += -DSHAKTI_HAVE_LIBEXPAT=1
  LDFLAGS += -lexpat
endif

LANG_STANDALONE := src/shakti_lang.c src/builtin.c src/table_sql.c src/mat_simd.c src/vec_kernels.c
LIBSRCS_STANDALONE := src/methods.c src/stdlib.c src/json_parse.c src/table_xml.c src/cli_main.c src/input.c src/graph.c src/machine.c src/subprocess.c

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

shakti: $(BUILD)/shakti_version.h src/a.h $(LANG_STANDALONE) $(LIBSRCS_STANDALONE)
	@if [ -d shakti ] && [ ! -f shakti ]; then \
		echo "error: ./shakti is a directory (stale build tree). Run: rm -rf shakti/" >&2; exit 1; \
	fi
	$(CC) $(CFLAGS) -DSHAKTI_STANDALONE=1 -o $@ $(LIBSRCS_STANDALONE) $(LANG_STANDALONE) $(LDFLAGS) $(IPC_LDFLAGS)

$(BUILD)/shakti_version.h: src/VERSION
	@mkdir -p $(BUILD)
	@sed 's/.*/#define SHAKTI_PKG_VERSION "&"/' src/VERSION > $@

clean:
	rm -f shakti
	rm -f $(BUILD)/shakti_version.h
	rm -rf build/ shakti/ *.dSYM shakti.zip

PROD_RELEASE_CFLAGS := -fno-stack-protector

prod: $(BUILD)/shakti_version.h
	$(MAKE) prod-size SHAKTI_PORTABLE_CPU=1

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
	rm -f shakti

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

.PHONY: test test-macros test-parse test-mac bench bench-update bench-report clean prod prod-size prod-speed clean-shakti-artifacts size-check size-update size-report check-deps update-whitelist-h
update-whitelist-h:;find . -name '*.ie' -exec ./shakti --hash {} \; | grep '^{' | sort -n -k 2 > src/whitelist.h
