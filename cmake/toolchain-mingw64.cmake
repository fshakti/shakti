# MSYS2 MinGW (UCRT64 or MINGW64) — native Windows GCC.
# Do not use the "msys" environment GCC from .../msys64/usr/bin (x86_64-pc-cygwin):
# it breaks when Ninja/Make is driven from PowerShell (cc1 fails to load DLLs).
#
# Override: cmake -DSHAKTI_MINGW_ROOT=C:/msys64/ucrt64 ...

cmake_minimum_required(VERSION 3.16)

set(CMAKE_SYSTEM_NAME Windows)

set(SHAKTI_MINGW_ROOT "" CACHE PATH
  "Directory containing bin/gcc.exe (e.g. C:/msys64/ucrt64). Leave empty to auto-detect.")

set(_SHAKTI_GCC "")
set(_SHAKTI_GXX "")

if(SHAKTI_MINGW_ROOT AND EXISTS "${SHAKTI_MINGW_ROOT}/bin/gcc.exe")
  set(_SHAKTI_GCC "${SHAKTI_MINGW_ROOT}/bin/gcc.exe")
  set(_SHAKTI_GXX "${SHAKTI_MINGW_ROOT}/bin/g++.exe")
endif()

if(_SHAKTI_GCC STREQUAL "" AND DEFINED ENV{MSYSTEM_PREFIX} AND NOT "$ENV{MSYSTEM_PREFIX}" STREQUAL "")
  set(_cand "$ENV{MSYSTEM_PREFIX}/bin/gcc.exe")
  if(EXISTS "${_cand}")
    set(_SHAKTI_GCC "${_cand}")
    set(_SHAKTI_GXX "$ENV{MSYSTEM_PREFIX}/bin/g++.exe")
  endif()
endif()

if(_SHAKTI_GCC STREQUAL "")
  foreach(_root IN ITEMS
      "C:/msys64/ucrt64"
      "C:/msys32/ucrt64"
      "C:/msys64/mingw64"
      "C:/tools/msys64/ucrt64"
      "D:/msys64/ucrt64")
    if(EXISTS "${_root}/bin/gcc.exe")
      set(_SHAKTI_GCC "${_root}/bin/gcc.exe")
      set(_SHAKTI_GXX "${_root}/bin/g++.exe")
      break()
    endif()
  endforeach()
endif()

if(_SHAKTI_GCC STREQUAL "" OR NOT EXISTS "${_SHAKTI_GCC}")
  message(FATAL_ERROR
    "Shakti: No MinGW gcc found (need ucrt64 or mingw64, not msys /usr).\n"
    "  Install: https://www.msys2.org/ then in UCRT64 shell:\n"
    "    pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-ninja\n"
    "  Configure with:\n"
    "    cmake --preset windows-mingw-release\n"
    "  Or set -DSHAKTI_MINGW_ROOT=C:/msys64/ucrt64")
endif()

set(CMAKE_C_COMPILER "${_SHAKTI_GCC}" CACHE FILEPATH "MinGW C compiler" FORCE)
set(CMAKE_CXX_COMPILER "${_SHAKTI_GXX}" CACHE FILEPATH "MinGW C++ compiler" FORCE)
