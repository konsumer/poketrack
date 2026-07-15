# Runs as wclap-bridge's FetchContent PATCH_COMMAND (cwd = its fetched
# source root). Drops the build of its real webview-gui (GTK+WebKit on
# Linux, WebKit.framework on macOS) — poketrack never hosts a CLAP plugin's
# GUI, and a hard GTK/WebKit runtime dependency is wrong for a
# framebuffer-only handheld build with no desktop installed. The include
# path gets pointed at src/vendor/webview-gui-stub instead (see
# CMakeLists.txt), a no-op stand-in with the same public API.
#
# Idempotent: re-running finds nothing to replace and is a silent no-op, so
# it's safe if FetchContent re-invokes this on a cache hit.

set(f "CMakeLists.txt")
if(NOT EXISTS "${f}")
  message(FATAL_ERROR "patch-wclap-bridge.cmake: ${f} not found — wclap-bridge layout may have changed")
endif()

file(READ "${f}" content)

string(REPLACE "add_subdirectory(\${CMAKE_CURRENT_LIST_DIR}/modules/webview-gui)\n" "" content "${content}")

string(REGEX REPLACE
  "if\\(NOT \\\${CMAKE_SYSTEM_NAME} STREQUAL \"Darwin\"\\)\n[^\n]*\n[^\n]*target_include_directories\\(webview-gui[^\n]*\n[^\n]*endif\\(\\)\n"
  ""
  content "${content}")

string(REPLACE
  "target_link_libraries(wclap-bridge PRIVATE wclap-cpp webview-gui)"
  "target_link_libraries(wclap-bridge PRIVATE wclap-cpp)"
  content "${content}")

file(WRITE "${f}" "${content}")
