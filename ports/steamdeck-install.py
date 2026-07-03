#!/usr/bin/env python3
# Steam Deck installer for RayPokeTrack. Downloads the latest release,
# optionally the example songs, and optionally adds a non-Steam game
# shortcut. Only needs python3 + zenity, both preinstalled on SteamOS.
#
# Usage: chmod +x steamdeck-install.py && ./steamdeck-install.py

import json
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
import zlib
from pathlib import Path

REPO = "konsumer/raypoketrack"
ASSET = "raypoketrack-linux.zip"  # steamdeck is x86_64


def zenity(*args, input=None):
    return subprocess.run(["zenity", *args], input=input, text=True,
                           capture_output=True)


def die(msg):
    zenity("--error", "--text", msg, "--width=400")
    sys.exit(1)


def info(msg):
    zenity("--info", "--text", msg, "--width=400")


def question(msg):
    return zenity("--question", "--text", msg, "--width=400").returncode == 0


def choose_dir():
    r = zenity("--file-selection", "--title=Choose a directory for RayPokeTrack",
               "--directory", f"--filename={Path.home()}/")
    if r.returncode != 0:
        sys.exit(0)
    return Path(r.stdout.strip())


def choose_from_list(title, items):
    if len(items) == 1:
        return items[0]
    r = zenity("--list", f"--title={title}", "--column=user id", *items)
    if r.returncode != 0 or not r.stdout.strip():
        return None
    return r.stdout.strip()


def download_with_progress(url, dest, title):
    proc = subprocess.Popen(["zenity", "--progress", f"--title={title}",
                              f"--text={title}", "--percentage=0",
                              "--auto-close", "--no-cancel"],
                             stdin=subprocess.PIPE, text=True)

    def hook(count, block_size, total_size):
        if total_size > 0 and proc.stdin:
            pct = min(100, count * block_size * 100 // total_size)
            try:
                proc.stdin.write(f"{pct}\n")
                proc.stdin.flush()
            except BrokenPipeError:
                pass

    try:
        urllib.request.urlretrieve(url, dest, reporthook=hook)
    finally:
        if proc.stdin:
            try:
                proc.stdin.close()
            except BrokenPipeError:
                pass
        proc.wait()


def latest_release():
    with urllib.request.urlopen(f"https://api.github.com/repos/{REPO}/releases/latest") as r:
        return json.load(r)


# --- minimal binary VDF (shortcuts.vdf) read/write ---
# format: 0x00 = nested dict, 0x01 = string, 0x02 = int32, 0x08 = end of dict

def _read_cstring(buf, i):
    j = buf.index(b"\x00", i)
    return buf[i:j].decode("utf-8", "replace"), j + 1


def _parse_dict(buf, i):
    d = {}
    while buf[i] != 0x08:
        t = buf[i]
        i += 1
        name, i = _read_cstring(buf, i)
        if t == 0x00:
            d[name], i = _parse_dict(buf, i)
        elif t == 0x01:
            d[name], i = _read_cstring(buf, i)
        elif t == 0x02:
            d[name] = struct.unpack_from("<i", buf, i)[0]
            i += 4
        else:
            raise ValueError(f"unknown vdf type {t:#x}")
    return d, i + 1


def _serialize_dict(d):
    out = bytearray()
    for k, v in d.items():
        key = k.encode("utf-8") + b"\x00"
        if isinstance(v, dict):
            out += b"\x00" + key + _serialize_dict(v)
        elif isinstance(v, str):
            out += b"\x01" + key + v.encode("utf-8") + b"\x00"
        elif isinstance(v, int):
            out += b"\x02" + key + struct.pack("<i", v)
    out += b"\x08"
    return bytes(out)


def load_shortcuts(path):
    if not path.exists():
        return {"shortcuts": {}}
    d, _ = _parse_dict(path.read_bytes(), 0)
    return d


def add_shortcut(shortcuts_vdf, exe, appname, start_dir, launch_options):
    entries = shortcuts_vdf.setdefault("shortcuts", {})
    idx = str(len(entries))
    appid = zlib.crc32((exe + appname).encode()) | 0x80000000
    if appid >= 0x80000000:
        appid -= 0x100000000  # store as signed int32
    entries[idx] = {
        "appid": appid,
        "AppName": appname,
        "Exe": exe,
        "StartDir": start_dir,
        "icon": "",
        "ShortcutPath": "",
        "LaunchOptions": launch_options,
        "IsHidden": 0,
        "AllowDesktopConfig": 1,
        "AllowOverlay": 1,
        "OpenVR": 0,
        "Devkit": 0,
        "DevkitGameID": "",
        "DevkitOverrideAppID": 0,
        "LastPlayTime": 0,
        "tags": {},
    }


def find_shortcuts_vdf():
    for steam_dir in (Path.home() / ".steam/steam", Path.home() / ".local/share/Steam"):
        userdata = steam_dir / "userdata"
        if not userdata.is_dir():
            continue
        ids = [p.name for p in userdata.iterdir() if p.is_dir() and (p / "config").is_dir()]
        if not ids:
            continue
        picked = choose_from_list("Which Steam account should get the shortcut?", sorted(ids))
        if not picked:
            return None
        return userdata / picked / "config" / "shortcuts.vdf"
    return None


def add_steam_shortcut(exe_path):
    vdf_path = find_shortcuts_vdf()
    if vdf_path is None:
        die("Could not find a Steam userdata directory. Add the shortcut manually.")
        return
    shortcuts = load_shortcuts(vdf_path)
    add_shortcut(shortcuts, f'"{exe_path}"', "RayPokeTrack", f'"{exe_path.parent}"', "--fullscreen")
    vdf_path.write_bytes(_serialize_dict(shortcuts))


def download_examples(dest):
    release = latest_release()
    tag = release["tag_name"]
    with tempfile.TemporaryDirectory() as tmp:
        tar_path = Path(tmp) / "src.tar.gz"
        url = f"https://github.com/{REPO}/archive/refs/tags/{tag}.tar.gz"
        download_with_progress(url, tar_path, "Downloading examples...")
        with tarfile.open(tar_path) as t:
            members = [m for m in t.getmembers() if "/examples/" in m.name]
            t.extractall(tmp, members=members)
        src = next(Path(tmp).glob("*/examples"))
        shutil.copytree(src, dest / "examples", dirs_exist_ok=True)


def main():
    if shutil.which("zenity") is None:
        print("zenity is required", file=sys.stderr)
        sys.exit(1)

    install_root = choose_dir()
    dest = install_root / "raypoketrack"

    release = latest_release()
    tag = release["tag_name"]
    asset_url = next((a["browser_download_url"] for a in release["assets"]
                       if a["name"] == ASSET), None)
    if not asset_url:
        die(f"Could not find asset {ASSET} in release {tag}")

    with tempfile.TemporaryDirectory() as tmp:
        zip_path = Path(tmp) / ASSET
        download_with_progress(asset_url, zip_path, f"Downloading RayPokeTrack {tag}...")
        dest.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(zip_path) as z:
            z.extractall(dest)

    bin_path = dest / "raypoketrack"
    bin_path.chmod(bin_path.stat().st_mode | 0o111)

    if question("Download example songs and instruments too?"):
        download_examples(dest)

    shortcut_added = False
    if question("Add RayPokeTrack as a non-Steam game?"):
        add_steam_shortcut(bin_path)
        shortcut_added = True

    msg = f"RayPokeTrack {tag} installed to:\n{dest}"
    if shortcut_added:
        msg += "\n\nRestart Steam to see it in your library. Launch option --fullscreen was set."
    info(msg)


if __name__ == "__main__":
    main()
