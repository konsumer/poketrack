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

## Library assets

| File | Size | Steam asset |
|---|---|---|
| `library_hero_3840x1240.png` | 3840×1240 | Library Hero |
| `library_logo_1280x360.png` | 1280×360 | Library Logo |

Built the same way as the capsules (`src/library_hero.html`, `src/library_logo.html`)
except at a size the browser-screenshot workflow can't capture at full
resolution (Chrome caps screenshots around ~1568px on the long edge), so
these two are rendered directly with Pillow instead of a browser screenshot:

- **Hero**: same cover-fit/blur/darken/saturate/vignette recipe as the
  capsules, applied to `art/horiz.png` at full 3840×1240 — no text, since
  it's a straight blur of the render (destroys the baked-in wordmark same
  as the capsules do).
- **Logo**: "POKETRACK" wordmark, same styling as the capsules' overlay
  text, rendered on a transparent background. True alpha is recovered by
  rendering the HTML twice (once on white, once on black) and diffing —
  Chrome's screenshot tool only outputs JPEG, which has no alpha channel.

## Screenshots

`screenshots/` — real in-app captures at 2532×1424 (16:9, comfortably over
Steam's 1920×1080 minimum), one per main screen:

| File | Screen |
|---|---|
| `01-song.png` | Song / arrangement |
| `02-pattern-empty.png` | Pattern editor, empty |
| `03-menu.png` | Menu |
| `04-instrument.png` | Instrument chain + params |
| `05-pattern-filled.png` | Pattern editor with a song loaded |

Steam shows these in store-page order, so the numeric prefix is the upload
order, not just a filename. Re-capture rather than upscale if the UI changes —
Steam rejects obvious upscales and these are already above the minimum.

## Not generated — needs real capture

- **Trailer**: not an image asset, and the only remaining gap. Steam wants a
  real capture (gameplay, not a promo render); `art/keys.png` is a controls
  diagram and doesn't substitute.
