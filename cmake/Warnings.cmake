# Strict warnings for our own targets only (not for JUCE or other deps).
function(arranger_set_warnings target)
    if(MSVC)
        # C4324: padding from alignas on purpose (lock-free queue indices on separate cache lines).
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /wd4324)
        if(ARRANGER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            # GCC 12-13 report false stringop-overflow errors inside std::vector at -O3.
            target_compile_options(${target} PRIVATE -Wno-stringop-overflow)
        endif()
        if(ARRANGER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
