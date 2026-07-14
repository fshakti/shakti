cmake_minimum_required(VERSION 3.16)

# Fail fast on Windows when CMake picks NMake/MSVC without a toolchain (common from PowerShell).
if(CMAKE_HOST_WIN32 AND NOT CMAKE_TOOLCHAIN_FILE AND NOT DEFINED ENV{VCINSTALLDIR})
  if(NOT CMAKE_GENERATOR MATCHES "Ninja|MinGW|Unix|Visual Studio")
    message(FATAL_ERROR
      "Shakti: no C compiler configured for generator '${CMAKE_GENERATOR}'.\n"
      "From PowerShell on Windows, use the MinGW preset (not plain cmake -S . -B build):\n"
      "  cmake --preset windows-mingw-release\n"
      "  cmake --build --preset windows-mingw-release --target shakti_standalone\n"
      "Install MSYS2 UCRT64 or MinGW64 gcc+ninja first, or run scripts/build-standalone-windows.ps1")
  endif()
  if(CMAKE_GENERATOR MATCHES "NMake")
    message(FATAL_ERROR
      "Shakti: NMake requires Visual Studio Build Tools on PATH.\n"
      "For open-source builds use MinGW instead:\n"
      "  cmake --preset windows-mingw-release\n"
      "See shakti/README.md (Windows section).")
  endif()
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/VERSION" _shakti_version_file)
string(STRIP "${_shakti_version_file}" SHAKTI_PKG_VERSION)
project(shakti VERSION "${SHAKTI_PKG_VERSION}" LANGUAGES C CXX)

# Standalone runtime. On Android we build libshakti.so + JNI; elsewhere the `shakti` executable.

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(SHAKTI_EMSCRIPTEN TRUE)
else()
  set(SHAKTI_EMSCRIPTEN FALSE)
endif()

if(ANDROID)
  set(SHAKTI_ANDROID_BUILD TRUE)
else()
  set(SHAKTI_ANDROID_BUILD FALSE)
endif()

# MSYS2 /usr/bin gcc is x86_64-pc-cygwin; driving it from PowerShell breaks cc1 DLL loading.
if(CMAKE_HOST_WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
  string(TOLOWER "${CMAKE_C_COMPILER}" _shakti_cc)
  if(_shakti_cc MATCHES "/usr/bin/gcc" OR _shakti_cc MATCHES "\\\\usr\\\\bin\\\\gcc"
      OR _shakti_cc MATCHES "cygwin")
    message(FATAL_ERROR
      "Shakti: CMake chose MSYS/Cygwin GCC (${CMAKE_C_COMPILER}).\n"
      "That compiler does not work when launched from PowerShell/CMD.\n"
      "Use MinGW:  cmake --preset windows-mingw-release\n"
      "Or:  powershell -File scripts/build-standalone-windows.ps1")
  endif()
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/shakti_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/shakti_version.h"
    @ONLY)

option(SHAKTI_STANDALONE_MINIMAL_LIBS "Link without OpenMP, Expat, librt (Linux), or Apple GPU/OpenMP frameworks" ON)
option(SHAKTI_STANDALONE_STATIC "Pass -static when linking (not used with Emscripten)" OFF)

set(SHAKTI_LANG_CORE
    src/shakti_lang.c
    src/builtin.c
    src/table_sql.c
    src/mat_simd.c
    src/vec_kernels.c)

set(SHAKTI_LIB_STANDALONE
    src/methods.c
    src/stdlib.c
    src/json_parse.c
    src/table_io.c
    src/table_xml.c
    src/input.c
    src/isolde_bridge.c
    src/lissen.c
    src/govee.c
    src/rest.c
    src/graph.c
    src/machine.c)

if(NOT SHAKTI_EMSCRIPTEN AND NOT SHAKTI_ANDROID_BUILD)
  option(SHAKTI_IPC "Build IPC module (TCP/UDS/SHM, optional RDMA)" ON)
else()
  set(SHAKTI_IPC OFF)
endif()

