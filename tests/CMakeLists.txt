FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(arranger_tests
    TimeTests.cpp
    ProjectFileTests.cpp
    MetronomeTests.cpp
    EngineTests.cpp)

target_link_libraries(arranger_tests PRIVATE arranger::core arranger::engine Catch2::Catch2WithMain)
arranger_set_warnings(arranger_tests)

include(CTest)
include(Catch)
catch_discover_tests(arranger_tests)
