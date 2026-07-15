cmake_minimum_required(VERSION 3.15)
if(APPLE)
  project(poketrack VERSION 0.2.0 LANGUAGES C OBJC)
else()
  project(poketrack VERSION 0.2.0 LANGUAGES C)
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
# FLAC is off by default in raylib's config.h; SFZ instruments (e.g. ARIA-format
# piano libraries) commonly ship FLAC samples, so turn it on for everything raylib compiles.
add_compile_definitions(SUPPORT_FILEFORMAT_FLAC=1)
FetchContent_Declare(raylib URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.zip)
FetchContent_MakeAvailable(raylib)

# CLAP headers (instruments + effects plugin standard)
FetchContent_Declare(clap URL https://github.com/free-audio/clap/archive/refs/tags/1.2.6.zip)
FetchContent_MakeAvailable(clap)

# wclap-bridge (native only) — loads WCLAP (CLAP-compiled-to-wasm32) plugins via
# Wasmtime and hands back a normal CLAP factory, so the existing CLAP host code
# barely changes. Pulls in wasmtime + a webview-gui dep transitively (unused by
# poketrack — no plugin GUI support here — but it's built as part of the static lib).
if(NOT EMSCRIPTEN)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    FetchContent_Declare(wclap-bridge
        GIT_REPOSITORY https://github.com/WebCLAP/wclap-bridge.git
        GIT_TAG main
        GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_MakeAvailable(wclap-bridge)
endif()

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

# miniz (single-file zip reader, used by SFZ unit to load .zip instrument bundles)
set(MINIZ_SRC "${CMAKE_BINARY_DIR}/miniz_include/miniz.c")
if(NOT EXISTS "${MINIZ_SRC}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/miniz_include")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/richgel999/miniz/v114/miniz.c"
        "${MINIZ_SRC}"
        STATUS MINIZ_STATUS
    )
    list(GET MINIZ_STATUS 0 MINIZ_STATUS_CODE)
    if(NOT MINIZ_STATUS_CODE EQUAL 0)
        message(WARNING "miniz download failed — SFZ .zip support disabled")
    endif()
endif()
set(MINIZ_INCLUDE_DIR "${CMAKE_BINARY_DIR}/miniz_include")

set(SOURCES
    src/main.c
    src/tracker.c
    src/input.c
    src/icons.c
    src/audio.c
    src/ui.c
    src/theme.c
    src/file_browser.c
    src/midi_in.c
    src/screen_song.c
    src/screen_pattern.c
    src/screen_instrument.c
    src/screen_menu.c
    src/units/osc.c
    src/units/fm.c
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
    src/units/lfo.c
    src/units/unit_registry.c
)

list(APPEND SOURCES src/midi_out.c src/units/midi_unit.c src/units/clap_unit.c)
if(EMSCRIPTEN)
    list(APPEND SOURCES src/wclap_host_web.c)
else()
    list(APPEND SOURCES src/wclap_host_native.c src/units/sfz.c)
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

option(FORCE_GAMEPAD_ICONS "Always render gamepad-style button icons, even with no controller connected (for testing icon layout)" OFF)
if(FORCE_GAMEPAD_ICONS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE FORCE_GAMEPAD_ICONS)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${raylib_SOURCE_DIR}/src
    ${TSF_INCLUDE_DIR}
    ${MINIZ_INCLUDE_DIR}
    ${TFD_DIR}
    src
)
target_include_directories(${PROJECT_NAME} PRIVATE ${clap_SOURCE_DIR}/include)
if(NOT EMSCRIPTEN)
    target_include_directories(${PROJECT_NAME} PRIVATE ${wclap-bridge_SOURCE_DIR}/include)
    # wclap-bridge is C++ (Wasmtime embedding); force the C++ linker for the final link.
    set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
endif()

if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".mjs")
    target_link_options(${PROJECT_NAME} PRIVATE
        -sUSE_GLFW=3
        -sINITIAL_MEMORY=134217728  # 128MB: multi-track patterns enlarge TrackerSong (~29MB)
        -sALLOW_MEMORY_GROWTH=1
        -sFORCE_FILESYSTEM=1
        -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']
        "-sEXPORTED_FUNCTIONS=['_main','_midi_in_web_set_port_count','_midi_in_web_set_port_name','_midi_in_web_push','_midi_out_web_set_port_count','_midi_out_web_set_port_name']"
        -sEXPORT_ES6=1
        -sMODULARIZE=1
        -sSINGLE_FILE=1
        -sNO_EXIT_RUNTIME=1
        -sASSERTIONS=0
        -sWASM=1
        --no-heap-copy
        --js-library "${CMAKE_SOURCE_DIR}/src/wclap_host_web.lib.js"
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
    target_link_libraries(${PROJECT_NAME} PRIVATE wclap-bridge)
    if(APPLE)
        target_sources(${PROJECT_NAME} PRIVATE src/macos_dialog.m)
        target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Cocoa" "-framework AppKit" "-framework CoreMIDI" "-framework CoreFoundation")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "poketrack"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist.in"
        )
    endif()
endif()
