# Emscripten (WASM) helper — loaded via -DCMAKE_TOOLCHAIN_FILE=...
#
# Prerequisite: install/activate the Emscripten SDK (https://emscripten.org)
# and set EMSDK to the SDK root, e.g. after `source emsdk_env.sh`.
#
#   export EMSDK=$HOME/emsdk
#   cmake -S . -B build/wasm -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-emscripten.cmake

if(NOT DEFINED ENV{EMSDK} OR "$ENV{EMSDK}" STREQUAL "")
  message(FATAL_ERROR "Set EMSDK to the Emscripten SDK root (same as emsdk_env.sh). Example: export EMSDK=$HOME/emsdk")
endif()

set(_shakti_emsdk_toolchain "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
if(NOT EXISTS "${_shakti_emsdk_toolchain}")
  message(FATAL_ERROR "Emscripten toolchain file not found: ${_shakti_emsdk_toolchain}. Check EMSDK and Emscripten install.")
endif()

include("${_shakti_emsdk_toolchain}")
