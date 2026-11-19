# Small VST3 plugins for trying the plugin host: an effect, an instrument, and
# one that crashes, hangs or changes its latency on purpose (for the tests).
# They are not part of the app. Build them with -DARRANGER_BUILD_TEST_PLUGINS=ON.

foreach(kind Gain Tone Misbehave)
    set(manifest TRUE)
    if(kind STREQUAL "Tone")
        set(is_synth TRUE)
        set(code Attn)
    elseif(kind STREQUAL "Misbehave")
        set(is_synth FALSE)
        set(code Atmb)
        # No moduleinfo.json: a scan must load the plugin to learn what it is, as for older plugins.
        set(manifest FALSE)
    else()
        set(is_synth FALSE)
        set(code Atgn)
    endif()
    juce_add_plugin(ArrangerTest${kind}
        PRODUCT_NAME "Arranger Test ${kind}"
        COMPANY_NAME "Arranger Tests"
        BUNDLE_ID com.arrangertests.${kind}
        PLUGIN_MANUFACTURER_CODE Arrt
        PLUGIN_CODE ${code}
        FORMATS VST3
        IS_SYNTH ${is_synth}
        NEEDS_MIDI_INPUT ${is_synth}
        VST3_AUTO_MANIFEST ${manifest}
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
