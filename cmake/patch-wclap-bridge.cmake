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

# TEMPORARY diagnostic: narrow down exactly which sub-call inside
# InstanceGroup::setup() crashes with STATUS_ACCESS_VIOLATION on Windows CI
# (module compile itself already proven fine standalone via
# tools/wasmtime_smoke.c). Remove once found.
string(REPLACE
  "\tauto *error = wasmtime_module_new(globalWasmEngine, wasmBytes, wasmLength, &wtModule);\n\tif (error) {\n"
  "\tstd::cerr << \"DIAG: before module_new\\n\"; std::cerr.flush();\n\tauto *error = wasmtime_module_new(globalWasmEngine, wasmBytes, wasmLength, &wtModule);\n\tstd::cerr << \"DIAG: after module_new\\n\"; std::cerr.flush();\n\tif (error) {\n"
  wcontent "${wcontent}")

string(REPLACE
  "\t\twasm_exporttype_vec_t exportTypes;\n\t\twasmtime_module_exports(wtModule, &exportTypes);\n\t\t\n"
  "\t\twasm_exporttype_vec_t exportTypes;\n\t\tstd::cerr << \"DIAG: before module_exports\\n\"; std::cerr.flush();\n\t\twasmtime_module_exports(wtModule, &exportTypes);\n\t\tstd::cerr << \"DIAG: after module_exports\\n\"; std::cerr.flush();\n\t\t\n"
  wcontent "${wcontent}")

string(REPLACE
  "\t\twasm_exporttype_vec_delete(&exportTypes);\n\t}\n\tif (!foundClapEntry) return stopWithError(\"clap_entry not exported\");\n"
  "\t\tstd::cerr << \"DIAG: after exports loop\\n\"; std::cerr.flush();\n\t\twasm_exporttype_vec_delete(&exportTypes);\n\t}\n\tif (!foundClapEntry) return stopWithError(\"clap_entry not exported\");\n"
  wcontent "${wcontent}")

string(REPLACE
  "\t\twasm_importtype_vec_t importTypes;\n\t\twasmtime_module_imports(wtModule, &importTypes);\n\t\tfor (size_t i = 0; i < importTypes.size; ++i) {\n"
  "\t\twasm_importtype_vec_t importTypes;\n\t\tstd::cerr << \"DIAG: before module_imports\\n\"; std::cerr.flush();\n\t\twasmtime_module_imports(wtModule, &importTypes);\n\t\tstd::cerr << \"DIAG: after module_imports\\n\"; std::cerr.flush();\n\t\tfor (size_t i = 0; i < importTypes.size; ++i) {\n"
  wcontent "${wcontent}")

string(REPLACE
  "\t\twasm_importtype_vec_delete(&importTypes);\n\t}\n}\n"
  "\t\tstd::cerr << \"DIAG: after imports loop\\n\"; std::cerr.flush();\n\t\twasm_importtype_vec_delete(&importTypes);\n\t}\n}\n"
  wcontent "${wcontent}")

# InstanceGroup::setup() (module compile) is proven clean by the markers
# above — narrow down further into InstanceImpl::setup() (creates the
# store/linker/WASI config and instantiates the module), called from
# WclapModuleBase's constructor via InstanceGroup::startInstance().
string(REPLACE
  "bool wclap_wasmtime::InstanceImpl::setup() {\n\tif (group.hasError()) return false;\n"
  "bool wclap_wasmtime::InstanceImpl::setup() {\n\tstd::cerr << \"DIAG: InstanceImpl::setup begin\\n\"; std::cerr.flush();\n\tif (group.hasError()) return false;\n"
  wcontent "${wcontent}")

string(REPLACE
  "\t\tsetWasmDeadline();\n\t\twasm_trap_t *trap = nullptr;\n\t\tauto *error = wasmtime_linker_instantiate(wtLinker, wtContext, group.wtModule, &wtInstance, &trap);\n"
  "\t\tsetWasmDeadline();\n\t\twasm_trap_t *trap = nullptr;\n\t\tstd::cerr << \"DIAG: before linker_instantiate\\n\"; std::cerr.flush();\n\t\tauto *error = wasmtime_linker_instantiate(wtLinker, wtContext, group.wtModule, &wtInstance, &trap);\n\t\tstd::cerr << \"DIAG: after linker_instantiate\\n\"; std::cerr.flush();\n"
  wcontent "${wcontent}")

# instantiate itself is proven clean too — narrow into the post-instantiate
# export lookups (memory/clap_entry/malloc/function-table), still inside
# InstanceImpl::setup().
string(REPLACE
  "\tif (!updateClapEntry()) return false;\n\n\tif (!wasmtime_instance_export_get(wtContext, &wtInstance, \"malloc\", 6, &item)) {\n"
  "\tstd::cerr << \"DIAG: before updateClapEntry\\n\"; std::cerr.flush();\n\tif (!updateClapEntry()) return false;\n\tstd::cerr << \"DIAG: after updateClapEntry\\n\"; std::cerr.flush();\n\n\tif (!wasmtime_instance_export_get(wtContext, &wtInstance, \"malloc\", 6, &item)) {\n"
  wcontent "${wcontent}")

