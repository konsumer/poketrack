cmake_minimum_required(VERSION 3.20)
if(APPLE)
  project(raypoketrack VERSION 0.2.0 LANGUAGES C OBJC)
else()
  project(raypoketrack VERSION 0.2.0 LANGUAGES C)
endif()

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
  set(CMAKE_POLICY_DEFAULT_CMP0135 NEW)
endif()
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
endif()

set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)

# raylib 6.0
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(raylib URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.zip)
FetchContent_MakeAvailable(raylib)

# CLAP headers (instruments + effects plugin standard)
FetchContent_Declare(clap URL https://github.com/free-audio/clap/archive/refs/tags/1.2.6.zip)
FetchContent_MakeAvailable(clap)

# TinySoundFont (single-header SF2 player)
set(TSF_HEADER "${CMAKE_BINARY_DIR}/tsf_include/tsf.h")
if(NOT EXISTS "${TSF_HEADER}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tsf_include")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/schellingb/TinySoundFont/master/tsf.h"
        "${TSF_HEADER}"
        STATUS TSF_STATUS
    )
    list(GET TSF_STATUS 0 TSF_STATUS_CODE)
    if(NOT TSF_STATUS_CODE EQUAL 0)
        message(WARNING "TinySoundFont download failed — SF2 unit disabled")
    endif()
endif()
set(TSF_INCLUDE_DIR "${CMAKE_BINARY_DIR}/tsf_include")

# tinyfiledialogs (native OS file picker dialog)
set(TFD_DIR "${CMAKE_BINARY_DIR}/tfd_include")
if(NOT EXISTS "${TFD_DIR}/tinyfiledialogs.c")
    file(MAKE_DIRECTORY "${TFD_DIR}")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/native-toolkit/libtinyfiledialogs/master/tinyfiledialogs.c"
        "${TFD_DIR}/tinyfiledialogs.c" STATUS TFD_STATUS_C)
    file(DOWNLOAD
        "https://raw.githubusercontent.com/native-toolkit/libtinyfiledialogs/master/tinyfiledialogs.h"
        "${TFD_DIR}/tinyfiledialogs.h" STATUS TFD_STATUS_H)
endif()

set(SOURCES
    src/main.c
    src/tracker.c
    src/input.c
    src/audio.c
    src/ui.c
    src/file_browser.c
    src/midi_in.c
    src/screen_song.c
    src/screen_pattern.c
    src/screen_instrument.c
    src/screen_menu.c
    src/units/osc.c
    src/units/drum.c
    src/units/sf2.c
    src/units/delay.c
    src/units/dist.c
    src/units/reverb.c
    src/units/chorus.c
    src/units/flanger.c
    src/units/phaser.c
    src/units/gran.c
    src/units/filter.c
    src/units/sampler.c
    src/units/bitcrush.c
    src/units/tremolo.c
    src/units/compressor.c
    src/units/ducker.c
    src/units/unit_registry.c
)

# CLAP and tinyfiledialogs — desktop only
if(NOT EMSCRIPTEN)
    list(APPEND SOURCES src/clap_host.c src/units/clap_unit.c)
    list(APPEND SOURCES ${TFD_DIR}/tinyfiledialogs.c)
    list(APPEND SOURCES src/midi_out.c src/units/midi_unit.c)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${raylib_SOURCE_DIR}/src
    ${TSF_INCLUDE_DIR}
    ${TFD_DIR}
    src
)
if(NOT EMSCRIPTEN)
    target_include_directories(${PROJECT_NAME} PRIVATE ${clap_SOURCE_DIR}/include)
endif()

if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".mjs")
    target_link_options(${PROJECT_NAME} PRIVATE
        -sUSE_GLFW=3
        -sINITIAL_MEMORY=67108864
        -sALLOW_MEMORY_GROWTH=1
        -sFORCE_FILESYSTEM=1
        -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']
        -sEXPORT_ES6=1
        -sMODULARIZE=1
        -sSINGLE_FILE=1
        -sNO_EXIT_RUNTIME=1
        -sASSERTIONS=0
        -sWASM=1
        --no-heap-copy
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE raylib)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE raylib m)
    if(UNIX AND NOT APPLE)
        find_package(ALSA QUIET)
        if(ALSA_FOUND)
            target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_ALSA)
            target_include_directories(${PROJECT_NAME} PRIVATE ${ALSA_INCLUDE_DIRS})
            target_link_libraries(${PROJECT_NAME} PRIVATE ${ALSA_LIBRARIES})
        endif()
    endif()
    if(WIN32)
        target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
    endif()
    if(APPLE)
        target_sources(${PROJECT_NAME} PRIVATE src/macos_dialog.m)
        target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Cocoa" "-framework AppKit" "-framework CoreMIDI" "-framework CoreFoundation")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "raypoketrack"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist.in"
        )
    endif()
endif()
