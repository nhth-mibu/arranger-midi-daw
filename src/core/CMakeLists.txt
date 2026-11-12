# arranger_core: song model, timing and project file. Plain C++20, no JUCE.
add_library(arranger_core
    src/Time.cpp
    src/ProjectFile.cpp)
add_library(arranger::core ALIAS arranger_core)

target_include_directories(arranger_core PUBLIC include)
target_link_libraries(arranger_core PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(arranger_core PUBLIC cxx_std_20)
arranger_set_warnings(arranger_core)