string(REPLACE
  "\twasmtime_extern_delete(&item);\n\n\t// Look for the first function table\n\tsize_t exportIndex = 0;\n"
  "\tstd::cerr << \"DIAG: after malloc lookup\\n\"; std::cerr.flush();\n\twasmtime_extern_delete(&item);\n\n\t// Look for the first function table\n\tsize_t exportIndex = 0;\n"
  wcontent "${wcontent}")

string(REPLACE
  "\tif (!foundTable) return stopWithError(\"couldn't find function table in WCLAP\");\n\treturn true;\n}\n"
  "\tstd::cerr << \"DIAG: after function table loop, foundTable=\" << foundTable << \"\\n\"; std::cerr.flush();\n\tif (!foundTable) return stopWithError(\"couldn't find function table in WCLAP\");\n\treturn true;\n}\n"
  wcontent "${wcontent}")

# setup() (store/linker/WASI/instantiate/exports) is now proven fully clean —
# the crash must be later, in wasiInit() (calls _initialize() — the actual
# first execution of the plugin's own JIT'd code) or the clap_entry.init()
# call after it, both invoked from WclapModule's constructor.
string(REPLACE
  "bool wclap_wasmtime::InstanceImpl::wasiInit() {\n"
  "bool wclap_wasmtime::InstanceImpl::wasiInit() {\n\tstd::cerr << \"DIAG: wasiInit begin\\n\"; std::cerr.flush();\n"
  wcontent "${wcontent}")

string(REPLACE
  "\t\tsetWasmDeadline();\n\t\twasm_trap_t *trap = nullptr;\n\t\tauto error = wasmtime_func_call(wtContext, &item.of.func, nullptr, 0, nullptr, 0, &trap);\n"
  "\t\tsetWasmDeadline();\n\t\twasm_trap_t *trap = nullptr;\n\t\tstd::cerr << \"DIAG: before _initialize call\\n\"; std::cerr.flush();\n\t\tauto error = wasmtime_func_call(wtContext, &item.of.func, nullptr, 0, nullptr, 0, &trap);\n\t\tstd::cerr << \"DIAG: after _initialize call\\n\"; std::cerr.flush();\n"
  wcontent "${wcontent}")

string(REPLACE
  "\tif (!updateClapEntry()) return false;\n\n\treturn true;\n}\n"
  "\tstd::cerr << \"DIAG: no _initialize export (or done), before 2nd updateClapEntry\\n\"; std::cerr.flush();\n\tif (!updateClapEntry()) return false;\n\tstd::cerr << \"DIAG: wasiInit end\\n\"; std::cerr.flush();\n\n\treturn true;\n}\n"
  wcontent "${wcontent}")

file(WRITE "${wf}" "${wcontent}")

# TEMPORARY diagnostic (continued): wasiInit()/_initialize() is proven clean
# too — narrow into WclapModule's constructor itself: addHostFunctions,
# mainThread->init(), bindGlobalArena() (copies host C++ structs into wasm
# linear memory — genuine MSVC-vs-wasm-ABI struct-layout risk), and the
# final clap_entry.init() call (included via ../wclap-module.h, included by
# wclap-bridge.cpp which already has <iostream>).
set(mf "source/_generic/wclap-module.h")
if(NOT EXISTS "${mf}")
  message(FATAL_ERROR "patch-wclap-bridge.cmake: ${mf} not found — wclap-bridge layout may have changed")
endif()

file(READ "${mf}" mcontent)

string(REPLACE
  "\t\tif (hasError) return; // base class failed\n\t\tif (!addHostFunctions(mainThread.get())) return;\n\t\t\n"
  "\t\tstd::cerr << \"DIAG: WclapModule ctor begin\\n\"; std::cerr.flush();\n\t\tif (hasError) return; // base class failed\n\t\tstd::cerr << \"DIAG: before addHostFunctions\\n\"; std::cerr.flush();\n\t\tif (!addHostFunctions(mainThread.get())) return;\n\t\tstd::cerr << \"DIAG: after addHostFunctions\\n\"; std::cerr.flush();\n\t\t\n"
  mcontent "${mcontent}")

string(REPLACE
  "\t\tmainThread->init();\n"
  "\t\tstd::cerr << \"DIAG: before mainThread init\\n\"; std::cerr.flush();\n\t\tmainThread->init();\n\t\tstd::cerr << \"DIAG: after mainThread init\\n\"; std::cerr.flush();\n"
  mcontent "${mcontent}")

