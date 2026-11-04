# Minimal VST3 fixture for ui/test/plugins_scan_test.dart — see Main.cpp.
cmake_minimum_required(VERSION 3.22)
project(cadence_test_vst3 VERSION 1.0.0 LANGUAGES C CXX)

set(CADENCE_JUCE_DIR "" CACHE PATH "Path to a JUCE checkout")
if(NOT CADENCE_JUCE_DIR)
    message(FATAL_ERROR "Pass -DCADENCE_JUCE_DIR=<path to JUCE>")
endif()
add_subdirectory(${CADENCE_JUCE_DIR} juce)

juce_add_plugin(cadence_test_vst3
    PLUGIN_MANUFACTURER_CODE Cdnc
    PLUGIN_CODE Cdtn
    FORMATS VST3
    PRODUCT_NAME "CadenceTestTone"
    COMPANY_NAME "Cadence")
target_sources(cadence_test_vst3 PRIVATE Main.cpp)
target_compile_features(cadence_test_vst3 PRIVATE cxx_std_20)
# Same web/curl-off combo as the engine so Linux needs no gtk/webkit headers.
target_compile_definitions(cadence_test_vst3 PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(cadence_test_vst3 PRIVATE juce::juce_audio_processors)
