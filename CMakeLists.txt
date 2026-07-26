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
# barely changes. Its real webview-gui module (for CLAP_EXT_GUI, which poketrack
# never uses) pulls in a hard GTK+WebKit runtime dependency on Linux — wrong for
# a framebuffer-only handheld build with no desktop. patch-wclap-bridge.cmake
# drops that module from the build; src/vendor/webview-gui-stub (a no-op
# stand-in with the same public API) takes its place on the include path.
if(NOT EMSCRIPTEN)
    enable_language(CXX)
    # C++20 for everyone, not just MSVC ("strict about designated
    # initialisers" — wclap-bridge's own CMakeLists does this too, but only
    # when it's the top-level project, which it isn't here since we pull it
    # in via FetchContent). GCC/Clang have supported C++20 for years, so no
    # need to branch by compiler.
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    # Deliberately NOT setting CMAKE_MSVC_RUNTIME_LIBRARY here (wclap-bridge's
    # own top-level CMakeLists does, for its standalone builds) — raylib was
    # already fetched above with CMake's default (dynamic /MD) runtime before
    # this point, so overriding it here would only apply to targets configured
    # after this line, mixing /MT and /MD in one executable — real link errors
    # (__imp_* unresolved externals across raylib's objects).
    FetchContent_Declare(wclap-bridge
        GIT_REPOSITORY https://github.com/WebCLAP/wclap-bridge.git
        GIT_TAG cd11d22afbe2af350f24cd56e6e0536e5ca86452
        GIT_SUBMODULES_RECURSE TRUE
        PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/patch-wclap-bridge.cmake
    )
    FetchContent_MakeAvailable(wclap-bridge)
    # Real webview-gui module was dropped by the patch above; give wclap-bridge's
    # own sources the no-op stand-in on its include path instead.
    target_include_directories(wclap-bridge PRIVATE ${CMAKE_SOURCE_DIR}/src/vendor/webview-gui-stub)
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
    src/units/pangain.c
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
    src
)
target_include_directories(${PROJECT_NAME} PRIVATE ${clap_SOURCE_DIR}/include)
if(NOT EMSCRIPTEN)
    target_include_directories(${PROJECT_NAME} PRIVATE ${wclap-bridge_SOURCE_DIR}/include)
    # wclap-bridge is C++ (Wasmtime embedding); force the C++ linker for the final link.
    set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
endif()
if(WIN32)
    # MSVC's math.h doesn't define M_PI etc. without this.
    target_compile_definitions(${PROJECT_NAME} PRIVATE _USE_MATH_DEFINES)
    # NOGDI/NOUSER: raylib declares its own Rectangle/CloseWindow/ShowCursor,
    # which collide with WinGDI/WinUser's identically-named ones — as a
    # compile definition (not a local #define in one header) this applies
    # no matter which translation unit pulls in <windows.h> first (lock.h's
    # CRITICAL_SECTION is the only one left that does).
    target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOGDI NOUSER)
endif()

