#!/usr/bin/env python3

# Steam Deck installer for PokeTrack. Downloads the latest release,
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
import urllib.error
import urllib.request
import zipfile
import zlib
from pathlib import Path

REPO = "konsumer/poketrack"
ASSET = "poketrack-linux.zip"  # steamdeck is x86_64
ART_BASE_URL = f"https://raw.githubusercontent.com/{REPO}/main/art"


class InstallError(Exception):
    """Something went wrong; show it in a dialog instead of a stack trace."""


def zenity(*args, input=None):
    return subprocess.run(["zenity", *args], input=input, text=True,
                           capture_output=True)


def die(msg):
    raise InstallError(msg)


def warn(msg):
    zenity("--warning", "--text", msg, "--width=400")


def info(msg):
    zenity("--info", "--text", msg, "--width=400")


def question(msg):
    return zenity("--question", "--text", msg, "--width=400").returncode == 0


def choose_dir(title, default, exit_on_cancel=True):
    r = zenity("--file-selection", f"--title={title}",
               "--directory", f"--filename={default}/")
    if r.returncode != 0:
        if exit_on_cancel:
            sys.exit(0)
        return default
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
    except (urllib.error.URLError, OSError) as e:
        die(f"Download failed ({url}): {e}")
    finally:
        if proc.stdin:
            try:
                proc.stdin.close()
            except BrokenPipeError:
                pass
        proc.wait()


def latest_release():
    url = f"https://api.github.com/repos/{REPO}/releases/latest"
    try:
        with urllib.request.urlopen(url) as r:
            return json.load(r)
    except (urllib.error.URLError, json.JSONDecodeError) as e:
        die(f"Could not reach GitHub ({e}). Check your internet connection.")


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
    try:
        d, _ = _parse_dict(path.read_bytes(), 0)
        d.setdefault("shortcuts", {})
        return d
    except (ValueError, IndexError):
        backup = path.with_suffix(".vdf.bak")
        shutil.copy(path, backup)
        warn(f"{path} looked corrupt, backed it up to {backup} and starting fresh.")
        return {"shortcuts": {}}


def compute_appid(exe, appname):
    """Returns (unsigned, signed) appid, as Steam derives it from exe+appname.
    Grid artwork filenames use the unsigned form; the vdf int32 field needs signed."""
    unsigned = zlib.crc32((exe + appname).encode()) | 0x80000000
    signed = unsigned - 0x100000000 if unsigned >= 0x80000000 else unsigned
    return unsigned, signed


def add_shortcut(shortcuts_vdf, exe, appname, start_dir, launch_options, icon=""):
    entries = shortcuts_vdf.setdefault("shortcuts", {})
    _, appid = compute_appid(exe, appname)
    entry = {
        "appid": appid,
        "AppName": appname,
        "Exe": exe,
        "StartDir": start_dir,
        "icon": icon,
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
    # update in place if this shortcut already exists, instead of duplicating it
    for idx, existing in entries.items():
        if existing.get("AppName") == appname:
            entries[idx] = entry
            return
    entries[str(len(entries))] = entry


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


def download_art(url, dest):
    try:
        dest.parent.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(url, dest)
        return True
    except (urllib.error.URLError, OSError):
        return False


def add_steam_shortcut(exe_path, start_dir):
    vdf_path = find_shortcuts_vdf()
    if vdf_path is None:
        warn("Could not find a Steam userdata directory. Add the shortcut manually.")
        return False
    config_dir = vdf_path.parent
    exe = f'"{exe_path}"'
    unsigned_appid, _ = compute_appid(exe, "PokeTrack")

    icon_path = config_dir / "grid" / f"{unsigned_appid}_icon.png"
    icon_ok = download_art(f"{ART_BASE_URL}/square.png", icon_path)
    download_art(f"{ART_BASE_URL}/vert.png", config_dir / "grid" / f"{unsigned_appid}p.png")
    download_art(f"{ART_BASE_URL}/horiz.png", config_dir / "grid" / f"{unsigned_appid}_hero.png")

    shortcuts = load_shortcuts(vdf_path)
    add_shortcut(shortcuts, exe, "PokeTrack", f'"{start_dir}"', "--fullscreen",
                 icon=str(icon_path) if icon_ok else "")
    config_dir.mkdir(parents=True, exist_ok=True)
    vdf_path.write_bytes(_serialize_dict(shortcuts))
    return True


def download_examples(dest, release):
    tag = release["tag_name"]
    with tempfile.TemporaryDirectory() as tmp:
        tar_path = Path(tmp) / "src.tar.gz"
        url = f"https://github.com/{REPO}/archive/refs/tags/{tag}.tar.gz"
        download_with_progress(url, tar_path, "Downloading examples...")
        try:
            with tarfile.open(tar_path) as t:
                members = [m for m in t.getmembers() if "/examples/" in m.name]
                t.extractall(tmp, members=members)
            src = next(Path(tmp).glob("*/examples"))
        except (tarfile.TarError, StopIteration) as e:
            die(f"Could not find examples/ in the {tag} source archive: {e}")
        shutil.copytree(src, dest, dirs_exist_ok=True)


def main():
    if shutil.which("zenity") is None:
        print("zenity is required", file=sys.stderr)
        sys.exit(1)

    install_root = choose_dir("Choose a directory for PokeTrack", Path.home())
    dest = install_root / "poketrack"

    release = latest_release()
    tag = release["tag_name"]
    asset_url = next((a["browser_download_url"] for a in release["assets"]
                       if a["name"] == ASSET), None)
    if not asset_url:
        die(f"Could not find asset {ASSET} in release {tag}")

    with tempfile.TemporaryDirectory() as tmp:
        zip_path = Path(tmp) / ASSET
        download_with_progress(asset_url, zip_path, f"Downloading PokeTrack {tag}...")
        dest.mkdir(parents=True, exist_ok=True)
        try:
            with zipfile.ZipFile(zip_path) as z:
                z.extractall(dest)
        except zipfile.BadZipFile as e:
            die(f"Downloaded file isn't a valid zip: {e}")

    bin_path = dest / "poketrack"
    if not bin_path.exists():
        die(f"{ASSET} didn't contain a poketrack binary as expected")
    bin_path.chmod(bin_path.stat().st_mode | 0o111)

    run_dir = choose_dir("Choose a directory to run PokeTrack from (examples go here)",
                          dest, exit_on_cancel=False)

    if question("Download example songs and instruments too?"):
        try:
            download_examples(run_dir, release)
        except InstallError as e:
            warn(f"Skipping examples: {e}")

    shortcut_added = False
    if question("Add PokeTrack as a non-Steam game?"):
        try:
            shortcut_added = add_steam_shortcut(bin_path, run_dir)
        except InstallError as e:
            warn(f"Skipping Steam shortcut: {e}")

    msg = f"PokeTrack {tag} installed to:\n{dest}"
    if run_dir != dest:
        msg += f"\nRun-in directory: {run_dir}"
    if shortcut_added:
        msg += "\n\nRestart Steam to see it in your library. Launch option --fullscreen was set."
    info(msg)


if __name__ == "__main__":
    try:
        main()
    except InstallError as e:
        zenity("--error", "--text", str(e), "--width=400")
        sys.exit(1)
    except Exception as e:
        zenity("--error", "--text", f"Unexpected error: {e}", "--width=400")
        raise
