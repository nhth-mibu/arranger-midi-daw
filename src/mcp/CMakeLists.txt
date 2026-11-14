# arranger-mcp: the helper that AI assistants start. It links the JUCE-free core
# (for the tool list) and juce_core (for the local socket), and goes next to the app.
juce_add_console_app(ArrangerMcp PRODUCT_NAME "arranger-mcp")

target_sources(ArrangerMcp PRIVATE Main.cpp)
target_compile_definitions(ArrangerMcp PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(ArrangerMcp
    PRIVATE
        arranger::core
        nlohmann_json::nlohmann_json
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags)
set_target_properties(ArrangerMcp PROPERTIES OUTPUT_NAME "arranger-mcp")

# A change to the helper alone does not relink the app: copy it from its own build too.
add_custom_command(TARGET ArrangerMcp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ArrangerMidiDaw>
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ArrangerMcp> $<TARGET_FILE_DIR:ArrangerMidiDaw>/
    VERBATIM)
