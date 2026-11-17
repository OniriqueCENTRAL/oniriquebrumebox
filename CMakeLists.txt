cmake_minimum_required(VERSION 3.22)

project(OniriqueShaper VERSION 1.0.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Binaire universel par défaut ; -DCMAKE_OSX_ARCHITECTURES=arm64 pour compiler plus vite en dev.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
endif()

# JUCE : à côté de ce fichier, sinon la copie de ResoBox.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(SHAPER_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
else()
    set(SHAPER_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../ResoBox/JUCE")
endif()
set(SHAPER_JUCE_DIR "${SHAPER_JUCE_DEFAULT}" CACHE PATH "Chemin de JUCE")

if(NOT EXISTS "${SHAPER_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE introuvable dans ${SHAPER_JUCE_DIR} (-DSHAPER_JUCE_DIR=...)")
endif()

add_subdirectory("${SHAPER_JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Le moteur, sans JUCE : tout ce qui décide du son est ici, et testé hors ligne.
add_library(shaper_core STATIC
    Source/dsp/Fft.cpp
    Source/dsp/ShaperEngine.cpp)
target_include_directories(shaper_core PUBLIC Source)
set_target_properties(shaper_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(shaper_core PUBLIC _USE_MATH_DEFINES)
else()
    target_compile_options(shaper_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
set(SHAPER_FORMATS VST3)
if(APPLE)
    list(APPEND SHAPER_FORMATS AU)
endif()

option(SHAPER_COPY_AFTER_BUILD "Installer le plugin après la compilation" OFF)

juce_add_plugin(OniriqueShaper
    COMPANY_NAME "Onirique"
    BUNDLE_ID "com.onirique.shaper"
    PLUGIN_MANUFACTURER_CODE Onrq
    PLUGIN_CODE Brmb
    FORMATS ${SHAPER_FORMATS}
    PRODUCT_NAME "OniriqueShaper"
    PLUGIN_NAME "ONIRIQUE Shaper"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Reverb Dynamics
    # Pour la molette et la saisie des valeurs.
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${SHAPER_COPY_AFTER_BUILD})

# Le sigil ONIRIQUE, comme dans l'en-tête du site.
juce_add_binary_data(ShaperAssets SOURCES Resources/sigil.png)

target_sources(OniriqueShaper PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/ShaperDisplay.cpp
    Source/ui/Theme.cpp)

# Licence ONIRIQUE (copie de plugin-license/ d'ONIRIQUE CENTRAL). Voir License/README.md.
include(License/OniriqueLicense.cmake)
onirique_add_license(OniriqueShaper)

if(MSVC)
    target_compile_options(OniriqueShaper PRIVATE /utf-8)
endif()

target_compile_definitions(OniriqueShaper PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(OniriqueShaper
    PRIVATE
        shaper_core
        ShaperAssets
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
enable_testing()
add_executable(test_engine tests/test_engine.cpp)
target_link_libraries(test_engine PRIVATE shaper_core)
add_test(NAME engine COMMAND test_engine ${CMAKE_CURRENT_BINARY_DIR})

# Un outil de développement, pas le produit : rend l'interface dans un PNG, sans hôte.
add_executable(shaper_snapshot EXCLUDE_FROM_ALL tools/snapshot.cpp)
target_include_directories(shaper_snapshot PRIVATE $<TARGET_PROPERTY:OniriqueShaper,INCLUDE_DIRECTORIES>)
target_compile_definitions(shaper_snapshot PRIVATE $<TARGET_PROPERTY:OniriqueShaper,COMPILE_DEFINITIONS>)
target_link_libraries(shaper_snapshot PRIVATE OniriqueShaper shaper_core ShaperAssets)

# La démo interactive pour ONIRIQUE CENTRAL (le travail d'onirique-probe, sur Mac). Construite à la demande.
add_executable(shaper_demo EXCLUDE_FROM_ALL tools/demo.cpp)
target_include_directories(shaper_demo PRIVATE $<TARGET_PROPERTY:OniriqueShaper,INCLUDE_DIRECTORIES>)
target_compile_definitions(shaper_demo PRIVATE $<TARGET_PROPERTY:OniriqueShaper,COMPILE_DEFINITIONS>)
target_link_libraries(shaper_demo PRIVATE OniriqueShaper shaper_core ShaperAssets)

# Un outil de développement : passe les mêmes sons dans n'importe quel VST3 (Shaper, spiff...) et compare. À la demande.
juce_add_console_app(shaper_compare PRODUCT_NAME "shaper_compare")
set_target_properties(shaper_compare PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(shaper_compare PRIVATE tools/compare.cpp)
target_compile_definitions(shaper_compare PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(MSVC)
    target_compile_options(shaper_compare PRIVATE /utf-8)
endif()
target_link_libraries(shaper_compare PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp
    juce::juce_recommended_config_flags)