if(SHAKTI_IPC)
  list(APPEND SHAKTI_LIB_STANDALONE src/ipc.c)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckIncludeFile)
    check_include_file(infiniband/verbs.h SHAKTI_HAVE_INFINIBAND_VERBS)
    check_include_file(rdma/rdma_cma.h SHAKTI_HAVE_RDMA_CMA)
    if(SHAKTI_HAVE_INFINIBAND_VERBS AND SHAKTI_HAVE_RDMA_CMA)
      set(SHAKTI_HAVE_RDMA_BUILD TRUE)
      list(APPEND SHAKTI_LIB_STANDALONE src/ipc_rdma.c)
    endif()
  endif()
endif()

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "tvOS"
   AND NOT CMAKE_SYSTEM_NAME STREQUAL "watchOS" AND NOT SHAKTI_EMSCRIPTEN AND NOT SHAKTI_ANDROID_BUILD)
  option(SHAKTI_TALK "Build talk module (homemade C STT in talk.c, macOS desktop)" ON)
else()
  option(SHAKTI_TALK "Build talk module (homemade C STT in talk.c)" OFF)
endif()

if(SHAKTI_TALK)
  list(APPEND SHAKTI_LIB_STANDALONE src/talk.c)
endif()

if(NOT SHAKTI_EMSCRIPTEN AND NOT SHAKTI_ANDROID_BUILD)
  option(SHAKTI_SYNTH "Build synth module (Linux X11/ALSA + macOS Cocoa/Core Audio)" ON)
else()
  option(SHAKTI_SYNTH "Build synth module (homemade C synth in synth.c)" OFF)
endif()

if(SHAKTI_SYNTH)
  list(APPEND SHAKTI_LIB_STANDALONE src/synth.c src/synth_ui.c)
  if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "tvOS"
     AND NOT CMAKE_SYSTEM_NAME STREQUAL "watchOS")
    list(APPEND SHAKTI_LIB_STANDALONE src/synth_mac.m)
  endif()
endif()

set(SHAKTI_RUNTIME_SOURCES
    ${SHAKTI_LANG_CORE}
    ${SHAKTI_LIB_STANDALONE})

if(SHAKTI_ANDROID_BUILD)
  add_library(shakti_jni SHARED
      src/shakti_jni.c
      ${SHAKTI_RUNTIME_SOURCES})
  set(_shakti_targets shakti_jni)
  set_target_properties(shakti_jni PROPERTIES OUTPUT_NAME "shakti")
else()
  add_executable(shakti_standalone
      src/cli_main.c
      ${SHAKTI_RUNTIME_SOURCES})
  set(_shakti_targets shakti_standalone)
  set_target_properties(shakti_standalone PROPERTIES OUTPUT_NAME "shakti")
endif()

foreach(_t IN LISTS _shakti_targets)
  target_compile_definitions(${_t} PRIVATE SHAKTI_STANDALONE=1 _GNU_SOURCE
      SHAKTI_HAVE_LISSEN=1 SHAKTI_HAVE_GOVEE=1)
  target_include_directories(${_t} PRIVATE
      "${CMAKE_CURRENT_BINARY_DIR}"
      "${CMAKE_CURRENT_SOURCE_DIR}/src")
  if(SHAKTI_EMSCRIPTEN)
    target_compile_definitions(${_t} PRIVATE SHAKTI_WASM=1)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "tvOS" OR CMAKE_SYSTEM_NAME STREQUAL "watchOS")
    target_compile_definitions(${_t} PRIVATE SHAKTI_IOS=1)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_compile_definitions(${_t} PRIVATE SHAKTI_ANDROID=1)
    target_link_libraries(${_t} PRIVATE log)
  endif()
endforeach()

if(SHAKTI_IPC)
  foreach(_t IN LISTS _shakti_targets)
    target_compile_definitions(${_t} PRIVATE SHAKTI_HAVE_IPC=1)
  endforeach()
endif()

if(SHAKTI_HAVE_RDMA_BUILD)
  foreach(_t IN LISTS _shakti_targets)
    target_compile_definitions(${_t} PRIVATE SHAKTI_HAVE_RDMA=1)
    target_link_libraries(${_t} PRIVATE rdmacm ibverbs)
  endforeach()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT SHAKTI_ANDROID_BUILD)
  foreach(_t IN LISTS _shakti_targets)
    target_link_libraries(${_t} PRIVATE dl)
  endforeach()
endif()

if(NOT SHAKTI_EMSCRIPTEN)
    find_package(OpenMP)
endif()

