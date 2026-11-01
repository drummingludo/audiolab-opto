cmake_minimum_required(VERSION 3.22)

# Mac : un seul binaire pour Apple Silicon et Intel
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(AudioLabOpto VERSION 1.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 8 (licence AGPLv3 : le code de ce plugin est public)
set(ALO_JUCE_PATH "" CACHE PATH "Dossier JUCE local (sinon téléchargé)")
if(ALO_JUCE_PATH)
    add_subdirectory(${ALO_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ALO_FORMATS VST3)
if(APPLE)
    list(APPEND ALO_FORMATS AU)
endif()
# AAX (Pro Tools) : SDK Avid fourni avec JUCE (licence GPL v3, compatible AGPLv3).
# Non signé, il ne se charge que dans Pro Tools Developer ; signature PACE à faire avant diffusion.
option(ALO_AAX "Fabriquer le format AAX (Mac et Windows)" ON)
if(ALO_AAX AND (APPLE OR WIN32))
    list(APPEND ALO_FORMATS AAX)
endif()
if(ALO_STANDALONE)
    list(APPEND ALO_FORMATS Standalone)
endif()

juce_add_plugin(AudioLabOpto
    PRODUCT_NAME "Audio Lab Opto"
    PLUGIN_NAME "Audio Lab Opto"
    DESCRIPTION "Compresseur optique d'Audio Lab (Chimiste du son)"
    COMPANY_NAME "Transversal Studio"
    COMPANY_WEBSITE "https://transversalstudio.fr"
    COMPANY_EMAIL "ludo@transversalstudio.fr"
    BUNDLE_ID "fr.transversalstudio.audiolab.opto"
    PLUGIN_MANUFACTURER_CODE Trsv
    PLUGIN_CODE AlOp
    FORMATS ${ALO_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY AAX_ePlugInCategory_Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(ALOData SOURCES
    Resources/Poppins-Regular.ttf
    Resources/Poppins-Medium.ttf
    Resources/Poppins-SemiBold.ttf
    Resources/Poppins-Bold.ttf)

target_sources(AudioLabOpto PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(AudioLabOpto PRIVATE Source)
if(MSVC)
    target_compile_options(AudioLabOpto PRIVATE /utf-8)
endif()

target_compile_definitions(AudioLabOpto PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(AudioLabOpto
    PRIVATE
        ALOData
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Outil de test (captures de l'interface hors DAW) — non livré
option(ALO_TESTS "Construire les outils de test" OFF)
if(ALO_TESTS)
    juce_add_console_app(ALORenderUI PRODUCT_NAME "ALORenderUI")
    target_sources(ALORenderUI PRIVATE tests/render_ui.cpp)
    target_include_directories(ALORenderUI PRIVATE Source)
    target_compile_definitions(ALORenderUI PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ALORenderUI PRIVATE AudioLabOpto ALOData juce::juce_audio_utils juce::juce_gui_basics juce::juce_recommended_config_flags)

    juce_add_console_app(ALOProcessorTest PRODUCT_NAME "ALOProcessorTest")
    target_sources(ALOProcessorTest PRIVATE tests/processor_test.cpp)
    target_include_directories(ALOProcessorTest PRIVATE Source)
    target_compile_definitions(ALOProcessorTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ALOProcessorTest PRIVATE AudioLabOpto ALOData juce::juce_audio_utils juce::juce_gui_basics juce::juce_recommended_config_flags)
endif()
