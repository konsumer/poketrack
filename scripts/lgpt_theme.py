#!/usr/bin/env python3
"""Convert an LGPT (Little GPTracker) CONFIG.xml theme into a poketrack
.ptt theme file.

LGPT themes only define 4 colors:
  BACKGROUND, FOREGROUND, HICOLOR1, HICOLOR2
(see https://sixey.es/sounds/piggythemes/ for examples)

poketrack has more named roles than that, so the extra fields are
derived by blending between those 4 anchors. There's no way to recover
colors LGPT never had an opinion on (e.g. a dedicated "note off" red) -
those roles just reuse the closest anchor. Edit the generated .ptt file
by hand if you want to tweak individual roles afterwards.

Usage:
  ./lgpt_theme.py CONFIG.xml [output.ptt]
"""
import sys
import xml.etree.ElementTree as ET


def parse_lgpt(path):
    root = ET.parse(path).getroot()
    colors = {}
    for tag in ("BACKGROUND", "FOREGROUND", "HICOLOR1", "HICOLOR2"):
        el = root.find(tag)
        if el is None or "value" not in el.attrib:
            raise ValueError(f"{path}: missing <{tag} value=.../>")
        colors[tag] = el.attrib["value"]
    return colors


def hex_to_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def rgb_to_hex(rgb):
    return "".join(f"{max(0, min(255, round(c))):02x}" for c in rgb)


def lerp(a, b, t):
    ca, cb = hex_to_rgb(a), hex_to_rgb(b)
    return rgb_to_hex(ca[i] + (cb[i] - ca[i]) * t for i in range(3))


def build_theme(bg, fg, hi1, hi2):
    fields = {
        "bg": bg,
        "bg_alt": lerp(bg, fg, 0.08),
        "cursor": hi1,
        "cursor2": lerp(bg, hi2, 0.55),
        "sep": lerp(bg, fg, 0.18),
        "text": fg,
        "dim": lerp(bg, fg, 0.35),
        "header": lerp(bg, hi1, 0.6),
        "note": hi2,
        "note_off": hi2,  # LGPT has no dedicated warning color; closest accent
        "vel": hi2,
        "inst": hi1,
        "fx": lerp(hi1, hi2, 0.5),
        "play": hi2,
        "status": hi2,
        "title": lerp(fg, "ffffff", 0.3),
        "edit_tag": hi1,
        "fb_header": lerp(bg, hi1, 0.6),
        "fb_dim": lerp(bg, fg, 0.35),
        "fb_input_bg": lerp(bg, fg, 0.08),
    }
    # 16 track colors: loop around hi1 -> hi2 -> hi1 for variety
    for i in range(16):
        t = i / 16
        t = t * 2 if t < 0.5 else (1 - t) * 2
        fields[f"track{i}"] = lerp(hi1, hi2, t)
    return fields


def main():
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} CONFIG.xml [output.ptt]", file=sys.stderr)
        sys.exit(1)

    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".ptt"

    lgpt = parse_lgpt(src)
    fields = build_theme(lgpt["BACKGROUND"], lgpt["FOREGROUND"],
                          lgpt["HICOLOR1"], lgpt["HICOLOR2"])

    with open(dst, "w") as f:
        f.write(f"# converted from {src} by lgpt_theme.py\n")
        for key, val in fields.items():
            f.write(f"{key}={val}\n")

    print(f"wrote {dst}")


if __name__ == "__main__":
    main()
