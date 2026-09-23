# Cadence audio engine — JUCE core + phase 1 CLI.
include(FetchContent)

# Pinned JUCE release. Bump deliberately: the engine tracks a single tested version.
set(CADENCE_JUCE_TAG 8.0.9 CACHE STRING "JUCE release tag")

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${CADENCE_JUCE_TAG}
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(JUCE)

# Core library. In phase 1 it is linked statically into the CLI; a SHARED
# target with the same sources will back the Dart/Flutter FFI binding.
add_library(cadence_engine STATIC
    src/model.cpp
    src/engine.cpp
    src/c_api.cpp)

target_include_directories(cadence_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(cadence_engine PUBLIC cxx_std_20)

target_link_libraries(cadence_engine
    PUBLIC
        juce::juce_core
        juce::juce_events
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_audio_processors)

# Console-app test harness: devices / tone / play.
juce_add_console_app(cadence_cli PRODUCT_NAME "cadence-cli")
target_sources(cadence_cli PRIVATE apps/cli/main.cpp)
target_link_libraries(cadence_cli PRIVATE cadence_engine)

# Shared engine for the Dart/Flutter FFI binding (libcadence_engine.dylib).
add_library(cadence_engine_shared SHARED
    src/model.cpp
    src/engine.cpp
    src/c_api.cpp)
target_include_directories(cadence_engine_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(cadence_engine_shared PRIVATE cxx_std_20)
target_link_libraries(cadence_engine_shared
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_audio_processors)
set_target_properties(cadence_engine_shared PROPERTIES OUTPUT_NAME cadence_engine)
