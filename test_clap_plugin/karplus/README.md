# Karplus String

A Karplus-Strong plucked string, written in AssemblyScript as a WCLAP
instrument using [as-clap](https://github.com/WebCLAP/as-clap).

Each voice is a circular delay line tuned to the played note's pitch, fed
back through a one-pole lowpass filter that recirculates near-losslessly and
shapes brightness only. The audible decay is a separate amplitude envelope
multiplied onto the loop's output but not fed back into it — feeding decay
into the loop let the filter's own dynamics swamp it, making DECAY barely
audible next to DAMP, so the two are deliberately decoupled here.

## Params

| Param | Range | Notes |
|-------|-------|-------|
| Decay | 0–1 | How long the string rings before going silent (independent of Damp) |
| Damp | 0–1 | Loop filter brightness — low = dark/muted, high = bright/metallic |
| Pluck | 0–1 | Excitation brightness — low = soft mallet, high = hard pick |
| Volume | 0–1 | Output level |

## Building

```sh
npm install
npm run build
```

`npm install` needs `as-clap` itself (`github:WebCLAP/as-clap`), which isn't
on the npm registry — if git dependencies are disabled in your environment,
clone [WebCLAP/as-clap](https://github.com/WebCLAP/as-clap) and copy it into
`node_modules/as-clap` by hand instead.

`npm run build` compiles `assembly/index.ts` to `build/release.wasm` and
copies it to `../karplus.wclap.wasm`, alongside the other test plugins.
