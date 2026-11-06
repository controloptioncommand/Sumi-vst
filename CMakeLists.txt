cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architectures")

project(Sumi VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: локально можно передать -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(SUMI_FORMATS VST3 AU Standalone)
else()
    set(SUMI_FORMATS VST3 Standalone)
endif()

# Эффект со стереовходом: Paint рисует звук с нуля (вход не нужен), Imprint вписывает картинку во входной звук
juce_add_plugin(Sumi
    COMPANY_NAME "Personal"
    BUNDLE_ID "com.personal.sumi"
    PLUGIN_MANUFACTURER_CODE Prsn
    PLUGIN_CODE Sumi
    FORMATS ${SUMI_FORMATS}
    PRODUCT_NAME "Sumi"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Generator
    AU_MAIN_TYPE kAudioUnitType_Effect)

set(SUMI_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Params.cpp
    Source/Presets.cpp
    Source/Picture.cpp
    Source/DSP/SpectralEngine.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/Widgets.cpp
    Source/UI/Views.cpp
    Source/UI/About.cpp)

target_sources(Sumi PRIVATE ${SUMI_SOURCES})
target_include_directories(Sumi PRIVATE Source)

set(SUMI_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(Sumi PUBLIC ${SUMI_DEFS})

# Шрифты Medeo (SIL OFL): Sans — интерфейс, Mono — числа, Pixel — логотип
juce_add_binary_data(SumiFonts
    NAMESPACE SumiFonts
    HEADER_NAME BinaryData.h
    SOURCES
        Resources/Fonts/MedeoSans-Regular.ttf
        Resources/Fonts/MedeoSans-SemiBold.ttf
        Resources/Fonts/MedeoMono-Regular.ttf
        Resources/Fonts/MedeoMono-SemiBold.ttf
        Resources/Fonts/MedeoPixel-Regular.ttf)
set_target_properties(SumiFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(SUMI_LIBS
    SumiFonts
    juce::juce_audio_utils
    juce::juce_dsp)

target_link_libraries(Sumi
    PRIVATE ${SUMI_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Офлайн-тест: рендер в WAV, проверка картинки в спектре, состояния и скриншоты интерфейса
option(SUMI_BUILD_TESTS "Build offline test" OFF)
if(SUMI_BUILD_TESTS)
    juce_add_console_app(SumiTest PRODUCT_NAME "SumiTest")
    target_sources(SumiTest PRIVATE Tests/SumiTest.cpp ${SUMI_SOURCES})
    target_include_directories(SumiTest PRIVATE Source)
    target_compile_definitions(SumiTest PRIVATE ${SUMI_DEFS} SUMI_TEST_BUILD=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SumiTest PRIVATE ${SUMI_LIBS} juce::juce_recommended_config_flags)
endif()
