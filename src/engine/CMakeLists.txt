# arranger_engine: real-time engine. Plain C++20, no JUCE.
add_library(arranger_engine
    src/Transport.cpp
    src/Playback.cpp
    src/Metronome.cpp
    src/Engine.cpp
    src/DrumSampler.cpp)
add_library(arranger::engine ALIAS arranger_engine)

target_include_directories(arranger_engine PUBLIC include)
target_link_libraries(arranger_engine PUBLIC arranger::core)
target_compile_features(arranger_engine PUBLIC cxx_std_20)
arranger_set_warnings(arranger_engine)
