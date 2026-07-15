# Themes

raypoketrack's colors can be overridden at launch:

```
raypoketrack --theme ~/cyber.ptt
```

There's no persisted config — if you want a theme, pass it every time (alias
the command, or wrap it in a launcher script).

## Default theme

If a file named `theme.ptt` exists in the start directory (the directory you
launch raypoketrack from), it's loaded automatically at startup — same as
`song.rpt`. It's silently skipped if missing, so this is opt-in: drop a
`theme.ptt` next to your `song.rpt` to theme a project without touching the
command line.

`--theme <path>` is layered on top of `theme.ptt`, not instead of it: any
field the `--theme` file doesn't mention keeps whatever `theme.ptt` (or the
built-in default) set it to.

Reference implementation: `src/theme.c` (`theme_load` / `theme_load_default`).

## File format

A `.ptt` file is plain text, one field per line:

```
key=RRGGBB
```

- `#` starts a comment (the whole line is ignored)
- blank lines are ignored
- hex color is 6 digits, case-insensitive, optional leading `#`
- unknown keys and unparsable colors print a warning to stderr and are
  skipped — the rest of the file still loads, and any field not mentioned
  keeps its built-in default
- you don't have to specify every field; a theme file can override just
  one or two colors

[`examples/theme.ptt`](./examples/theme.ptt) spells out every field at its
built-in default value — a good starting point for your own theme. Copy it
to your project directory as `theme.ptt` and start editing hex values:

```
cp examples/theme.ptt theme.ptt
```

## Gallery

[`examples/themes/`](./examples/themes/) has more to try or pick apart:

| Theme | Look |
|---|---|
| `cyber.ptt` | hand-tuned monochrome phosphor green — old hacker-terminal CRT |
| `lgpt-default.ptt` | LGPT's own default — warm dusty rose |
| `purple.ptt` | black + hot pink/magenta |
| `furrest.ptt` | pale pink background, forest green accents |
| `dustrial.ptt` | near-black, dusty mauve, industrial amber |
| `sylveon.ptt` | off-white background, soft pink/blue pastel |
| `minecraft.ptt` | parchment background, orange/red accents |
| `rain.ptt` | dark olive, muted teal-green |
| `808.ptt` | charcoal, orange + yellow accents |
| `wavetable.ptt` | near-black, warm orange/teal |
| `greeny.ptt` | teal background, lime/hot-pink accents |

```
raypoketrack --theme examples/themes/cyber.ptt
```

Every theme in the gallery except `cyber.ptt` was generated with
`lgpt_theme.py` from the example palettes at
[sixey.es/sounds/piggythemes](https://sixey.es/sounds/piggythemes/) — see
below for how to convert your own.

## Fields

| Key           | Default    | Used for                                   |
|---------------|------------|---------------------------------------------|
| `bg`          | `000008`   | main background                            |
| `bg_alt`      | `060610`   | alternating row / panel background         |
| `cursor`      | `182868`   | primary selection highlight                |
| `cursor2`     | `281040`   | secondary selection highlight              |
| `sep`         | `202030`   | separator lines                            |
| `text`        | `b8b8c8`   | body text                                  |
| `dim`         | `282838`   | de-emphasized text / empty cells           |
| `header`      | `505070`   | column/row headers                         |
| `note`        | `40ffc0`   | note names                                 |
| `note_off`    | `ff5050`   | note-off (`OFF`)                           |
| `vel`         | `a0ff60`   | velocity column                            |
| `inst`        | `ffa030`   | instrument column                          |
| `fx`          | `80a0ff`   | effect columns                             |
| `play`        | `00ff60`   | play-state indicator                       |
| `status`      | `e0ff00`   | status bar text                            |
| `title`       | `ffffff`   | titles / emphasized text                   |
| `edit_tag`    | `ffc000`   | "editing" indicator                        |
| `fb_header`   | `0a0a28`   | file browser header/footer bars            |
| `fb_dim`      | `404060`   | file browser scrollbar track               |
| `fb_input_bg` | `080820`   | file browser text-input field background   |
| `track0`–`track15` | see `src/theme.c` | per-track color in the song/pattern grid |

Controller-button glyphs (A/B/X/Y colors, keycaps) are not themeable —
they mimic real gamepad button colors, so retheming them would make the
on-screen hints misleading.

## Options

Non-color settings, same `key=value` file:

| Key         | Default | Value               | Effect                                          |
|-------------|---------|----------------------|--------------------------------------------------|
| `hide_help` | `0`     | `1`/`true`/`yes` = on | Hides the button-help line at the bottom of every screen, reclaiming that strip for content. |

```
hide_help=1
```

## Converting LGPT themes

[LGPT (Little GPTracker)](https://sixey.es/sounds/piggythemes/) themes are a
`CONFIG.xml` with 4 colors (`BACKGROUND`, `FOREGROUND`, `HICOLOR1`,
`HICOLOR2`). `lgpt_theme.py` converts one into a `.ptt`, deriving the extra
fields by blending between those 4 anchors:

```
./lgpt_theme.py CONFIG.xml mytheme.ptt
raypoketrack --theme mytheme.ptt
```

Since LGPT only has 4 colors and raypoketrack has more roles, some roles
collapse onto the same anchor color (e.g. there's no dedicated LGPT
"note-off" color, so it reuses `HICOLOR2`). Edit the generated `.ptt` by
hand afterwards if you want to split any of those apart.
