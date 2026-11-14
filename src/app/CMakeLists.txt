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
        ${PROJECT_SOURCE_DIR}/resources/fonts/Bravura.otf
        ${PROJECT_SOURCE_DIR}/resources/themes/dark.json
        ${PROJECT_SOURCE_DIR}/resources/themes/light.json
        ${PROJECT_SOURCE_DIR}/resources/icons/app-icon-256.png
        ${DEVICE_PROFILES})

juce_add_gui_app(ArrangerMidiDaw
    PRODUCT_NAME "Arranger MIDI DAW"
    COMPANY_NAME "Nhat Bui"
    BUNDLE_ID "com.nhatbui.arrangermididaw"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Arranger MIDI DAW records audio from your audio interface."
    DOCUMENT_EXTENSIONS arranger
    ICON_BIG ${PROJECT_SOURCE_DIR}/resources/icons/app-icon.png
    ICON_SMALL ${PROJECT_SOURCE_DIR}/resources/icons/app-icon-256.png
    HARDENED_RUNTIME_ENABLED FALSE)

target_sources(ArrangerMidiDaw PRIVATE
    Main.cpp
    Session.cpp
    McpService.cpp
    AssistantChat.cpp
    AudioFiles.cpp
    plugins/PluginHost.cpp
    ui/Theme.cpp
    ui/AppLookAndFeel.cpp
    ui/HeaderBar.cpp
    ui/Panels.cpp
    ui/TimelineViews.cpp
    ui/PianoRoll.cpp
    ui/MixerView.cpp
    ui/ChatView.cpp
    ui/BeatView.cpp
    ui/ScoreView.cpp
    ui/ClipGridView.cpp
    ui/StyleRuleEditor.cpp
    ui/SlicerView.cpp
    ui/MainComponent.cpp
    ui/PluginViews.cpp
    ui/SoundEditorView.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/juce/JuceDeviceHost.cpp)

target_include_directories(ArrangerMidiDaw PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/platform/include)

target_compile_definitions(ArrangerMidiDaw PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_PLUGINHOST_VST3=1
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>
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

# The MCP helper goes next to the app's program (inside the .app on macOS), where
# Options > Allow AI Assistants finds it.
add_dependencies(ArrangerMidiDaw ArrangerMcp)
add_custom_command(TARGET ArrangerMidiDaw POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ArrangerMcp> $<TARGET_FILE_DIR:ArrangerMidiDaw>/
    VERBATIM)
if(APPLE)
    # Sign the helper, then the bundle again, so the bundle's seal covers it (ad-hoc, as the release script does).
    add_custom_command(TARGET ArrangerMidiDaw POST_BUILD
        COMMAND codesign --force --sign - --timestamp=none $<TARGET_FILE_DIR:ArrangerMidiDaw>/arranger-mcp
        COMMAND codesign --force --sign - --timestamp=none $<TARGET_BUNDLE_DIR:ArrangerMidiDaw>
        VERBATIM)
endif()
