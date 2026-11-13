cmake_minimum_required(VERSION 3.22)

project(OniriqueBrumeBox VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Binaire universel par défaut ; -DCMAKE_OSX_ARCHITECTURES=arm64 pour compiler plus vite en dev.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
endif()

# JUCE : à côté de ce fichier, sinon la copie de ResoBox.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(BRUMEBOX_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
else()
    set(BRUMEBOX_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../ResoBox/JUCE")
endif()
set(BRUMEBOX_JUCE_DIR "${BRUMEBOX_JUCE_DEFAULT}" CACHE PATH "Chemin de JUCE")

if(NOT EXISTS "${BRUMEBOX_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE introuvable dans ${BRUMEBOX_JUCE_DIR} (-DBRUMEBOX_JUCE_DIR=...)")
endif()

add_subdirectory("${BRUMEBOX_JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Le moteur, sans JUCE : tout ce qui décide du son est ici, et testé hors ligne.
add_library(brumebox_core STATIC
    Source/dsp/Fft.cpp
    Source/dsp/BrumeEngine.cpp)
target_include_directories(brumebox_core PUBLIC Source)
set_target_properties(brumebox_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(brumebox_core PUBLIC _USE_MATH_DEFINES)
else()
    target_compile_options(brumebox_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
set(BRUMEBOX_FORMATS VST3)
if(APPLE)
    list(APPEND BRUMEBOX_FORMATS AU)
endif()

option(BRUMEBOX_COPY_AFTER_BUILD "Installer le plugin après la compilation" OFF)

juce_add_plugin(OniriqueBrumeBox
    COMPANY_NAME "Onirique"
    BUNDLE_ID "com.onirique.brumebox"
    PLUGIN_MANUFACTURER_CODE Onrq
    PLUGIN_CODE Brmb
    FORMATS ${BRUMEBOX_FORMATS}
    PRODUCT_NAME "BrumeBox"
    PLUGIN_NAME "ONIRIQUE BrumeBox"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Reverb Dynamics
    COPY_PLUGIN_AFTER_BUILD ${BRUMEBOX_COPY_AFTER_BUILD})

target_sources(OniriqueBrumeBox PRIVATE Source/PluginProcessor.cpp)

# Licence ONIRIQUE (copie de plugin-license/ d'ONIRIQUE CENTRAL). Voir License/README.md.
include(License/OniriqueLicense.cmake)
onirique_add_license(OniriqueBrumeBox)

if(MSVC)
    target_compile_options(OniriqueBrumeBox PRIVATE /utf-8)
endif()

target_compile_definitions(OniriqueBrumeBox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(OniriqueBrumeBox
    PRIVATE
        brumebox_core
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
enable_testing()
add_executable(test_engine tests/test_engine.cpp)
target_link_libraries(test_engine PRIVATE brumebox_core)
add_test(NAME engine COMMAND test_engine ${CMAKE_CURRENT_BINARY_DIR})