if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".mjs")
    target_link_options(${PROJECT_NAME} PRIVATE
        -sUSE_GLFW=3
        -sINITIAL_MEMORY=134217728  # 128MB headroom: patterns are lazily allocated (~114KB each), samples/soundfonts dominate
        -sALLOW_MEMORY_GROWTH=1
        # Emscripten 6.x flipped GROWABLE_ARRAYBUFFERS on by default, exposing the
        # heap as a resizable ArrayBuffer — WebGL rejects views of those
        # ("texImage2D: ... must not be resizable"). Pin the old replace-on-grow
        # behavior until emscripten's GL bindings handle resizable buffers.
        -sGROWABLE_ARRAYBUFFERS=0
        -sFORCE_FILESYSTEM=1
        -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']
        "-sEXPORTED_FUNCTIONS=['_main','_midi_in_web_set_port_count','_midi_in_web_set_port_name','_midi_in_web_push','_midi_out_web_set_port_count','_midi_out_web_set_port_name']"
        -sEXPORT_ES6=1
        -sMODULARIZE=1
        -sSINGLE_FILE=1
        -sNO_EXIT_RUNTIME=1
        -sASSERTIONS=0
        -sWASM=1
        --js-library "${CMAKE_SOURCE_DIR}/src/wclap_host_web.lib.js"
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE raylib)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE raylib)
    if(NOT WIN32)
        # MSVC has no separate libm — math functions are built into the CRT.
        target_link_libraries(${PROJECT_NAME} PRIVATE m)
    endif()
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
        target_link_libraries(${PROJECT_NAME} PRIVATE "-framework CoreMIDI" "-framework CoreFoundation")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "poketrack"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist.in"
        )
    endif()

    # Quick sanity tests (native only, not built by default): `make test`
    set(TEST_SOURCES ${SOURCES})
    list(REMOVE_ITEM TEST_SOURCES src/main.c)
    add_executable(poketrack_tests EXCLUDE_FROM_ALL ${TEST_SOURCES} tests/test_main.c)
    target_include_directories(poketrack_tests PRIVATE
        ${raylib_SOURCE_DIR}/src
        ${TSF_INCLUDE_DIR}
        ${MINIZ_INCLUDE_DIR}
        src
        ${clap_SOURCE_DIR}/include
        ${wclap-bridge_SOURCE_DIR}/include
    )
    set_target_properties(poketrack_tests PROPERTIES LINKER_LANGUAGE CXX)
    target_link_libraries(poketrack_tests PRIVATE raylib wclap-bridge)
    if(NOT WIN32)
        target_link_libraries(poketrack_tests PRIVATE m)
    endif()
    if(UNIX AND NOT APPLE AND ALSA_FOUND)
        target_compile_definitions(poketrack_tests PRIVATE HAVE_ALSA)
        target_include_directories(poketrack_tests PRIVATE ${ALSA_INCLUDE_DIRS})
        target_link_libraries(poketrack_tests PRIVATE ${ALSA_LIBRARIES})
    endif()
    if(WIN32)
        target_link_libraries(poketrack_tests PRIVATE winmm)
        target_compile_definitions(poketrack_tests PRIVATE _USE_MATH_DEFINES WIN32_LEAN_AND_MEAN NOGDI NOUSER)
    endif()
    if(APPLE)
        target_link_libraries(poketrack_tests PRIVATE "-framework CoreMIDI" "-framework CoreFoundation")
    endif()

    # Theme screenshot tool (native only, not built by default): renders a
    # fake pattern screen under each examples/themes/*.ptt and saves a PNG.
    # cmake --build build --target theme_shot
    set(THEME_SHOT_SOURCES ${SOURCES})
    list(REMOVE_ITEM THEME_SHOT_SOURCES src/main.c)
    add_executable(theme_shot EXCLUDE_FROM_ALL ${THEME_SHOT_SOURCES} tools/theme_shot.c)
    target_include_directories(theme_shot PRIVATE
        ${raylib_SOURCE_DIR}/src
        ${TSF_INCLUDE_DIR}
        ${MINIZ_INCLUDE_DIR}
        src
        ${clap_SOURCE_DIR}/include
        ${wclap-bridge_SOURCE_DIR}/include
    )
    set_target_properties(theme_shot PROPERTIES LINKER_LANGUAGE CXX)
    target_link_libraries(theme_shot PRIVATE raylib wclap-bridge)
    if(NOT WIN32)
        target_link_libraries(theme_shot PRIVATE m)
    endif()
    if(UNIX AND NOT APPLE AND ALSA_FOUND)
        target_compile_definitions(theme_shot PRIVATE HAVE_ALSA)
        target_include_directories(theme_shot PRIVATE ${ALSA_INCLUDE_DIRS})
        target_link_libraries(theme_shot PRIVATE ${ALSA_LIBRARIES})
    endif()
    if(WIN32)
        target_link_libraries(theme_shot PRIVATE winmm)
        target_compile_definitions(theme_shot PRIVATE _USE_MATH_DEFINES WIN32_LEAN_AND_MEAN NOGDI NOUSER)
    endif()
    if(APPLE)
        target_link_libraries(theme_shot PRIVATE "-framework CoreMIDI" "-framework CoreFoundation")
    endif()
endif()
