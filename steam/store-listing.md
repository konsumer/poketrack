# Steam store listing - paste-ready copy

Draft copy for the Steamworks "Store Page" wizard. Steam's description
editor uses its own markup (not Markdown) - bracketed tags below
([b], [list], etc.) are literal Steam formatting tags, paste them as-is
into the "About This Game" box.

## Basic info

- **App name:** PokeTrack
- **Genre:** Audio Production, Utilities (pick both if allowed; closest
  single genre is "Audio Production")
- **Price:** $4.99 USD (let Steam auto-calculate regional prices from this)

## Short description (appears in search results, ~300 char limit)

```
Cross-platform joystick-driven music-tracker.
```

## About the game (long description)

```
[b]PokeTrack[/b] is a cross-platform music tracker built for joystick  input first. Input is consistent and fast once you learn it - most actions are a single button plus a direction, so you can stay heads-down in the pattern editor without reaching for a mouse.

[b]Designed for handhelds[/b]

PokeTrack runs great on Linux-based handheld consoles as well as mac/windows/linux desktop.

[b]Built-in synths and effects[/b]

A set of built-in units (sound generators and effects) covers most tracking needs out of the box, with no external plugins required.

[b]Sample-based instruments[/b]

Instruments are intentionally simple: load samples, set up velocity zones offline, and save as SFZ. A companion web-based SFZ editor is linked in the game's README for building complex instruments, multi-instrument kits and breakbeat chops.

[b]Plugin support[/b]

Extend PokeTrack with custom WCLAP plugins. These can be written in any language that compiles to wasm (including puredata, using my own tools.)

[b]Themeable[/b]

Skin the UI at launch with a theme file, including converted LGPT themes.


[list]
[*] Fast, consistent joystick + keyboard control scheme
[*] Cross-platform: Windows, macOS, Linux (incl. ARM/handheld builds)
[*] Built-in synth and effect units - no plugins required to start
[*] SF2/SFZ soundfont instrument support
[*] Custom CLAP/WCLAP plugin support
[*] Themable UI
[/list]

[b]Open source[/b]

PokeTrack's source is developed in the open on GitHub, and there are even build-releases, so you don't need any tools. Buying it here supports continued development and gets you an easy one-click install plus automatic updates through Steam, but is not required if you want to install it yourself.
```

## Feature list (bullet points on store page)

- Joystick-first, fast pattern-tracker workflow
- Cross-platform: Windows / macOS / Linux, incl. ARM handhelds
- Built-in synth & effect units
- SFZ sample-based instruments
- Custom CLAP/WCLAP plugin support
- Themeable UI

## Tags (suggest up to 20, order matters - most relevant first)

Music, Audio Production, Chiptune, Utilities, Indie, Software,
Controller, Full controller support, Sandbox, Design & Illustration,
Pixel Graphics, 2D, Singleplayer, Tracker, Handheld

## Controller support

Full controller support - this is a joystick-driven app, make sure to
mark "Full Controller Support" (not "Partial") in the Steamworks
controller config section. If you get to Steam Input, you can add a
proper controller config template later (see the README todo item).

## System requirements

Fill separately for Windows / macOS / Linux tabs. Suggested starting
point (adjust after real testing - these are placeholders, not measured):

**Minimum (all platforms)**
- OS: Windows 10 64-bit / macOS 12+ / Ubuntu 22.04 (or equivalent)
- Processor: Any 64-bit dual-core, 2015 or later
- Memory: 512 MB RAM
- Graphics: OpenGL 3.3 / GLES2-capable GPU
- Storage: 100 MB available space

No "Recommended" tier is really needed for an app this light - minimum
covers it, but Steam requires the field to be filled, so duplicate
minimum into recommended if left blank.

## Notes / things only you can answer in the wizard

- Age rating questionnaire (IARC): no objectionable content, should be
  a fast "everyone" pass.
- Legal/tax/banking setup: one-time Steamworks account step, not
  scriptable, do this first if not already done - everything else in
  this repo can be prepped while that's in review.
- Trailer: not drafted here - a 30–60s capture of the pattern editor in
  use (joystick input visible) would sell the "fast tracker" pitch well.
- Screenshots: need actual in-app captures at 1920x1080+ (16:9). `art/keys.png`
  explains controls but isn't a screenshot; capture live UI for the
  store gallery.
