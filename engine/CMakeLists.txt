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
enable_language(OBJCXX)
add_library(cadence_engine STATIC
    src/native_dialogs.mm
    src/model.cpp
    src/engine.cpp
    src/magenta_vae.cpp
    src/ai_generation.cpp
    src/ezq4.cpp
    src/ezinst.cpp
    src/ezsynth.cpp
    src/ezorq.cpp
    src/ezdrums.cpp
    src/ezarp.cpp
    src/c_api.cpp)

target_include_directories(cadence_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(cadence_engine PUBLIC cxx_std_20)

# Plugin hosting: VST3 + AU. VST2 is intentionally not hosted.
# CoreAudioKit provides AUGenericView, referenced by the AU host path.
target_compile_definitions(cadence_engine PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(cadence_engine PUBLIC "-framework CoreAudioKit")
target_link_libraries(cadence_engine PUBLIC "-framework UniformTypeIdentifiers")
# BLAS for the Magenta MusicVAE LSTM layers (AI clip generation).
target_link_libraries(cadence_engine PUBLIC "-framework Accelerate")

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

# Shared engine for the Dart/Flutter FFI binding (libcadence_engine.dylib).
add_library(cadence_engine_shared SHARED
    src/native_dialogs.mm
    src/model.cpp
    src/engine.cpp
    src/magenta_vae.cpp
    src/ai_generation.cpp
    src/ezq4.cpp
    src/ezinst.cpp
    src/ezsynth.cpp
    src/ezorq.cpp
    src/ezdrums.cpp
    src/ezarp.cpp
    src/c_api.cpp)
target_include_directories(cadence_engine_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(cadence_engine_shared PRIVATE cxx_std_20)

# Keep hosting flags in sync with the static engine (see above).
target_compile_definitions(cadence_engine_shared PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(cadence_engine_shared PRIVATE "-framework CoreAudioKit")
target_link_libraries(cadence_engine_shared PRIVATE "-framework UniformTypeIdentifiers")
target_link_libraries(cadence_engine_shared PRIVATE "-framework Accelerate")
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
