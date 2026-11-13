# The desktop app: JUCE window, platform layer and UI.

# Built-in device profiles. Users can add their own in the app's Devices folder.
file(GLOB DEVICE_PROFILES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/resources/devices/*.yaml
    ${PROJECT_SOURCE_DIR}/resources/controllers/*.yaml)

juce_add_binary_data(arranger_resources
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        ${PROJECT_SOURCE_DIR}/resources/fonts/Inter-Regular.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/Inter-SemiBold.ttf
        ${PROJECT_SOURCE_DIR}/resources/themes/dark.json
        ${PROJECT_SOURCE_DIR}/resources/themes/light.json
        ${DEVICE_PROFILES})

juce_add_gui_app(ArrangerMidiDaw
    PRODUCT_NAME "Arranger MIDI DAW"
    COMPANY_NAME "Nhat Bui"
    BUNDLE_ID "com.nhatbui.arrangermididaw"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Arranger MIDI DAW records audio from your audio interface."
    DOCUMENT_EXTENSIONS arranger
    HARDENED_RUNTIME_ENABLED FALSE)

target_sources(ArrangerMidiDaw PRIVATE
    Main.cpp
    Session.cpp
    ui/Theme.cpp
    ui/AppLookAndFeel.cpp
    ui/HeaderBar.cpp
    ui/Panels.cpp
    ui/TimelineViews.cpp
    ui/PianoRoll.cpp
    ui/MixerView.cpp
    ui/BeatView.cpp
    ui/ClipGridView.cpp
    ui/StyleRuleEditor.cpp
    ui/SlicerView.cpp
    ui/MainComponent.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/juce/JuceDeviceHost.cpp)

target_include_directories(ArrangerMidiDaw PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/platform/include)

target_compile_definitions(ArrangerMidiDaw PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ArrangerMidiDaw,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ArrangerMidiDaw,JUCE_VERSION>")

target_link_libraries(ArrangerMidiDaw
    PRIVATE
        arranger::core
        arranger::engine
        arranger::soundfont
        arranger_resources
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
