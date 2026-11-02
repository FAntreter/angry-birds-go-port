cmake_minimum_required(VERSION 3.22)
project(AngryBirdsGoARM64Bridge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT ANDROID)
  message(FATAL_ERROR "This bridge must be configured with the Android NDK toolchain.")
endif()
if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "Target ABI must be arm64-v8a; got '${ANDROID_ABI}'.")
endif()

set(DYNARMIC_SRC "" CACHE PATH "Path to a Dynarmic source checkout")
if(NOT DYNARMIC_SRC OR NOT EXISTS "${DYNARMIC_SRC}/CMakeLists.txt")
  message(FATAL_ERROR "Set -DDYNARMIC_SRC=/path/to/dynarmic (A32 frontend required).")
endif()

# Boost is header-only here. Expose host Boost headers to the Android
# cross-compiler after the NDK sysroot, for Dynarmic and bridge sources.
add_compile_options(-idirafter /usr/include)

set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_FRONTENDS "A32" CACHE STRING "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory("${DYNARMIC_SRC}" "${CMAKE_CURRENT_BINARY_DIR}/dynarmic" EXCLUDE_FROM_ALL)

add_library(AngryBirdsGoBridge SHARED
  bridge_src/arena.cpp
  bridge_src/bridge.cpp
  bridge_src/cpu.cpp
  bridge_src/display.cpp
  bridge_src/elf32.cpp
  bridge_src/gheap.c
  bridge_src/hle.cpp
  bridge_src/hle_android.cpp
  bridge_src/hle_egl.cpp
  bridge_src/hle_opensles.cpp
  bridge_src/hle_gles.cpp
  bridge_src/hle_libc.cpp
  bridge_src/hle_pthread.cpp
  bridge_src/hle_stdio.cpp
  bridge_src/jni_bridge.cpp
)
target_include_directories(AngryBirdsGoBridge PRIVATE bridge_src)
target_compile_definitions(AngryBirdsGoBridge PRIVATE ABSW_TRACE=0)
target_compile_options(AngryBirdsGoBridge PRIVATE -fvisibility=hidden $<$<COMPILE_LANGUAGE:C>:-w>)
target_link_libraries(AngryBirdsGoBridge PRIVATE dynarmic GLESv2 EGL android log OpenSLES dl)
target_link_options(AngryBirdsGoBridge PRIVATE -Wl,--exclude-libs,ALL -Wl,--gc-sections)
# Java loads the engine using the manifest-selected name "ABK". The ARM64
# bridge occupies that name; original ARM32 DSOs are kept as *_arm32.so data.
set_target_properties(AngryBirdsGoBridge PROPERTIES OUTPUT_NAME "ABK")

# The original Java Activity calls System.loadLibrary("gnustl_shared") and
# System.loadLibrary("SkynestSDK") before loading ABK. These ARM64 no-op
# load stubs satisfy Android's linker; the real ARM32 libraries are loaded
# privately by the bridge and their SDK JNI_OnLoad is invoked there.
add_library(GoLoadStub SHARED bridge_src/load_stub.cpp)
target_link_libraries(GoLoadStub PRIVATE log)
set_target_properties(GoLoadStub PROPERTIES OUTPUT_NAME "gnustl_shared")

add_library(GoSdkLoadStub SHARED bridge_src/load_stub.cpp)
target_link_libraries(GoSdkLoadStub PRIVATE log)
set_target_properties(GoSdkLoadStub PROPERTIES OUTPUT_NAME "SkynestSDK")

add_library(GoAdcolonyLoadStub SHARED bridge_src/load_stub.cpp)
target_link_libraries(GoAdcolonyLoadStub PRIVATE log)
set_target_properties(GoAdcolonyLoadStub PROPERTIES OUTPUT_NAME "adcolony")

add_library(GoJsLoadStub SHARED bridge_src/load_stub.cpp)
target_link_libraries(GoJsLoadStub PRIVATE log)
set_target_properties(GoJsLoadStub PROPERTIES OUTPUT_NAME "js")
