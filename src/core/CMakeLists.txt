# arranger_core: song model, timing, editing, undo and file formats. Plain C++20, no JUCE.
add_library(arranger_core
    src/Time.cpp
    src/TempoMap.cpp
    src/Song.cpp
    src/SongEdit.cpp
    src/Document.cpp
    src/Quantise.cpp
    src/MidiFile.cpp
    src/ProjectFile.cpp
    src/DeviceProfile.cpp
    src/ControllerProfile.cpp)
add_library(arranger::core ALIAS arranger_core)

target_include_directories(arranger_core PUBLIC include)
target_link_libraries(arranger_core PRIVATE nlohmann_json::nlohmann_json yaml-cpp::yaml-cpp)
target_compile_features(arranger_core PUBLIC cxx_std_20)
arranger_set_warnings(arranger_core)
