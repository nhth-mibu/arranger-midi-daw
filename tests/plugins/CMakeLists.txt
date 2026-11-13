# Two small VST3 plugins for trying the plugin host: an effect and an instrument.
# They are not part of the app. Build them with -DARRANGER_BUILD_TEST_PLUGINS=ON.

foreach(kind Gain Tone)
    if(kind STREQUAL "Tone")
        set(is_synth TRUE)
        set(code Attn)
    else()
        set(is_synth FALSE)
        set(code Atgn)
    endif()
    juce_add_plugin(ArrangerTest${kind}
        PRODUCT_NAME "Arranger Test ${kind}"
        COMPANY_NAME "Arranger Tests"
        PLUGIN_MANUFACTURER_CODE Arrt
        PLUGIN_CODE ${code}
        FORMATS VST3
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT ${is_synth}
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(ArrangerTest${kind} PRIVATE Test${kind}.cpp)
    target_compile_definitions(ArrangerTest${kind} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(ArrangerTest${kind}
        PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags)
endforeach()
