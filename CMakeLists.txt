cmake_minimum_required(VERSION 3.22)
project(PsyPlugins VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(COPY_AFTER_BUILD "Plugins nach dem Build in den System-Plugin-Ordner kopieren" ON)

# JUCE wird automatisch geladen, kein Submodule nötig
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Gemeinsame Einstellungen für alle Plugins
function(add_psy_plugin NAME CODE SOURCE)
    juce_add_plugin(${NAME}
        COMPANY_NAME "PsyPlugins"
        PLUGIN_MANUFACTURER_CODE Psyp
        PLUGIN_CODE ${CODE}
        FORMATS VST3 Standalone
        PRODUCT_NAME "${NAME}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        COPY_PLUGIN_AFTER_BUILD ${COPY_AFTER_BUILD})

    target_sources(${NAME} PRIVATE ${SOURCE})

    target_compile_definitions(${NAME} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(${NAME}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

add_psy_plugin(GoaGater Gtr1 Source/GoaGater.cpp)
add_psy_plugin(PsyDelay Pdl1 Source/PsyDelay.cpp)
add_psy_plugin(GoaFilter Gfl1 Source/GoaFilter.cpp)
