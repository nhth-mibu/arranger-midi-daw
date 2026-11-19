# arranger_core: song model, timing, editing, undo and file formats. Plain C++20, no JUCE.
add_library(arranger_core
    src/Time.cpp
    src/Version.cpp
    src/RecentFiles.cpp
    src/TempoMap.cpp
    src/Song.cpp
    src/SongEdit.cpp
    src/Clipboard.cpp
    src/Document.cpp
    src/Quantise.cpp
    src/MidiFile.cpp
    src/PsrExport.cpp
    src/ProjectFile.cpp
    src/DeviceProfile.cpp
    src/RecorderImport.cpp
    src/SynthEditor.cpp
    src/ControllerProfile.cpp
    src/Sample.cpp
    src/AudioToMidi.cpp
    src/AudioAnalysis.cpp
    src/TimeStretch.cpp
    src/Transients.cpp
    src/StyleFile.cpp
    src/StyleChord.cpp
    src/ChordFinder.cpp
    src/Theory.cpp
    src/Arranging.cpp
    src/ChordInput.cpp
    src/KidsMode.cpp
    src/HardwareFit.cpp
    src/Clarify.cpp
    src/Skills.cpp
    src/McpServer.cpp
    src/MidiAi.cpp
    src/Assistant.cpp
    src/StyleConvert.cpp
    src/StyleFromAudio.cpp
    src/LinkAudio.cpp
    src/Notation.cpp
    src/ScoreLayout.cpp
    src/MusicXml.cpp
    src/Pdf.cpp)
add_library(arranger::core ALIAS arranger_core)

target_include_directories(arranger_core PUBLIC include)
target_link_libraries(arranger_core PRIVATE nlohmann_json::nlohmann_json yaml-cpp::yaml-cpp)
target_compile_features(arranger_core PUBLIC cxx_std_20)
arranger_set_warnings(arranger_core)
