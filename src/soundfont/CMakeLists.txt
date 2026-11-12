# arranger_soundfont: the built-in GM/GS player, FluidSynth (LGPL-2.1) behind our Processor interface. No JUCE.

set(FLUIDSYNTH_OPTIONS_OFF
    alsa aufile dbus ipv6 jack ladspa libinstpatch libsndfile midishare network oss dsound wasapi
    waveout winmidi sdl3 pulseaudio pipewire readline openmp systemd coreaudio coremidi framework
    native-dls unicode)
foreach(opt IN LISTS FLUIDSYNTH_OPTIONS_OFF)
    set(enable-${opt} OFF CACHE BOOL "" FORCE)
endforeach()
set(osal "cpp11" CACHE STRING "" FORCE)

# FluidSynth's own build reads BUILD_SHARED_LIBS; build it static without changing ours.
set(_arranger_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(fluidsynth
    GIT_REPOSITORY https://github.com/FluidSynth/fluidsynth.git
    GIT_TAG v2.5.7
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(fluidsynth)
set(BUILD_SHARED_LIBS ${_arranger_shared})

add_library(arranger_soundfont src/SoundFontPlayer.cpp)
add_library(arranger::soundfont ALIAS arranger_soundfont)
target_include_directories(arranger_soundfont PUBLIC include)
target_link_libraries(arranger_soundfont PUBLIC arranger::engine PRIVATE libfluidsynth)
target_compile_features(arranger_soundfont PUBLIC cxx_std_20)
arranger_set_warnings(arranger_soundfont)
