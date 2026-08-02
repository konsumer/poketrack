// Minimal, wclap-bridge-independent smoke test: calls the raw Wasmtime C API
// directly (engine + module compile only, no instantiation) to tell apart
// "Wasmtime itself crashes on this platform/environment" from "wclap-bridge
// misuses the API". Windows-only (see CMakeLists.txt) — this exists purely
// to chase down a Windows-only STATUS_ACCESS_VIOLATION on the first real
// Wasmtime call, reproduced via wclap-bridge; not part of the normal build.
#include <stdio.h>
#include <stdlib.h>

#include <wasm.h>
#include <wasmtime.h>

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "../examples/plugins/karp.wclap.wasm";
  FILE* f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "smoke: cannot open %s\n", path);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char* buf = (unsigned char*)malloc((size_t)sz);
  size_t nread = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  printf("smoke: read %zu bytes from %s\n", nread, path);
  fflush(stdout);

  printf("smoke: wasm_engine_new\n");
  fflush(stdout);
  wasm_engine_t* engine = wasm_engine_new();
  if (!engine) {
    fprintf(stderr, "smoke: wasm_engine_new failed\n");
    return 1;
  }
  printf("smoke: engine ok\n");
  fflush(stdout);

  wasmtime_module_t* module = NULL;
  printf("smoke: wasmtime_module_new\n");
  fflush(stdout);
  wasmtime_error_t* err = wasmtime_module_new(engine, buf, (size_t)sz, &module);
  printf("smoke: module_new returned, err=%p module=%p\n", (void*)err, (void*)module);
  fflush(stdout);
  if (err) {
    wasm_byte_vec_t msg;
    wasmtime_error_message(err, &msg);
    fprintf(stderr, "smoke: module_new error: %.*s\n", (int)msg.size, msg.data);
    wasmtime_error_delete(err);
    return 1;
  }

  printf("smoke: OK\n");
  wasmtime_module_delete(module);
  wasm_engine_delete(engine);
  free(buf);
  return 0;
}
