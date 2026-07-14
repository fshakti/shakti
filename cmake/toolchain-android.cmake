# Android NDK — use as -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-android.cmake
#
# Pass -DANDROID_NDK=/path/to/ndk (recommended) or set environment ANDROID_NDK.
#
#   cmake -S . -B build/android-arm64 \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-android.cmake \
#         -DANDROID_NDK=$HOME/Android/Sdk/ndk/26.1.10909125 \
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26

if(NOT ANDROID_NDK)
  if(DEFINED ENV{ANDROID_NDK} AND NOT "$ENV{ANDROID_NDK}" STREQUAL "")
    set(ANDROID_NDK "$ENV{ANDROID_NDK}")
  endif()
endif()
if(NOT ANDROID_NDK)
  message(FATAL_ERROR "toolchain-android.cmake: pass -DANDROID_NDK=... or set environment ANDROID_NDK.")
endif()

set(_shakti_ndk_tc "${ANDROID_NDK}/build/cmake/android.toolchain.cmake")
if(NOT EXISTS "${_shakti_ndk_tc}")
  message(FATAL_ERROR "android.toolchain.cmake not found at ${_shakti_ndk_tc} (ANDROID_NDK=${ANDROID_NDK})")
endif()

include("${_shakti_ndk_tc}")