string(REPLACE
  "\t\tbindGlobalArena();\n\t\t\n\t\tauto scoped = arenaPool.scoped();\n"
  "\t\tstd::cerr << \"DIAG: before bindGlobalArena\\n\"; std::cerr.flush();\n\t\tbindGlobalArena();\n\t\tstd::cerr << \"DIAG: after bindGlobalArena\\n\"; std::cerr.flush();\n\t\t\n\t\tauto scoped = arenaPool.scoped();\n"
  mcontent "${mcontent}")

string(REPLACE
  "\t\tif (!mainThread->call(entryPtr[&wclap_plugin_entry::init], pathStr)) {\n"
  "\t\tstd::cerr << \"DIAG: before clap_entry.init call\\n\"; std::cerr.flush();\n\t\tif (!mainThread->call(entryPtr[&wclap_plugin_entry::init], pathStr)) {\n"
  mcontent "${mcontent}")

string(REPLACE
  "\t\t\n\t\thasError = false;\n\t}\n"
  "\t\tstd::cerr << \"DIAG: after clap_entry.init call\\n\"; std::cerr.flush();\n\t\t\n\t\thasError = false;\n\t\tstd::cerr << \"DIAG: WclapModule ctor end\\n\"; std::cerr.flush();\n\t}\n"
  mcontent "${mcontent}")

string(REPLACE
  "\tbool bindGlobalArena() {\n\t\tauto scoped = arenaPool.scoped();\n\t\t\n\t\t// The global arena holds all the extensions, for the lifetime of the module\n\t\thostAmbisonicPtr = scoped.copyAcross(hostAmbisonic);\n\t\thostAudioPortsConfigPtr = scoped.copyAcross(hostAudioPortsConfig);\n\t\thostAudioPortsPtr = scoped.copyAcross(hostAudioPorts);\n"
  "\tbool bindGlobalArena() {\n\t\tstd::cerr << \"DIAG: bindGlobalArena begin\\n\"; std::cerr.flush();\n\t\tauto scoped = arenaPool.scoped();\n\t\tstd::cerr << \"DIAG: arena scoped ok\\n\"; std::cerr.flush();\n\t\t\n\t\t// The global arena holds all the extensions, for the lifetime of the module\n\t\thostAmbisonicPtr = scoped.copyAcross(hostAmbisonic);\n\t\tstd::cerr << \"DIAG: after copy 1\\n\"; std::cerr.flush();\n\t\thostAudioPortsConfigPtr = scoped.copyAcross(hostAudioPortsConfig);\n\t\thostAudioPortsPtr = scoped.copyAcross(hostAudioPorts);\n\t\tstd::cerr << \"DIAG: after copy 3\\n\"; std::cerr.flush();\n"
  mcontent "${mcontent}")

string(REPLACE
  "\t\thostTimerSupportPtr = scoped.copyAcross(hostTimerSupport);\n\t\t// need to be able to point to these constants\n"
  "\t\thostTimerSupportPtr = scoped.copyAcross(hostTimerSupport);\n\t\tstd::cerr << \"DIAG: after copy ~15\\n\"; std::cerr.flush();\n\t\t// need to be able to point to these constants\n"
  mcontent "${mcontent}")

string(REPLACE
  "\t\thostVoiceInfoPtr = scoped.copyAcross(hostVoiceInfo);\n\t\t\n\t\thostWebviewPtr = scoped.copyAcross(hostWebview);\n\t\t\n\t\tglobalArena = scoped.commit();\n\t\treturn true;\n\t}\n"
  "\t\thostVoiceInfoPtr = scoped.copyAcross(hostVoiceInfo);\n\t\tstd::cerr << \"DIAG: after copy ~20 (voiceinfo)\\n\"; std::cerr.flush();\n\t\t\n\t\thostWebviewPtr = scoped.copyAcross(hostWebview);\n\t\tstd::cerr << \"DIAG: after webview copy\\n\"; std::cerr.flush();\n\t\t\n\t\tglobalArena = scoped.commit();\n\t\tstd::cerr << \"DIAG: bindGlobalArena commit done\\n\"; std::cerr.flush();\n\t\treturn true;\n\t}\n"
  mcontent "${mcontent}")

file(WRITE "${mf}" "${mcontent}")

# Windows only: link wasmtime.dll.lib (the DLL's import lib) instead of the
# static wasmtime.lib. wasm.h/wasi.h unconditionally declare every export as
# __declspec(dllimport) on Windows; combined with the STATIC .lib that
# produces a build that links fine (blanking WASM_API_EXTERN/WASI_API_EXTERN
# papers over the mismatch at compile time) but crashes with
# STATUS_ACCESS_VIOLATION on the very first real call into the library at
# runtime — reproduced both locally and on Windows CI, same failure mode
# reported in https://github.com/ashtonmeuser/godot-wasm/issues/65.
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
