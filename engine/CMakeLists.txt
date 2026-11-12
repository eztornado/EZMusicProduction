# Cadence audio engine — JUCE core + phase 1 CLI.
include(FetchContent)

# Pinned JUCE release. Bump deliberately: the engine tracks a single tested version.
set(CADENCE_JUCE_TAG 8.0.9 CACHE STRING "JUCE release tag")

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${CADENCE_JUCE_TAG}
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(JUCE)

# FluidSynth (LGPL 2.1+) backs EZInstruments. libsndfile adds SF3 support;
# it is optional, so a build can stay free of Homebrew dependencies.
set(BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
# libsndfile (brew) enables SF3 (Ogg Vorbis) sample loading. Turn it off for
# a self-contained build without Homebrew dependencies (SF2 only).
option(CADENCE_SF3 "Enable SF3 SoundFont support through libsndfile" ON)
set(enable-libsndfile ${CADENCE_SF3} CACHE BOOL "" FORCE)
set(LADSPA_SUPPORT OFF CACHE BOOL "" FORCE)
set(ENABLE_COVERAGE OFF CACHE BOOL "" FORCE)
# READLINE adds -isystem <SDK>/usr/include/readline/.. which puts the whole
# SDK include dir ahead of libc++ and breaks <cstddef> on this toolchain.
set(enable-readline OFF CACHE BOOL "" FORCE)
FetchContent_Declare(fluidsynth
    GIT_REPOSITORY https://github.com/FluidSynth/fluidsynth.git
    GIT_TAG v2.6.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(fluidsynth)

# Core library. In phase 1 it is linked statically into the CLI; a SHARED
# target with the same sources will back the Dart/Flutter FFI binding.
set(CADENCE_ENGINE_SOURCES
    src/model.cpp
    src/engine.cpp
    src/magenta_vae.cpp
    src/ai_generation.cpp
    src/ai_arranger.cpp
    src/mix_ai.cpp
    src/ezq4.cpp
    src/ezcomp.cpp
    src/ezlim.cpp
    src/ezinst.cpp
    src/ezsynth.cpp
    src/ezorq.cpp
    src/ezdrums.cpp
    src/ezarp.cpp
    src/remote_server.cpp
    src/c_api.cpp)

# macOS: ObjC open-panel bridge. Elsewhere: no native picker (the stub
# behaves like a cancelled dialog).
if(APPLE)
    list(INSERT CADENCE_ENGINE_SOURCES 0 src/native_dialogs.mm)
else()
    list(APPEND CADENCE_ENGINE_SOURCES src/native_dialogs_stub.cpp)
endif()

add_library(cadence_engine STATIC ${CADENCE_ENGINE_SOURCES})

target_include_directories(cadence_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(cadence_engine PUBLIC cxx_std_20)

# Plugin hosting: VST3 everywhere; AU (AudioUnits) is macOS-only.
# VST2 is intentionally not hosted.
target_compile_definitions(cadence_engine PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_MP3AUDIOFORMAT=1
    # The engine never opens a WebBrowserComponent nor does HTTP itself
    # (the Dart/Flutter side does): disabling them keeps Linux off
    # webkit2gtk/gtk headers and libcurl entirely.
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
if(APPLE)
    target_compile_definitions(cadence_engine PUBLIC JUCE_PLUGINHOST_AU=1)
    # CoreAudioKit provides AUGenericView, referenced by the AU host path.
    target_link_libraries(cadence_engine PUBLIC "-framework CoreAudioKit")
    target_link_libraries(cadence_engine PUBLIC "-framework UniformTypeIdentifiers")
    # BLAS for the Magenta MusicVAE LSTM layers (AI clip generation).
    target_link_libraries(cadence_engine PUBLIC "-framework Accelerate")
else()
    # MusicVAE makes a single BLAS call (cblas_sgemv); system CBLAS covers it
    # on Linux (pacman: cblas / blas-openblas, apt: libopenblas-dev).
    # On Windows CBLAS is optional: without it, AI generation is disabled.
    find_library(CADENCE_CBLAS NAMES cblas openblas)
    if(CADENCE_CBLAS)
        target_link_libraries(cadence_engine PUBLIC ${CADENCE_CBLAS})
    else()
        message(STATUS "CBLAS not found — AI generation (MusicVAE) will be disabled")
        target_compile_definitions(cadence_engine PUBLIC CADENCE_NO_CBLAS)
    endif()
endif()

target_link_libraries(cadence_engine
    PUBLIC
        juce::juce_core
        juce::juce_events
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_dsp
        libfluidsynth)

# Console-app test harness: devices / tone / play.
juce_add_console_app(cadence_cli PRODUCT_NAME "cadence-cli")
target_sources(cadence_cli PRIVATE apps/cli/main.cpp)
target_link_libraries(cadence_cli PRIVATE cadence_engine)

# Shared engine for the Dart/Flutter FFI binding
# (libcadence_engine.dylib / .so / cadence_engine.dll).
add_library(cadence_engine_shared SHARED ${CADENCE_ENGINE_SOURCES})
target_include_directories(cadence_engine_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(cadence_engine_shared PRIVATE cxx_std_20)

# Keep hosting flags and platform libs in sync with the static engine (see above).
target_compile_definitions(cadence_engine_shared PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
# Export C API symbols from the shared DLL on Windows.
target_compile_definitions(cadence_engine_shared PUBLIC CADENCE_EXPORTS)
if(APPLE)
    target_compile_definitions(cadence_engine_shared PRIVATE JUCE_PLUGINHOST_AU=1)
    target_link_libraries(cadence_engine_shared PRIVATE
        "-framework CoreAudioKit"
        "-framework UniformTypeIdentifiers"
        "-framework Accelerate")
else()
    if(CADENCE_CBLAS)
        target_link_libraries(cadence_engine_shared PRIVATE ${CADENCE_CBLAS})
    else()
        target_compile_definitions(cadence_engine_shared PRIVATE CADENCE_NO_CBLAS)
    endif()
endif()
target_link_libraries(cadence_engine_shared
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_dsp
        libfluidsynth)
set_target_properties(cadence_engine_shared PROPERTIES OUTPUT_NAME cadence_engine)
