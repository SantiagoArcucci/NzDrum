cmake_minimum_required(VERSION 3.22)

# Tiene que ir antes de project(): define la versión mínima de macOS del binario.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Versión mínima de macOS")

project(NzDrum VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE se descarga en la configuración, fijado a un tag exacto (como un lockfile).
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

option(NZDRUM_COPY_AFTER_BUILD "Instalar el plugin en la carpeta del sistema al compilar" ON)

juce_add_plugin(NzDrum
    COMPANY_NAME "Santiago Arcucci"
    BUNDLE_ID com.santiagoarcucci.nzdrum   # sin espacios: Apple solo acepta letras, números, guiones y puntos
    PLUGIN_MANUFACTURER_CODE Sarc   # 4 caracteres, al menos una mayúscula
    PLUGIN_CODE Nzdm                # 4 caracteres, exactamente una mayúscula
    FORMATS VST3 Standalone         # Standalone sirve para probar sin abrir el DAW
    PRODUCT_NAME "NzDrum"
    IS_SYNTH TRUE                   # es un instrumento: genera audio, no lo procesa
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Drum
    COPY_PLUGIN_AFTER_BUILD ${NZDRUM_COPY_AFTER_BUILD}
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(NzDrum
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PadComponent.cpp)

target_compile_definitions(NzDrum
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(NzDrum
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Pruebas del motor (sin DAW) ----
option(NZDRUM_BUILD_TESTS "Compilar las pruebas del motor" ON)

if(NZDRUM_BUILD_TESTS)
    juce_add_console_app(NzDrumTests PRODUCT_NAME "NzDrumTests")

    target_sources(NzDrumTests
        PRIVATE
            Tests/EngineTests.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp
            Source/PadComponent.cpp)

    target_compile_definitions(NzDrumTests
        PRIVATE
            JucePlugin_Name="NzDrum"
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)

    target_link_libraries(NzDrumTests
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
