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

# Stop forwarding a loaded WCLAP's WASI stdout to poketrack's own stdout.
# Guest plugins can end up with debug console.log() calls compiled in
# (e.g. as-clap's clap_entry.init()/pluginInit() tracing, used by the
# karplus plugin's AssemblyScript build) — with stdout inherited, every
# plugin (re)instantiation spams that straight into poketrack's own
# console. stderr stays inherited so real WASM traps/errors still surface.
set(wf "source/wclap-instance-wasmtime/wclap-instance-wasmtime.cpp")
if(NOT EXISTS "${wf}")
  message(FATAL_ERROR "patch-wclap-bridge.cmake: ${wf} not found — wclap-bridge layout may have changed")
endif()

file(READ "${wf}" wcontent)

string(REPLACE
  "wasi_config_inherit_stdout(wasiConfig);\n"
  ""
  wcontent "${wcontent}")

file(WRITE "${wf}" "${wcontent}")

# Windows-only crash fix: MemoryArenaPool::scoped() returned
# `{*arena, std::move(arena)}` — dereferencing the arena and moving its
# owning unique_ptr as two elements of the same braced-init-list. Under
# MSVC this reproducibly crashed with STATUS_ACCESS_VIOLATION the first
# time any WCLAP plugin was loaded (traced via bisection: every step up to
# and including this exact statement runs clean, and every step after it
# never runs at all). Explicitly sequencing the dereference into its own
# statement before the move — still an unnamed prvalue construction, so
# Scoped's guaranteed copy elision (it has no copy/move constructor) is
# unaffected — makes the crash go away entirely, confirmed on Windows CI.
set(af "modules/wclap-cpp/include/wclap/memory-arena.hpp")
if(NOT EXISTS "${af}")
  message(FATAL_ERROR "patch-wclap-bridge.cmake: ${af} not found — wclap-bridge layout may have changed")
endif()

file(READ "${af}" acontent)

string(REPLACE
  "\ttypename Arena::Scoped scoped() {\n\t\tauto arena = getOrCreate();\n\t\treturn {*arena, std::move(arena)};\n\t}\n"
  "\ttypename Arena::Scoped scoped() {\n\t\tauto arena = getOrCreate();\n\t\tArena &arenaRef = *arena;\n\t\treturn {arenaRef, std::move(arena)};\n\t}\n"
  acontent "${acontent}")

file(WRITE "${af}" "${acontent}")

# Windows only: link wasmtime.dll.lib (the DLL's import lib) instead of the
# static wasmtime.lib. wasm.h/wasi.h unconditionally declare every export as
# __declspec(dllimport) on Windows; combined with the STATIC .lib that
# produces a build that links fine (blanking WASM_API_EXTERN/WASI_API_EXTERN
# papers over the mismatch at compile time) but is a documented source of
# runtime crashes — see https://github.com/ashtonmeuser/godot-wasm/issues/65.
# wasmtime.dll.lib ships in the same release zip as wasmtime.lib already
# fetched, so this needs no new download — just linking the other file in it,
# and (via CMakeLists.txt) copying wasmtime.dll next to the built .exe.
set(wtf "wasmtime-fetched.cmake")
if(NOT EXISTS "${wtf}")
  message(FATAL_ERROR "patch-wclap-bridge.cmake: ${wtf} not found — wclap-bridge layout may have changed")
endif()

file(READ "${wtf}" wtcontent)

string(REPLACE
  "\ttarget_link_libraries(wasmtime INTERFACE \"\${wasmtime-c-api_SOURCE_DIR}/lib/wasmtime.lib\")\n\t# as suggested in wasmtime.h for static linking on Windows\n\ttarget_compile_definitions(wasmtime INTERFACE WASM_API_EXTERN= WASI_API_EXTERN=)\n"
  "\ttarget_link_libraries(wasmtime INTERFACE \"\${wasmtime-c-api_SOURCE_DIR}/lib/wasmtime.dll.lib\")\n"
  wtcontent "${wtcontent}")

file(WRITE "${wtf}" "${wtcontent}")
