#!/usr/bin/env python3
"""Regenerate the app icon assets from one master image.

  ./scripts/make_icons.py [SOURCE_IMAGE]      (default: art/appicon.png)

Writes:
  art/appicon.png       1024x1024 RGBA master (padded square if the input isn't)
  art/appicon.icns      macOS bundle icon (named by Info.plist's CFBundleIconFile)
  art/appicon.ico       Windows icon, embedded in the .exe via art/appicon.rc.in
  art/appicon-256.png   window icon, embedded as src/appicon_png.h

The .icns needs macOS's iconutil; the rest is Pillow only.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MASTER = ROOT / "art" / "appicon.png"
TMP_DIR = Path("/tmp/poketrack-iconset")


def main():
  src = Path(sys.argv[1]) if len(sys.argv) > 1 else MASTER
  if not src.exists():
    sys.exit(f"no such image: {src}")

  from PIL import Image

  im = Image.open(src).convert("RGBA")
  side = max(im.size)
  square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
  square.paste(im, ((side - im.width) // 2, (side - im.height) // 2))
  master = square.resize((1024, 1024), Image.LANCZOS)
  master.save(MASTER, optimize=True)

  # Windows .ico — every size Explorer/taskbar asks for.
  master.save(ROOT / "art" / "appicon.ico",
              sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

  # macOS .icns — iconutil wants an .iconset of exact names.
  shutil.rmtree(TMP_DIR, ignore_errors=True)
  (TMP_DIR / "appicon.iconset").mkdir(parents=True)
  iconset = TMP_DIR / "appicon.iconset"
  for sz in (16, 32, 128, 256, 512):
    master.resize((sz, sz), Image.LANCZOS).save(iconset / f"icon_{sz}x{sz}.png")
    master.resize((sz * 2, sz * 2), Image.LANCZOS).save(iconset / f"icon_{sz}x{sz}@2x.png")
  if shutil.which("iconutil"):
    subprocess.run(["iconutil", "-c", "icns", str(iconset),
                    "-o", str(ROOT / "art" / "appicon.icns")], check=True)
    print("art/appicon.icns")
  else:
    print("iconutil not found (macOS only) — skipped appicon.icns")

  # Window icon embedded in the binary (SetWindowIcon on Linux/Windows).
  win_png = ROOT / "art" / "appicon-256.png"
  master.resize((256, 256), Image.LANCZOS).save(win_png, optimize=True)
  subprocess.run([sys.executable, str(ROOT / "scripts" / "embed_png.py"),
                  str(win_png.relative_to(ROOT)), "src/appicon_png.h", "APPICON_PNG"],
                 check=True, cwd=ROOT)


main()
