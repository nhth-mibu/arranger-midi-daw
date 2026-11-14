# arranger_whisper: writes down sung or spoken words with whisper.cpp (MIT).
# Plain C++, no JUCE. The model file is not built in: the app downloads one.
FetchContent_Declare(whisper_cpp
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG v1.8.7
    GIT_SHALLOW TRUE)
set(WHISPER_BUILD_TESTS OFF CACHE INTERNAL "")
set(WHISPER_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(WHISPER_BUILD_SERVER OFF CACHE INTERNAL "")
set(WHISPER_ALL_WARNINGS OFF CACHE INTERNAL "")
set(WHISPER_CURL OFF CACHE INTERNAL "")
set(WHISPER_SDL2 OFF CACHE INTERNAL "")
# The app runs on other computers than the one that built it.
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_BACKEND_DL OFF CACHE BOOL "" FORCE)
# Static, so that the app needs no extra library files. whisper.cpp's own
# option() would otherwise put BUILD_SHARED_LIBS=ON in the cache for all.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
FetchContent_MakeAvailable(whisper_cpp)

add_library(arranger_whisper STATIC src/Lyrics.cpp)
add_library(arranger::whisper ALIAS arranger_whisper)
target_include_directories(arranger_whisper PUBLIC include)
target_link_libraries(arranger_whisper PUBLIC arranger::core PRIVATE whisper)
target_compile_features(arranger_whisper PUBLIC cxx_std_20)
target_compile_definitions(arranger_whisper PUBLIC ARRANGER_HAS_WHISPER=1)
arranger_set_warnings(arranger_whisper)
