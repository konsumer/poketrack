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

  wclap_web_load__deps: ['$WCLAP_OFF', '$WCLAP_INSTANCES'],
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
      return UTF8ArrayToString(b, ptr, end - ptr);
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

      // Different toolchains need different imports: AssemblyScript exports
      // its own memory and only needs fd_write/proc_exit; wasi-sdk C++
      // builds (e.g. signalsmith) import a *shared* memory (built with
      // multithreading support even though we never spawn threads) and
      // call into the full WASI preview1 surface during libc init.
      var envImports = {};
      var needsMemImport = WebAssembly.Module.imports(module).some(function (imp) {
        return imp.module === 'env' && imp.kind === 'memory';
      });
      if (needsMemImport)
        envImports.memory = new WebAssembly.Memory({ initial: 256, maximum: 16384, shared: true });

      var raw = new WebAssembly.Instance(module, {
        env: envImports,
        wasi_snapshot_preview1: {
          fd_write: function (fd, iovs, iovsLen, nwritten) {
            var total = 0, out = '';
            var view = new DataView(mem.buffer);
            for (var i = 0; i < iovsLen; i++) {
              var base = view.getUint32(iovs + i * 8, true);
              var len = view.getUint32(iovs + i * 8 + 4, true);
              out += UTF8ArrayToString(u8(), base, len);
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
        },
      });
      exp = raw.exports;
      mem = exp.memory;
      table = exp.table || exp.__indirect_function_table;
      if (exp._initialize) exp._initialize();

      var shimB64 = 'AGFzbQEAAAABHgVgBH9/f38Bf2ACf38Bf2ABfwBgAn9/AGADf39/AAIQAQNlbnYIZGlzcGF0Y2gAAAMJCAECAgIDBAIEB4kBCA1nZXRfZXh0ZW5zaW9uAAEPcmVxdWVzdF9yZXN0YXJ0AAIPcmVxdWVzdF9wcm9jZXNzAAMQcmVxdWVzdF9jYWxsYmFjawAEDXBhcmFtc19yZXNjYW4ABQxwYXJhbXNfY2xlYXIABhRwYXJhbXNfcmVxdWVzdF9mbHVzaAAHCGhvc3RfbG9nAAgKcAgMAEEAIAAgAUEAEAALDQBBASAAQQBBABAAGgsNAEECIABBAEEAEAAaCw0AQQMgAEEAQQAQABoLDQBBBCAAIAFBABAAGgsNAEEFIAAgASACEAAaCw0AQQYgAEEAQQAQABoLDQBBByAAIAEgAhAAGgs=';
      var shimBytes = Uint8Array.from(atob(shimB64), function (c) { return c.charCodeAt(0); });
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
    WCLAP_INSTANCES[handle] = null;
  },

  wclap_web_list_plugins__deps: ['$WCLAP_OFF'],
  wclap_web_list_plugins: function (pathPtr, outIdsPtr, outNamesPtr, idNameSz, maxCount) {
    var path = UTF8ToString(pathPtr);
    var bytes;
    try { bytes = FS.readFile(path); } catch (e) { return 0; }
    var O = WCLAP_OFF;
    try {
      var mod = new WebAssembly.Module(bytes);
      var mem, table, exp;
      function u8() { return new Uint8Array(mem.buffer); }
      function readCStr(ptr) {
        if (!ptr) return '';
        var b = u8(), end = ptr, limit = Math.min(b.length, ptr + 65536);
        while (end < limit && b[end] !== 0) end++;
        return UTF8ArrayToString(b, ptr, end - ptr);
      }
      var envImports = {};
      var needsMemImport = WebAssembly.Module.imports(mod).some(function (imp) {
        return imp.module === 'env' && imp.kind === 'memory';
      });
      if (needsMemImport)
        envImports.memory = new WebAssembly.Memory({ initial: 256, maximum: 16384, shared: true });
      var raw = new WebAssembly.Instance(mod, {
        env: envImports,
        wasi_snapshot_preview1: {
          fd_write: function (fd, iovs, iovsLen, nwritten) {
            new DataView(mem.buffer).setUint32(nwritten, 0, true); return 0;
          },
          proc_exit: function () {},
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
        },
      });
      exp = raw.exports; mem = exp.memory; table = exp.table || exp.__indirect_function_table;
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
      if (!table.get(initIdx)(writeCStr(path))) return 0;
      var factoryPtr = table.get(getFactoryIdx)(writeCStr('clap.plugin-factory'));
      if (!factoryPtr) return 0;
      var getCountIdx = dvg(factoryPtr + O.factory.get_plugin_count);
      var getDescIdx = dvg(factoryPtr + O.factory.get_plugin_descriptor);
      var count = table.get(getCountIdx)(factoryPtr);
      var n = Math.min(count, maxCount);
      for (var i = 0; i < n; i++) {
        var d = table.get(getDescIdx)(factoryPtr, i);
        stringToUTF8(readCStr(dvg(d + O.descriptor.id)), outIdsPtr + i * idNameSz, idNameSz);
        stringToUTF8(readCStr(dvg(d + O.descriptor.name)), outNamesPtr + i * idNameSz, idNameSz);
      }
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
