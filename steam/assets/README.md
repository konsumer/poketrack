# Steam graphical assets

Sizes/requirements per [Steamworks docs](https://partner.steamgames.com/doc/store/assets)
(current since the Aug 2024 asset revamp). These are **starting points,
not final assets** — give each a final visual check before uploading.

## Capsules (header/small/main/vertical/library)

Steam's capsule upload requires confirming two checkboxes: the logo is
legible and fills at least 1/3 of the image, and there's no text besides
the logo. The original promo renders (`art/horiz.png` / `art/vert.png`)
fail both — they're photography-style scenes with a small logo plus a
lot of other baked-in text (on-device screen UI, nameplate, subtitle).

So these five are rebuilt instead: a heavily blurred/darkened crop of the
promo render as background artwork (blur destroys all the incidental
text, leaving just mood/color), with a large "POKETRACK" wordmark
overlaid via CSS — logo-only, no other text, sized well past the 1/3
minimum. Built with `steam/assets/src/*.html` + a browser screenshot,
then downscaled to exact target size with ffmpeg.

| File | Size | Steam asset |
|---|---|---|
| `main_capsule_1232x706.png` | 1232×706 | Main Capsule |
| `header_capsule_920x430.png` | 920×430 | Header Capsule |
| `vertical_capsule_748x896.png` | 748×896 | Vertical Capsule |
| `library_capsule_600x900.png` | 600×900 | Library Capsule |
| `small_capsule_462x174.png` | 462×174 | Small Capsule |

To tweak (different crop, font size, colors) and regenerate: edit the
matching file in `src/`, open it in a browser (plain double-click works,
it loads `../../../art/*.png` via relative path), and screenshot the
`#capsule` div at 1:1 — e.g. browser dev tools' "Capture node screenshot"
on that element — then resize the PNG to the exact target dimensions
above (`sips -z <h> <w> in.png --out out.png` or ffmpeg `scale=`).

## Icons

| File | Size | Steam asset | Notes |
|---|---|---|---|
| `app_icon_184x184.jpg` | 184×184 | App Icon | From square.png, bottom-anchored crop so the "PT" logotype keeps its margin |
| `shortcut_icon_256x256.png` | 256×256 | Shortcut Icon | Same source/crop as app icon |

No logo-fill/no-text checkboxes apply to these — they're already just the icon.

## Not generated — need real source art

- **Library Hero** (3840×1240, .png, must contain **no text at all** — Valve
  overlays the Library Logo on top of it): horiz.png/vert.png both have the
  wordmark baked into the render, and neither is remotely close to the
  3.1:1 aspect ratio. Needs a fresh render/crop of just the workbench scene
  without the "POKETRACK" text layer — ask whoever built the 3D render for
  a text-free version, or a wider background plate.
- **Library Logo** (transparent PNG, ≤1280w and/or ≤720h): needs the
  "POKETRACK / HANDHELD MUSIC TRACKER" wordmark as its own layer with a
  transparent background, isolated from the 3D render. Not derivable from
  the flattened PNGs in `art/` — check if the original design file (e.g.
  Blender/Photoshop/Figma project) still has the text as a separate layer.
- **Screenshots** (1920×1080+, 16:9): need real in-app captures, not
  promotional renders. `art/keys.png` is a controls diagram, not a
  screenshot — grab a few live shots of the pattern/song/instrument
  screens instead.
- **Trailer**: not an image asset, but same issue — needs real capture.
