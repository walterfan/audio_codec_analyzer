cmake_minimum_required(VERSION 3.20)
project(audio_codec_analyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(ACA_BUILD_TESTS "Build unit tests" ON)
option(ACA_ENABLE_LIVE "Build the live PortAudio duplex demo" ON)
option(ACA_ENABLE_ASAN "Build with AddressSanitizer + UBSan" OFF)
option(ACA_ENABLE_TSAN "Build with ThreadSanitizer" OFF)

if(ACA_ENABLE_ASAN AND ACA_ENABLE_TSAN)
  message(FATAL_ERROR "ACA_ENABLE_ASAN and ACA_ENABLE_TSAN cannot be used together")
endif()

# Homebrew prefix so pkg-config/find_package see keg-only + standard formulae.
if(APPLE)
  execute_process(COMMAND brew --prefix
                  OUTPUT_VARIABLE ACA_BREW_PREFIX
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
  if(ACA_BREW_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${ACA_BREW_PREFIX}")
    set(ENV{PKG_CONFIG_PATH} "${ACA_BREW_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
  endif()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS     REQUIRED IMPORTED_TARGET opus)
pkg_check_modules(SPEEXDSP REQUIRED IMPORTED_TARGET speexdsp)
pkg_check_modules(SNDFILE  REQUIRED IMPORTED_TARGET sndfile)

add_library(aca_core STATIC
  src/core/wav_io.cpp
  src/core/audio_buffer.cpp
  src/core/metrics.cpp
  src/core/frame_stream.cpp
  src/codec/opus_codec.cpp
  src/dsp/echo_canceller.cpp
  src/dsp/noise_suppressor.cpp
  src/dsp/gain_controller.cpp
  src/dsp/pipeline.cpp
  src/cli/args.cpp
)
target_include_directories(aca_core PUBLIC include)
target_link_libraries(aca_core PUBLIC
  PkgConfig::OPUS
  PkgConfig::SPEEXDSP
  PkgConfig::SNDFILE
)
target_compile_options(aca_core PRIVATE -Wall -Wextra -Wpedantic)

# Hardened stdlib checks (libc++ and/or libstdc++). Harmless if the active
# standard library ignores the other family's macro.
if(ACA_ENABLE_ASAN OR ACA_ENABLE_TSAN)
  target_compile_definitions(aca_core PUBLIC
    "_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST"
    _GLIBCXX_ASSERTIONS
  )
endif()

if(ACA_ENABLE_ASAN)
  target_compile_options(aca_core PUBLIC
    -fsanitize=address,undefined -fno-omit-frame-pointer -g)
  target_link_options(aca_core PUBLIC -fsanitize=address,undefined)
endif()

if(ACA_ENABLE_TSAN)
  target_compile_options(aca_core PUBLIC
    -fsanitize=thread -fno-omit-frame-pointer -g)
  target_link_options(aca_core PUBLIC -fsanitize=thread)
endif()

# ---- live audio (optional) ----
set(ACA_LIVE_AVAILABLE OFF)
if(ACA_ENABLE_LIVE)
  pkg_check_modules(PORTAUDIO IMPORTED_TARGET portaudio-2.0)
  if(PORTAUDIO_FOUND)
    set(ACA_LIVE_AVAILABLE ON)
    target_sources(aca_core PRIVATE src/live/live_session.cpp)
    target_link_libraries(aca_core PUBLIC PkgConfig::PORTAUDIO)
    target_compile_definitions(aca_core PUBLIC ACA_HAVE_PORTAUDIO=1)
  else()
    message(STATUS "portaudio not found -> 'aca live' and 'aca devices' will be stubbed out")
  endif()
endif()

add_executable(aca
  src/cli/main.cpp
  src/cli/cmd_codec.cpp
  src/cli/cmd_dsp.cpp
  src/cli/cmd_live.cpp
  src/cli/cmd_analyze.cpp
)
target_link_libraries(aca PRIVATE aca_core)
target_compile_options(aca PRIVATE -Wall -Wextra -Wpedantic)

add_executable(gen_fixtures tools/gen_fixtures.cpp)
target_link_libraries(gen_fixtures PRIVATE aca_core)

if(ACA_BUILD_TESTS)
  find_package(GTest QUIET)
  if(GTest_FOUND)
    enable_testing()
    add_executable(aca_tests
      tests/test_wav_io.cpp
      tests/test_opus_codec.cpp
      tests/test_dsp.cpp
      tests/test_metrics.cpp
      tests/test_frame_stream.cpp
      tests/test_cli_args.cpp
    )
    target_link_libraries(aca_tests PRIVATE aca_core GTest::gtest GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(aca_tests)
  else()
    message(STATUS "GTest not found -> tests disabled (brew install googletest)")
  endif()
endif()

message(STATUS "aca: live audio = ${ACA_LIVE_AVAILABLE}, tests = ${ACA_BUILD_TESTS}")