if(OpenMP_C_FOUND AND NOT SHAKTI_EMSCRIPTEN)
    if(NOT SHAKTI_STANDALONE_MINIMAL_LIBS)
        foreach(_t IN LISTS _shakti_targets)
          target_link_libraries(${_t} PRIVATE OpenMP::OpenMP_C)
        endforeach()
    endif()
elseif(NOT APPLE AND NOT SHAKTI_EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        if(NOT SHAKTI_STANDALONE_MINIMAL_LIBS)
            foreach(_t IN LISTS _shakti_targets)
              target_compile_options(${_t} PRIVATE $<$<COMPILE_LANGUAGE:C>:-fopenmp>)
            endforeach()
        endif()
    endif()
endif()

find_package(EXPAT)
if(EXPAT_FOUND)
    if(NOT SHAKTI_STANDALONE_MINIMAL_LIBS)
        foreach(_t IN LISTS _shakti_targets)
          target_compile_definitions(${_t} PRIVATE SHAKTI_HAVE_LIBEXPAT=1)
          target_link_libraries(${_t} PRIVATE EXPAT::EXPAT)
        endforeach()
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(NOT SHAKTI_STANDALONE_MINIMAL_LIBS)
        foreach(_t IN LISTS _shakti_targets)
          target_link_libraries(${_t} PRIVATE rt)
        endforeach()
    endif()
endif()

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "tvOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "watchOS")
    set(LIBOMP_ROOT "/opt/homebrew/opt/libomp" CACHE PATH "Homebrew libomp prefix")
    if(EXISTS "${LIBOMP_ROOT}/include/omp.h")
        if(NOT SHAKTI_STANDALONE_MINIMAL_LIBS)
            foreach(_t IN LISTS _shakti_targets)
              target_compile_options(${_t} PRIVATE -Xpreprocessor -fopenmp "-I${LIBOMP_ROOT}/include")
              target_link_directories(${_t} PRIVATE "${LIBOMP_ROOT}/lib")
              target_link_libraries(${_t} PRIVATE omp)
            endforeach()
        endif()
    endif()
    if(NOT SHAKTI_STANDALONE_MINIMAL_LIBS)
        find_library(METAL_FRAMEWORK_STANDALONE Metal)
        find_library(MPS_FRAMEWORK_STANDALONE MetalPerformanceShaders)
        find_library(ACCEL_FRAMEWORK_STANDALONE Accelerate)
        foreach(_t IN LISTS _shakti_targets)
          target_link_libraries(${_t} PRIVATE ${METAL_FRAMEWORK_STANDALONE} ${MPS_FRAMEWORK_STANDALONE} ${ACCEL_FRAMEWORK_STANDALONE} c++)
        endforeach()
    endif()
    if(SHAKTI_TALK)
        find_library(TALK_AVFOUNDATION AVFoundation REQUIRED)
        find_library(TALK_AUDIO_TOOLBOX AudioToolbox REQUIRED)
        find_library(TALK_FOUNDATION Foundation REQUIRED)
        find_library(TALK_SPEECH Speech REQUIRED)
        foreach(_t IN LISTS _shakti_targets)
          target_compile_definitions(${_t} PRIVATE SHAKTI_HAVE_TALK=1)
          set_source_files_properties(src/talk.c PROPERTIES LANGUAGE OBJC COMPILE_FLAGS "-fobjc-arc")
          target_link_libraries(${_t} PRIVATE
              ${TALK_AVFOUNDATION} ${TALK_AUDIO_TOOLBOX} ${TALK_FOUNDATION} ${TALK_SPEECH})
        endforeach()
    endif()
endif()

if(SHAKTI_SYNTH)
    foreach(_t IN LISTS _shakti_targets)
      target_compile_definitions(${_t} PRIVATE SHAKTI_HAVE_SYNTH=1)
    endforeach()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      find_package(X11 REQUIRED)
      find_library(SYNTH_ASOUND asound)
      foreach(_t IN LISTS _shakti_targets)
        target_link_libraries(${_t} PRIVATE X11::X11 pthread)
        if(SYNTH_ASOUND)
          target_link_libraries(${_t} PRIVATE ${SYNTH_ASOUND})
        endif()
      endforeach()
    elseif(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "tvOS"
           AND NOT CMAKE_SYSTEM_NAME STREQUAL "watchOS")
      find_library(SYNTH_COCOA Cocoa REQUIRED)
      find_library(SYNTH_AUDIO_TOOLBOX AudioToolbox REQUIRED)
      find_library(SYNTH_CORE_AUDIO CoreAudio REQUIRED)
      find_library(SYNTH_CORE_FOUNDATION CoreFoundation REQUIRED)
      foreach(_t IN LISTS _shakti_targets)
        set_source_files_properties(src/synth_mac.m PROPERTIES LANGUAGE OBJC COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(${_t} PRIVATE
            ${SYNTH_COCOA} ${SYNTH_AUDIO_TOOLBOX} ${SYNTH_CORE_AUDIO} ${SYNTH_CORE_FOUNDATION})
      endforeach()
    endif()
endif()

if(UNIX OR SHAKTI_EMSCRIPTEN OR SHAKTI_ANDROID_BUILD)
  foreach(_t IN LISTS _shakti_targets)
    target_link_libraries(${_t} PRIVATE m)
  endforeach()
endif()

if(SHAKTI_STANDALONE_STATIC AND NOT SHAKTI_EMSCRIPTEN AND NOT SHAKTI_ANDROID_BUILD)
    target_link_options(shakti_standalone PRIVATE -static)
endif()

if(SHAKTI_EMSCRIPTEN)
    foreach(_t IN LISTS _shakti_targets)
      target_link_options(${_t} PRIVATE
          "SHELL:-s ALLOW_MEMORY_GROWTH=1"
          "SHELL:-s STACK_SIZE=8388608"
          "SHELL:-s ENVIRONMENT=node,web,shell,worker"
          "SHELL:-s NODERAWFS=1"
      )
    endforeach()
endif()

enable_testing()
file(GLOB SHAKTI_TEST_SCRIPTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.ie")
if(NOT SHAKTI_EMSCRIPTEN AND NOT SHAKTI_ANDROID_BUILD)
    foreach(_shakti_script ${SHAKTI_TEST_SCRIPTS})
        get_filename_component(_shakti_test_name ${_shakti_script} NAME_WE)
        add_test(NAME shakti_standalone_${_shakti_test_name}
            COMMAND $<TARGET_FILE:shakti_standalone> "${_shakti_script}")
        set_tests_properties(shakti_standalone_${_shakti_test_name} PROPERTIES LABELS "standalone")
    endforeach()
elseif(SHAKTI_EMSCRIPTEN)
    if(DEFINED ENV{EMSDK_NODE} AND NOT "$ENV{EMSDK_NODE}" STREQUAL "")
        set(SHAKTI_NODE_EXECUTABLE "$ENV{EMSDK_NODE}")
    else()
        find_program(SHAKTI_NODE_EXECUTABLE NAMES node)
    endif()
    if(SHAKTI_NODE_EXECUTABLE)
        foreach(_shakti_script ${SHAKTI_TEST_SCRIPTS})
            get_filename_component(_shakti_test_name ${_shakti_script} NAME_WE)
            add_test(NAME shakti_standalone_${_shakti_test_name}
                COMMAND "${SHAKTI_NODE_EXECUTABLE}" "$<TARGET_FILE:shakti_standalone>" "${_shakti_script}")
            set_tests_properties(shakti_standalone_${_shakti_test_name} PROPERTIES LABELS "standalone")
        endforeach()
    endif()
endif()

find_program(SHAKTI_PYTHON3 NAMES python3)
if(SHAKTI_PYTHON3)
    add_custom_target(bench
        COMMAND ${CMAKE_COMMAND} -E env
            SHAKTI_LIB=${CMAKE_CURRENT_SOURCE_DIR}/src/lib
            SHAKTI=$<TARGET_FILE:shakti_standalone>
            ${SHAKTI_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/bench_check.py --check
        DEPENDS shakti_standalone
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Run Shakti benchmark regression check"
    )
    add_custom_target(bench-update
        COMMAND ${CMAKE_COMMAND} -E env
            SHAKTI_LIB=${CMAKE_CURRENT_SOURCE_DIR}/src/lib
            SHAKTI=$<TARGET_FILE:shakti_standalone>
            ${SHAKTI_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/bench_check.py --update
        DEPENDS shakti_standalone
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Update local Shakti benchmark baseline"
    )
endif()
