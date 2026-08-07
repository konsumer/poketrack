// Web WCLAP host: loads CLAP-compiled-to-wasm32 plugins using the browser's
// own WebAssembly.instantiate — no separate wasm engine needed, since
// poketrack itself already runs as wasm in the same JS environment.
//
// A CLAP plugin's "host" struct contains function pointers the plugin calls
// back into. In wasm those are function-table indices, so calling into them
// means installing our own entries in the plugin's own exported `table`.
// Since JS functions can't be table entries directly, two tiny fixed shim
// wasm modules (compiled ahead of time from shim.wat/events_shim.wat)
// forward table calls to a single JS dispatch function. Struct field offsets
// below are dumped directly from wclap-generic.hpp (see offsets.cpp in the
// implementation notes) — this only supports the wclap32 (32-bit) ABI.
//
// Threading: some wasi-sdk builds (e.g. signalsmith-clap-cpp, built with the
// `-pthread` wasi-sdk toolchain) import `wasi."thread-spawn"` and spawn real
// threads during `_initialize()` or plugin init. A single-threaded host can't
// satisfy that — the plugin blocks forever waiting for a thread that never
// runs, which is the "hangs on load" failure this file works around. See
// $WCLAP_MAKE_HOST_ENV / $WCLAP_THREADS / $WCLAP_THREAD_WORKER_SRC below,
// and plugins/README.md for the browser requirements (COOP/COEP — worked
// around on GitHub Pages by webroot/coi-serviceworker.js) and the
// wasi-threads ABI this implements.
mergeInto(LibraryManager.library, {
  $WCLAP_OFF: {
    entry: { init: 12, get_factory: 20 },
    factory: { get_plugin_count: 0, get_plugin_descriptor: 4, create_plugin: 8 },
    descriptor: { id: 12, name: 16 },
    plugin: { init: 8, activate: 16, start_processing: 24, process: 36, get_extension: 40, on_main_thread: 44 },
    host: { size: 48, host_data: 12, name: 16, vendor: 20, url: 24, version: 28,
            get_extension: 32, request_restart: 36, request_process: 40, request_callback: 44 },
    audio_buffer: { size: 24, data32: 0, channel_count: 8 },
    process: { size: 40, steady_time: 0, frames_count: 8, transport: 12, audio_inputs: 16,
               audio_outputs: 20, audio_inputs_count: 24, audio_outputs_count: 28, in_events: 32, out_events: 36 },
    input_events: { size: 12, ctx: 0, size_fn: 4, get: 8 },
    output_events: { size: 8, ctx: 0, try_push: 4 },
    event_header: { size: 16, sz: 0, time: 4, space_id: 8, type: 10, flags: 12 },
    event_note: { size: 40, note_id: 16, port_index: 20, channel: 22, key: 24, velocity: 32 },
    event_param_value: { size: 48, param_id: 16, note_id: 24, port_index: 28, channel: 30, key: 32, value: 40 },
    param_info: { size: 1320, id: 0, flags: 4, name: 12, min_value: 1296, max_value: 1304, default_value: 1312 },
    plugin_params: { count: 0, get_info: 4 },
    plugin_note_ports: { count: 0, get: 4 },
    host_log: { size: 4, log: 0 },
  },
  $WCLAP_INSTANCES: [null], // index 0 reserved (== NULL handle)

  // Host-bridge shim (get_extension/request_restart/request_process/request_callback/
  // params_rescan/params_clear/params_request_flush/host_log), compiled from a tiny
  // fixed .wat that forwards table calls to a single JS `dispatch` import. Shared by
  // the main thread instantiation and every worker thread instantiation of the same
  // plugin module, so a function-table index built on one thread means the same thing
  // on every other thread (see $WCLAP_THREAD_WORKER_SRC for why that alignment holds).
  // NOTE: value must be a function (not a bare string) — Emscripten's JS-library
  // preprocessor splices bare-string `$name` values in as raw source rather than
  // treating them as string literals, which would corrupt this base64 blob.
  $WCLAP_SHIM_B64: function () {
    return 'AGFzbQEAAAABHgVgBH9/f38Bf2ACf38Bf2ABfwBgAn9/AGADf39/AAIQAQNlbnYIZGlzcGF0Y2gAAAMJCAECAgIDBAIEB4kBCA1nZXRfZXh0ZW5zaW9uAAEPcmVxdWVzdF9yZXN0YXJ0AAIPcmVxdWVzdF9wcm9jZXNzAAMQcmVxdWVzdF9jYWxsYmFjawAEDXBhcmFtc19yZXNjYW4ABQxwYXJhbXNfY2xlYXIABhRwYXJhbXNfcmVxdWVzdF9mbHVzaAAHCGhvc3RfbG9nAAgKcAgMAEEAIAAgAUEAEAALDQBBASAAQQBBABAAGgsNAEECIABBAEEAEAAaCw0AQQMgAEEAQQAQABoLDQBBBCAAIAFBABAAGgsNAEEFIAAgASACEAAaCw0AQQYgAEEAQQAQABoLDQBBByAAIAEgAhAAGgs=';
  },

  // Standalone bootstrap for a wasi-threads worker thread. Loaded as a Blob URL by
  // $WCLAP_THREADS.spawn(). Each thread gets its own instantiation of the SAME
  // WebAssembly.Module, sharing the plugin's linear memory (a shared
  // WebAssembly.Memory, structured-cloned in) but with its own private function
  // table — deterministic because table contents come entirely from the module's
  // active element segments, fixed at instantiation time, so every instantiation of
  // the same module lays its table out identically (including the +8 host-bridge
  // shim slots we append right after instantiate, at the same base offset every
  // time). Per the wasi-threads convention, secondary threads must NOT re-run
  // `_initialize` (global ctors already ran on the spawning thread and that state
  // lives in the now-shared memory) — they call `wasi_thread_start(tid, arg)`
  // directly.
  // NOTE: value must be a function returning the source string (not a bare
  // template literal) — see the note on $WCLAP_SHIM_B64 above for why.
  $WCLAP_THREAD_WORKER_SRC: function () {
    return `
self.onmessage = function (e) {
  var msg = e.data;
  var mem = msg.memory;
  var module = msg.module;

  function u8 () { return new Uint8Array(mem.buffer); }
  function readCStr (ptr) {
    if (!ptr) return '';
    var b = u8(), end = ptr, limit = Math.min(b.length, ptr + 65536);
    while (end < limit && b[end] !== 0) end++;
    // .slice() (not .subarray()) — TextDecoder refuses a view directly over a
    // SharedArrayBuffer-backed memory (throws "must not be shared"); a copy is fine.
    return new TextDecoder().decode(b.slice(ptr, end));
  }

  var exp;
  function malloc (n) { return exp.malloc(n); }
  function writeCStr (str) {
    var utf8 = new TextEncoder().encode(str + '\\0');
    var ptr = malloc(utf8.length);
    u8().set(utf8, ptr);
    return ptr;
  }

  var hostFnIdx = {};
  function dispatch (id, a, b, c) {
    switch (id) {
      case 0: { // get_extension — pure function of table indices, safe to recompute per-thread
        var extId = readCStr(a);
        if (extId === 'clap.params') {
          var p = malloc(24);
          new DataView(mem.buffer).setUint32(p + 0, hostFnIdx.params_rescan, true);
          new DataView(mem.buffer).setUint32(p + 4, hostFnIdx.params_clear, true);
          new DataView(mem.buffer).setUint32(p + 8, hostFnIdx.params_request_flush, true);
          return p;
        }
        if (extId === 'clap.log') {
          var lp = malloc(4);
          new DataView(mem.buffer).setUint32(lp, hostFnIdx.host_log, true);
          return lp;
        }
        return 0;
      }
      case 3: self.postMessage({ type: 'request-callback' }); return 0; // request_callback
      case 7: console.log('[WCLAP thread ' + msg.threadId + ']', readCStr(c)); return 0; // log
      default: return 0;
    }
  }

  function threadSpawn (startArg) {
    var tid = Atomics.add(new Int32Array(msg.tidBuf), 0, 1) + 1;
    try {
      var w = new Worker(msg.workerUrl);
      w.onerror = function (err) { console.error('[WCLAP] thread', tid, 'worker error:', err.message); };
      w.onmessage = function (ev) {
        if (ev.data && ev.data.type === 'thread-error') console.error('[WCLAP] thread', ev.data.threadId, 'failed:', ev.data.error);
      };
      w.postMessage(Object.assign({}, msg, { threadId: tid, startArg: startArg }));
    } catch (err) {
      console.error('[WCLAP] nested thread-spawn failed:', err);
      return -1;
    }
    return tid;
  }

  var raw;
  try {
    var importObj = {
      env: { memory: mem },
      wasi_snapshot_preview1: {
        fd_write: function (fd, iovs, iovsLen, nwritten) {
          var total = 0, out = '';
          var view = new DataView(mem.buffer);
          for (var i = 0; i < iovsLen; i++) {
            var base = view.getUint32(iovs + i * 8, true);
            var len = view.getUint32(iovs + i * 8 + 4, true);
            out += new TextDecoder().decode(u8().slice(base, base + len));
            total += len;
          }
          console.log(out.replace(/\\n$/, ''));
          view.setUint32(nwritten, total, true);
          return 0;
        },
        proc_exit: function () { throw new Error('wclap thread called proc_exit'); },
        environ_sizes_get: function (cPtr, bPtr) {
          var v = new DataView(mem.buffer); v.setUint32(cPtr, 0, true); v.setUint32(bPtr, 0, true); return 0;
        },
        environ_get: function () { return 0; },
        clock_time_get: function (id, precision, outPtr) {
          new DataView(mem.buffer).setBigUint64(outPtr, BigInt(Math.floor(performance.now() * 1e6)), true); return 0;
        },
        fd_close: function () { return 8; },
        fd_fdstat_get: function () { return 8; },
        fd_prestat_get: function () { return 8; },
        fd_prestat_dir_name: function () { return 8; },
        fd_read: function () { return 8; },
        fd_seek: function () { return 8; },
        sched_yield: function () { return 0; },
        random_get: function (ptr, len) {
          var b = u8(); for (var i = 0; i < len; i++) b[ptr + i] = (Math.random() * 256) | 0; return 0;
        },
      },
      wasi: { 'thread-spawn': threadSpawn },
    };
    raw = new WebAssembly.Instance(module, importObj);
  } catch (err) {
    console.error('[WCLAP] thread', msg.threadId, 'instantiate failed:', err);
    self.postMessage({ type: 'thread-error', threadId: msg.threadId, error: String(err) });
    return;
  }
  exp = raw.exports;

  if (msg.shimB64) {
    var table = exp.table || exp.__indirect_function_table;
    var shimBytes = Uint8Array.from(atob(msg.shimB64), function (c) { return c.charCodeAt(0); });
    var shimInst = new WebAssembly.Instance(new WebAssembly.Module(shimBytes), { env: { dispatch: dispatch } });
    var base = table.length;
    table.grow(8);
    ['get_extension', 'request_restart', 'request_process', 'request_callback',
     'params_rescan', 'params_clear', 'params_request_flush', 'host_log'].forEach(function (n, i) {
      table.set(base + i, shimInst.exports[n]);
      hostFnIdx[n] = base + i;
    });
  }

  try {
    exp.wasi_thread_start(msg.threadId, msg.startArg);
  } catch (err) {
    console.error('[WCLAP] thread', msg.threadId, 'crashed:', err);
    self.postMessage({ type: 'thread-error', threadId: msg.threadId, error: String(err) });
    return;
  }
  self.postMessage({ type: 'thread-exit', threadId: msg.threadId });
  close();
};
`;
  },

  $WCLAP_THREADS__deps: ['$WCLAP_THREAD_WORKER_SRC'],
  $WCLAP_THREADS: {
    _workerUrl: null,
    getWorkerUrl: function () {
      if (!WCLAP_THREADS._workerUrl) {
        var blob = new Blob([WCLAP_THREAD_WORKER_SRC()], { type: 'application/javascript' });
        WCLAP_THREADS._workerUrl = URL.createObjectURL(blob);
      }
      return WCLAP_THREADS._workerUrl;
    },
    // Spawns a real thread for a wasi-threads plugin. `shimB64` may be '' when no
    // CLAP host struct exists yet (e.g. during wclap_web_list_plugins, before any
    // plugin is created) — the spawned thread then gets no host-bridge table
    // entries, which is fine since it has no host pointer to call them through.
    spawn: function (module, mem, tidBuf, shimB64, tid, startArg, workersOut) {
      var url = WCLAP_THREADS.getWorkerUrl();
      var w = new Worker(url);
      w.onerror = function (err) { console.error('[WCLAP] thread', tid, 'worker error:', err.message); };
      w.onmessage = function (ev) {
        if (ev.data && ev.data.type === 'thread-error') console.error('[WCLAP] thread', ev.data.threadId, 'failed:', ev.data.error);
      };
      w.postMessage({ module: module, memory: mem, tidBuf: tidBuf, shimB64: shimB64, workerUrl: url, threadId: tid, startArg: startArg });
      workersOut.push(w);
    },
  },

  // Builds the import object needed to instantiate a plugin module: detects whether
  // it needs shared memory / real threads (wasi-threads: imports `wasi."thread-spawn"`,
  // exports `wasi_thread_start`) and wires up worker-backed thread spawning if so.
  //
  // A shared `env.memory` import alone doesn't require cross-origin isolation — Chrome
  // happily constructs and uses a shared WebAssembly.Memory within a single thread
  // without it (this is the "-pthread toolchain used, but the plugin never actually
  // spawns a thread" case the wasi-sdk C++ builds hit). Cross-origin isolation is only
  // required to transfer that memory to a Worker (`postMessage` throws a synchronous
  // DataCloneError without it) and to construct the SharedArrayBuffer we use for tid
  // bookkeeping — i.e. only plugins that actually import `wasi."thread-spawn"` need it.
  // Returns null (after logging why) in that case, so a plugin whose thread-spawn we
  // can't ever satisfy fails the load up front instead of instantiating into a state
  // where it silently can't get the threads it's about to wait on.
  $WCLAP_MAKE_HOST_ENV__deps: ['$WCLAP_THREADS'],
  $WCLAP_MAKE_HOST_ENV: function (module, shimB64) {
    var needsMemImport = WebAssembly.Module.imports(module).some(function (imp) {
      return imp.module === 'env' && imp.kind === 'memory';
    });
    var needsThreads = WebAssembly.Module.imports(module).some(function (imp) {
      return imp.module === 'wasi' && imp.name === 'thread-spawn';
    });

    if (needsThreads && !globalThis.crossOriginIsolated) {
      console.error('[wclap] plugin declares real threads (imports wasi."thread-spawn"), ' +
        'but this page is not cross-origin isolated (COOP/COEP unavailable) — threads ' +
        'cannot be spawned, so the plugin cannot load. See plugins/README.md.');
      return null;
    }

    var mem = null;
    function u8 () { return new Uint8Array(mem.buffer); }

    var envImports = {};
    if (needsMemImport) envImports.memory = new WebAssembly.Memory({ initial: 256, maximum: 16384, shared: true });

    var wasiImports = {
      fd_write: function (fd, iovs, iovsLen, nwritten) {
        var total = 0, out = '';
        var view = new DataView(mem.buffer);
        for (var i = 0; i < iovsLen; i++) {
          var base = view.getUint32(iovs + i * 8, true);
          var len = view.getUint32(iovs + i * 8 + 4, true);
          // .slice() — TextDecoder (used inside UTF8ArrayToString for strings >16
          // bytes) refuses a view directly over a SharedArrayBuffer-backed memory.
          out += UTF8ArrayToString(u8().slice(base, base + len), 0, len);
          total += len;
        }
        console.log(out.replace(/\n$/, ''));
        view.setUint32(nwritten, total, true);
        return 0;
      },
      proc_exit: function () { throw new Error('wclap plugin called proc_exit'); },
      environ_sizes_get: function (countPtr, bufSizePtr) {
        new DataView(mem.buffer).setUint32(countPtr, 0, true);
        new DataView(mem.buffer).setUint32(bufSizePtr, 0, true);
        return 0;
      },
      environ_get: function () { return 0; },
      clock_time_get: function (id, precision, outPtr) {
        new DataView(mem.buffer).setBigUint64(outPtr, BigInt(Math.floor(performance.now() * 1e6)), true);
        return 0;
      },
      fd_close: function () { return 8; },
      fd_fdstat_get: function () { return 8; },
      fd_prestat_get: function () { return 8; },
      fd_prestat_dir_name: function () { return 8; },
      fd_read: function () { return 8; },
      fd_seek: function () { return 8; },
      sched_yield: function () { return 0; },
      random_get: function (ptr, len) {
        var bytes = u8();
        for (var i = 0; i < len; i++) bytes[ptr + i] = (Math.random() * 256) | 0;
        return 0;
      },
    };

    var workers = [];
    var threadImports = null;
    if (needsThreads) {
      var tidBuf = new SharedArrayBuffer(4);
      var tidArr = new Int32Array(tidBuf);
      threadImports = {
        // wasi-threads: returns the new tid, or a negative value on failure — a
        // well-behaved plugin checks this rather than assuming success, so prefer
        // reporting failure here over letting a spawn error abort the whole load.
        'thread-spawn': function (startArg) {
          var tid = Atomics.add(tidArr, 0, 1) + 1;
          try {
            WCLAP_THREADS.spawn(module, mem, tidBuf, shimB64 || '', tid, startArg, workers);
          } catch (err) {
            console.error('[wclap] thread-spawn failed:', err);
            return -1;
          }
          return tid;
        },
      };
    }

    return {
      envImports: envImports,
      wasiImports: wasiImports,
      threadImports: threadImports,
      workers: workers,
      setMem: function (m) { mem = m; },
    };
  },

  wclap_web_load__deps: ['$WCLAP_OFF', '$WCLAP_INSTANCES', '$WCLAP_MAKE_HOST_ENV', '$WCLAP_SHIM_B64'],
  wclap_web_load: function (pathPtr, pluginIdPtr, sampleRate, blockSize, outNamePtr, outNameSz, outIsInstrumentPtr) {
    var path = UTF8ToString(pathPtr);
    var wantedId = pluginIdPtr ? UTF8ToString(pluginIdPtr) : null;
    var bytes;
    try {
      bytes = FS.readFile(path);
    } catch (e) {
      console.error('wclap: cannot read', path, e);
      return 0;
    }

    var O = WCLAP_OFF;
    var mem, table, exp;
    function u8() { return new Uint8Array(mem.buffer); }
    var dv = {
      g32: function (o) { return new DataView(mem.buffer).getUint32(o, true); },
      s32: function (o, v) { new DataView(mem.buffer).setUint32(o, v, true); },
      s16: function (o, v) { new DataView(mem.buffer).setInt16(o, v, true); },
      su16: function (o, v) { new DataView(mem.buffer).setUint16(o, v, true); },
      sf64: function (o, v) { new DataView(mem.buffer).setFloat64(o, v, true); },
      sbig: function (o, v) { new DataView(mem.buffer).setBigInt64(o, v, true); },
    };
    function readCStr(ptr) {
      if (!ptr) return '';
      // Cap the scan — a bad/garbage pointer from a misbehaving plugin must
      // not hang the host waiting for a null terminator that's never there.
      var b = u8(), end = ptr, limit = Math.min(b.length, ptr + 65536);
      while (end < limit && b[end] !== 0) end++;
      // .slice() — TextDecoder (used inside UTF8ArrayToString for strings >16
      // bytes) refuses a view directly over a SharedArrayBuffer-backed memory,
      // which every plugin id/name read here can be for a threading-capable plugin.
      return UTF8ArrayToString(b.slice(ptr, end), 0, end - ptr);
    }
    function writeCStr(str) {
      var utf8 = new TextEncoder().encode(str + '\0');
      var ptr = exp.malloc(utf8.length);
      u8().set(utf8, ptr);
      return ptr;
    }
    function malloc(n) { return exp.malloc(n); }

    var inst;
    try {
      var module = new WebAssembly.Module(bytes);

      var hostEnv = WCLAP_MAKE_HOST_ENV(module, WCLAP_SHIM_B64());
      if (!hostEnv) return 0; // logged inside — needs shared memory but not cross-origin isolated

      var pendingMainThread = false;
      var hostFnIdx = {};
      function dispatch(id, a, b, c) {
        switch (id) {
          case 0: { // get_extension(host, extIdPtr)
            var extId = readCStr(a);
            if (extId === 'clap.params') {
              var p = malloc(24);
              dv.s32(p + 0, hostFnIdx.params_rescan);
              dv.s32(p + 4, hostFnIdx.params_clear);
              dv.s32(p + 8, hostFnIdx.params_request_flush);
              return p;
            }
            if (extId === 'clap.log') {
              var lp = malloc(O.host_log.size);
              dv.s32(lp + O.host_log.log, hostFnIdx.host_log);
              return lp;
            }
            return 0;
          }
          case 3: pendingMainThread = true; return 0; // request_callback
          case 7: console.log('[WCLAP]', readCStr(c)); return 0; // log
          default: return 0;
        }
      }

      var raw = new WebAssembly.Instance(module, {
        env: hostEnv.envImports,
        wasi_snapshot_preview1: hostEnv.wasiImports,
        wasi: hostEnv.threadImports || {},
      });
      exp = raw.exports;
      mem = hostEnv.envImports.memory || exp.memory;
      hostEnv.setMem(mem);
      table = exp.table || exp.__indirect_function_table;
      if (exp._initialize) exp._initialize();

      var shimBytes = Uint8Array.from(atob(WCLAP_SHIM_B64()), function (c) { return c.charCodeAt(0); });
      var shimInst = new WebAssembly.Instance(new WebAssembly.Module(shimBytes), { env: { dispatch: dispatch } });
      var base = table.length;
      table.grow(8);
      ['get_extension', 'request_restart', 'request_process', 'request_callback',
       'params_rescan', 'params_clear', 'params_request_flush', 'host_log'].forEach(function (n, i) {
        table.set(base + i, shimInst.exports[n]);
        hostFnIdx[n] = base + i;
      });

      var entryPtr = exp.clap_entry.value;
      var initIdx = dv.g32(entryPtr + O.entry.init);
      var getFactoryIdx = dv.g32(entryPtr + O.entry.get_factory);
      if (!table.get(initIdx)(writeCStr(path))) throw new Error('clap_entry.init failed');

      var factoryPtr = table.get(getFactoryIdx)(writeCStr('clap.plugin-factory'));
      if (!factoryPtr) throw new Error('no plugin factory');
      var getCountIdx = dv.g32(factoryPtr + O.factory.get_plugin_count);
      var getDescIdx = dv.g32(factoryPtr + O.factory.get_plugin_descriptor);
      var createIdx = dv.g32(factoryPtr + O.factory.create_plugin);

      var count = table.get(getCountIdx)(factoryPtr);
      var descPtr = 0;
      for (var i = 0; i < count; i++) {
        var d = table.get(getDescIdx)(factoryPtr, i);
        var id = readCStr(dv.g32(d + O.descriptor.id));
        if (!wantedId || id === wantedId) { descPtr = d; break; }
      }
      if (!descPtr) throw new Error('plugin id "' + wantedId + '" not found');
      var idPtr = dv.g32(descPtr + O.descriptor.id);
      var namePtr = dv.g32(descPtr + O.descriptor.name);
      var pluginName = readCStr(namePtr);

      var hostPtr = malloc(O.host.size);
      dv.s32(hostPtr + O.host.host_data, 0);
      dv.s32(hostPtr + O.host.name, writeCStr('poketrack'));
      dv.s32(hostPtr + O.host.vendor, writeCStr('poketrack'));
      dv.s32(hostPtr + O.host.url, writeCStr(''));
      dv.s32(hostPtr + O.host.version, writeCStr('0.1.0'));
      dv.s32(hostPtr + O.host.get_extension, hostFnIdx.get_extension);
      dv.s32(hostPtr + O.host.request_restart, hostFnIdx.request_restart);
      dv.s32(hostPtr + O.host.request_process, hostFnIdx.request_process);
      dv.s32(hostPtr + O.host.request_callback, hostFnIdx.request_callback);

      var pluginPtr = table.get(createIdx)(factoryPtr, hostPtr, idPtr);
      if (!pluginPtr) throw new Error('create_plugin failed');
      var pInitIdx = dv.g32(pluginPtr + O.plugin.init);
      if (!table.get(pInitIdx)(pluginPtr)) throw new Error('plugin.init failed');
      var pActivateIdx = dv.g32(pluginPtr + O.plugin.activate);
      if (!table.get(pActivateIdx)(pluginPtr, sampleRate, 1, blockSize)) throw new Error('plugin.activate failed');
      table.get(dv.g32(pluginPtr + O.plugin.start_processing))(pluginPtr);

      var pGetExtIdx = dv.g32(pluginPtr + O.plugin.get_extension);
      var isInstrument = false;
      var notePortsExtPtr = table.get(pGetExtIdx)(pluginPtr, writeCStr('clap.note-ports'));
      if (notePortsExtPtr) {
        var npCountIdx = dv.g32(notePortsExtPtr + O.plugin_note_ports.count);
        isInstrument = table.get(npCountIdx)(pluginPtr, 1) > 0;
      }

      // param cache (mirrors native backend's UnitState.param_cache)
      var paramCache = [];
      var paramsExtPtr = table.get(pGetExtIdx)(pluginPtr, writeCStr('clap.params'));
      if (paramsExtPtr) {
        var pcCountIdx = dv.g32(paramsExtPtr + O.plugin_params.count);
        var pcGetInfoIdx = dv.g32(paramsExtPtr + O.plugin_params.get_info);
        var pcount = table.get(pcCountIdx)(pluginPtr);
        var infoPtr = malloc(O.param_info.size);
        for (var pi = 0; pi < pcount; pi++) {
          if (!table.get(pcGetInfoIdx)(pluginPtr, pi, infoPtr)) continue;
          paramCache.push({
            id: dv.g32(infoPtr + O.param_info.id),
            flags: dv.g32(infoPtr + O.param_info.flags),
            name: readCStr(infoPtr + O.param_info.name),
            min: new DataView(mem.buffer).getFloat64(infoPtr + O.param_info.min_value, true),
            max: new DataView(mem.buffer).getFloat64(infoPtr + O.param_info.max_value, true),
            def: new DataView(mem.buffer).getFloat64(infoPtr + O.param_info.default_value, true),
          });
        }
      }

      // event-list shim + preallocated audio/event scratch (reused every process() call)
      var eventsShimB64 = 'AGFzbQEAAAABEwNgA39/fwF/YAF/AX9gAn9/AX8CCgEDZW52AmQyAAADBAMBAgIHGQMEc2l6ZQABA2dldAACCHRyeV9wdXNoAAMKIgMKAEEAIABBABAACwoAQQEgACABEAALCgBBAiAAIAEQAAs=';
      var eventsShimBytes = Uint8Array.from(atob(eventsShimB64), function (c) { return c.charCodeAt(0); });
      var pendingNotes = [], pendingParams = [];
      function d2(id, ctx, arg) {
        if (id === 0) return pendingNotes.length + pendingParams.length;
        if (id === 1) return arg < pendingParams.length ? pendingParams[arg] : pendingNotes[arg - pendingParams.length];
        return 1; // try_push: discard output events
      }
      var evShimInst = new WebAssembly.Instance(new WebAssembly.Module(eventsShimBytes), { env: { d2: d2 } });
      var evBase = table.length;
      table.grow(3);
      table.set(evBase + 0, evShimInst.exports.size);
      table.set(evBase + 1, evShimInst.exports.get);
      table.set(evBase + 2, evShimInst.exports.try_push);

      var outLPtr = malloc(blockSize * 4), outRPtr = malloc(blockSize * 4);
      var inLPtr = malloc(blockSize * 4), inRPtr = malloc(blockSize * 4);
      var data32OutPtr = malloc(8);
      dv.s32(data32OutPtr + 0, outLPtr);
      dv.s32(data32OutPtr + 4, outRPtr);
      var audioOutPtr = malloc(O.audio_buffer.size);
      dv.s32(audioOutPtr + O.audio_buffer.data32, data32OutPtr);
      dv.s32(audioOutPtr + O.audio_buffer.channel_count, 2);
      var data32InPtr = malloc(8);
      dv.s32(data32InPtr + 0, inLPtr);
      dv.s32(data32InPtr + 4, inRPtr);
      var audioInPtr = malloc(O.audio_buffer.size);
      dv.s32(audioInPtr + O.audio_buffer.data32, data32InPtr);
      dv.s32(audioInPtr + O.audio_buffer.channel_count, 2);

      var inEventsPtr = malloc(O.input_events.size);
      dv.s32(inEventsPtr + O.input_events.ctx, 0);
      dv.s32(inEventsPtr + O.input_events.size_fn, evBase + 0);
      dv.s32(inEventsPtr + O.input_events.get, evBase + 1);
      var outEventsPtr = malloc(O.output_events.size);
      dv.s32(outEventsPtr + O.output_events.ctx, 0);
      dv.s32(outEventsPtr + O.output_events.try_push, evBase + 2);

      var processPtr = malloc(O.process.size);
      dv.s32(processPtr + O.process.frames_count, blockSize);
      dv.s32(processPtr + O.process.transport, 0);
      dv.s32(processPtr + O.process.audio_outputs, audioOutPtr);
      dv.s32(processPtr + O.process.audio_outputs_count, 1);
      dv.s32(processPtr + O.process.in_events, inEventsPtr);
      dv.s32(processPtr + O.process.out_events, outEventsPtr);

      var NOTE_POOL = 64, PARAM_POOL = 16;
      var notePoolPtr = malloc(NOTE_POOL * O.event_note.size);
      var paramPoolPtr = malloc(PARAM_POOL * O.event_param_value.size);

      inst = {
        exp: exp, mem: mem, table: table, dv: dv, O: O,
        pluginPtr: pluginPtr, pProcessIdx: dv.g32(pluginPtr + O.plugin.process),
        pOnMainThreadIdx: dv.g32(pluginPtr + O.plugin.on_main_thread),
        blockSize: blockSize, isInstrument: isInstrument, name: pluginName, paramCache: paramCache,
        pendingMainThreadRef: function () { return pendingMainThread; },
        clearMainThread: function () { pendingMainThread = false; },
        outLPtr: outLPtr, outRPtr: outRPtr, inLPtr: inLPtr, inRPtr: inRPtr,
        audioInPtr: audioInPtr, audioOutPtr: audioOutPtr,
        processPtr: processPtr,
        notePoolPtr: notePoolPtr, paramPoolPtr: paramPoolPtr,
        NOTE_POOL: NOTE_POOL, PARAM_POOL: PARAM_POOL,
        noteWriteIdx: 0, paramWriteIdx: 0,
        pendingNotes: pendingNotes, pendingParams: pendingParams,
        workers: hostEnv.workers,
      };
    } catch (e) {
      console.error('wclap: load failed:', e);
      return 0;
    }

    stringToUTF8(inst.name, outNamePtr, outNameSz);
    HEAP32[outIsInstrumentPtr >> 2] = inst.isInstrument ? 1 : 0;
    var handle = WCLAP_INSTANCES.length;
    WCLAP_INSTANCES.push(inst);
    return handle;
  },

  wclap_web_unload__deps: ['$WCLAP_INSTANCES'],
  wclap_web_unload: function (handle) {
    if (!handle || !WCLAP_INSTANCES[handle]) return;
    var inst = WCLAP_INSTANCES[handle];
    // .terminate() also tears down any nested workers a thread itself spawned.
    if (inst.workers) inst.workers.forEach(function (w) { w.terminate(); });
    WCLAP_INSTANCES[handle] = null;
  },

  wclap_web_list_plugins__deps: ['$WCLAP_OFF', '$WCLAP_MAKE_HOST_ENV'],
  wclap_web_list_plugins: function (pathPtr, outIdsPtr, outNamesPtr, idNameSz, maxCount) {
    var path = UTF8ToString(pathPtr);
    var bytes;
    try { bytes = FS.readFile(path); } catch (e) { return 0; }
    var O = WCLAP_OFF;
    try {
      var mod = new WebAssembly.Module(bytes);

      // No CLAP host struct exists yet at listing time, so threads spawned during
      // _initialize() get no host-bridge shim (empty shimB64) — nothing valid for
      // them to call back into anyway.
      var hostEnv = WCLAP_MAKE_HOST_ENV(mod, '');
      if (!hostEnv) return 0;

      var mem, table, exp;
      function u8() { return new Uint8Array(mem.buffer); }
      function readCStr(ptr) {
        if (!ptr) return '';
        var b = u8(), end = ptr, limit = Math.min(b.length, ptr + 65536);
        while (end < limit && b[end] !== 0) end++;
        return UTF8ArrayToString(b.slice(ptr, end), 0, end - ptr); // .slice(): see wclap_web_load's readCStr
      }
      var raw = new WebAssembly.Instance(mod, {
        env: hostEnv.envImports,
        wasi_snapshot_preview1: hostEnv.wasiImports,
        wasi: hostEnv.threadImports || {},
      });
      exp = raw.exports; mem = hostEnv.envImports.memory || exp.memory; hostEnv.setMem(mem);
      table = exp.table || exp.__indirect_function_table;
      if (exp._initialize) exp._initialize();
      var dvg = function (o) { return new DataView(mem.buffer).getUint32(o, true); };
      var entryPtr = exp.clap_entry.value;
      var initIdx = dvg(entryPtr + O.entry.init);
      var getFactoryIdx = dvg(entryPtr + O.entry.get_factory);
      function writeCStr(str) {
        var utf8 = new TextEncoder().encode(str + '\0');
        var ptr = exp.malloc(utf8.length);
        u8().set(utf8, ptr);
        return ptr;
      }
      var n = 0;
      if (table.get(initIdx)(writeCStr(path))) {
        var factoryPtr = table.get(getFactoryIdx)(writeCStr('clap.plugin-factory'));
        if (factoryPtr) {
          var getCountIdx = dvg(factoryPtr + O.factory.get_plugin_count);
          var getDescIdx = dvg(factoryPtr + O.factory.get_plugin_descriptor);
          var count = table.get(getCountIdx)(factoryPtr);
          n = Math.min(count, maxCount);
          for (var i = 0; i < n; i++) {
            var d = table.get(getDescIdx)(factoryPtr, i);
            stringToUTF8(readCStr(dvg(d + O.descriptor.id)), outIdsPtr + i * idNameSz, idNameSz);
            stringToUTF8(readCStr(dvg(d + O.descriptor.name)), outNamesPtr + i * idNameSz, idNameSz);
          }
        }
      }
      hostEnv.workers.forEach(function (w) { w.terminate(); });
      return n;
    } catch (e) {
      console.error('wclap_web_list_plugins failed:', e);
      return 0;
    }
  },

  wclap_web_note_on__deps: ['$WCLAP_INSTANCES'],
  wclap_web_note_on: function (handle, note, velocity, offset) {
    var inst = WCLAP_INSTANCES[handle];
    if (!inst || inst.noteWriteIdx >= inst.NOTE_POOL) return;
    var O = inst.O, dv = inst.dv;
    var ptr = inst.notePoolPtr + inst.noteWriteIdx * O.event_note.size;
    inst.noteWriteIdx++;
    dv.s32(ptr + O.event_header.sz, O.event_note.size);
    dv.s32(ptr + O.event_header.time, offset);
    dv.su16(ptr + O.event_header.space_id, 0);
    dv.su16(ptr + O.event_header.type, 0); // EVENT_NOTE_ON
    dv.s32(ptr + O.event_header.flags, 0);
    dv.s32(ptr + O.event_note.note_id, -1);
    dv.s16(ptr + O.event_note.port_index, 0);
    dv.s16(ptr + O.event_note.channel, 0);
    dv.s16(ptr + O.event_note.key, note);
    dv.sf64(ptr + O.event_note.velocity, velocity / 127.0);
    inst.pendingNotes.push(ptr);
  },

  wclap_web_note_off__deps: ['$WCLAP_INSTANCES'],
  wclap_web_note_off: function (handle, note, offset) {
    var inst = WCLAP_INSTANCES[handle];
    if (!inst || inst.noteWriteIdx >= inst.NOTE_POOL) return;
    var O = inst.O, dv = inst.dv;
    var ptr = inst.notePoolPtr + inst.noteWriteIdx * O.event_note.size;
    inst.noteWriteIdx++;
    dv.s32(ptr + O.event_header.sz, O.event_note.size);
    dv.s32(ptr + O.event_header.time, offset);
    dv.su16(ptr + O.event_header.space_id, 0);
    dv.su16(ptr + O.event_header.type, 1); // EVENT_NOTE_OFF
    dv.s32(ptr + O.event_header.flags, 0);
    dv.s32(ptr + O.event_note.note_id, -1);
    dv.s16(ptr + O.event_note.port_index, 0);
    dv.s16(ptr + O.event_note.channel, 0);
    dv.s16(ptr + O.event_note.key, note);
    dv.sf64(ptr + O.event_note.velocity, 0);
    inst.pendingNotes.push(ptr);
  },

  wclap_web_queue_param__deps: ['$WCLAP_INSTANCES'],
  wclap_web_queue_param: function (handle, paramId, value) {
    var inst = WCLAP_INSTANCES[handle];
    if (!inst || inst.paramWriteIdx >= inst.PARAM_POOL) return;
    var O = inst.O, dv = inst.dv;
    var ptr = inst.paramPoolPtr + inst.paramWriteIdx * O.event_param_value.size;
    inst.paramWriteIdx++;
    dv.s32(ptr + O.event_header.sz, O.event_param_value.size);
    dv.s32(ptr + O.event_header.time, 0);
    dv.su16(ptr + O.event_header.space_id, 0);
    dv.su16(ptr + O.event_header.type, 5); // EVENT_PARAM_VALUE
    dv.s32(ptr + O.event_header.flags, 0);
    dv.s32(ptr + O.event_param_value.param_id, paramId);
    dv.sf64(ptr + O.event_param_value.value, value);
    dv.s32(ptr + O.event_param_value.note_id, -1);
    dv.s16(ptr + O.event_param_value.port_index, -1);
    dv.s16(ptr + O.event_param_value.channel, -1);
    dv.s16(ptr + O.event_param_value.key, -1);
    inst.pendingParams.push(ptr);
  },

  wclap_web_process__deps: ['$WCLAP_INSTANCES'],
  wclap_web_process: function (handle, inLPtr, inRPtr, outLPtr, outRPtr, frames) {
    var inst = WCLAP_INSTANCES[handle];
    if (!inst) return;
    var O = inst.O, dv = inst.dv, table = inst.table, mem = inst.mem;

    dv.s32(inst.processPtr + O.process.audio_inputs, inLPtr ? inst.audioInPtr : 0);
    dv.s32(inst.processPtr + O.process.audio_inputs_count, inLPtr ? 1 : 0);
    dv.s32(inst.processPtr + O.process.frames_count, frames);
    dv.sbig(inst.processPtr + O.process.steady_time, -1n);

    if (inLPtr) {
      new Float32Array(mem.buffer, inst.inLPtr, frames).set(HEAPF32.subarray(inLPtr >> 2, (inLPtr >> 2) + frames));
      new Float32Array(mem.buffer, inst.inRPtr, frames).set(HEAPF32.subarray(inRPtr >> 2, (inRPtr >> 2) + frames));
    }

    table.get(inst.pProcessIdx)(inst.pluginPtr, inst.processPtr);

    HEAPF32.set(new Float32Array(mem.buffer, inst.outLPtr, frames), outLPtr >> 2);
    HEAPF32.set(new Float32Array(mem.buffer, inst.outRPtr, frames), outRPtr >> 2);

    inst.noteWriteIdx = 0;
    inst.paramWriteIdx = 0;
    inst.pendingNotes.length = 0;
    inst.pendingParams.length = 0;
  },

  wclap_web_param_count__deps: ['$WCLAP_INSTANCES'],
  wclap_web_param_count: function (handle) {
    var inst = WCLAP_INSTANCES[handle];
    return inst ? inst.paramCache.length : 0;
  },

  wclap_web_param_info__deps: ['$WCLAP_INSTANCES'],
  wclap_web_param_info: function (handle, idx, outIdPtr, outNamePtr, outNameSz, outMinPtr, outMaxPtr, outDefaultPtr) {
    var inst = WCLAP_INSTANCES[handle];
    if (!inst || idx < 0 || idx >= inst.paramCache.length) return 0;
    var p = inst.paramCache[idx];
    HEAP32[outIdPtr >> 2] = p.id;
    stringToUTF8(p.name, outNamePtr, outNameSz);
    HEAPF64[outMinPtr >> 3] = p.min;
    HEAPF64[outMaxPtr >> 3] = p.max;
    HEAPF64[outDefaultPtr >> 3] = p.def;
    return 1;
  },

  wclap_web_param_flags__deps: ['$WCLAP_INSTANCES'],
  wclap_web_param_flags: function (handle, idx, outFlagsPtr) {
    var inst = WCLAP_INSTANCES[handle];
    if (!inst || idx < 0 || idx >= inst.paramCache.length) return 0;
    HEAP32[outFlagsPtr >> 2] = inst.paramCache[idx].flags;
    return 1;
  },

  wclap_web_do_main_thread_work__deps: ['$WCLAP_INSTANCES'],
  wclap_web_do_main_thread_work: function (handle) {
    var inst = WCLAP_INSTANCES[handle];
    if (!inst || !inst.pendingMainThreadRef()) return;
    inst.clearMainThread();
    if (inst.pOnMainThreadIdx) inst.table.get(inst.pOnMainThreadIdx)(inst.pluginPtr);
  },
});
