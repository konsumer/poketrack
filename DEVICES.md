This is device-specific directions for setting up poketrack.

### basic idea

The basic idea is you need a Linux-based device. It will work on windows/mac, but that should be pretty straightforward. There are prebuilt versions for arm64 drm/x11 that should work on most devices. Adding `--fullscreen` CLI flag will improve experience on most devices.

### steamdeck

This is a x86-64 device, running SteamOS (Arch-based linux.) Setup is fairly straightforward. I made a script to make it a bit easier. In desktop mode:

## [Steamdeck Installer (Save As)](https://raw.githubusercontent.com/konsumer/poketrack/main/scripts/steamdeck-install.py)

This is a GUI installer script. Save it, and in your Downloads folder right-click the file, check "is executable" under Permissions, and double-click to run. It'll let you pick an install-location, optionally grab the example songs/instruments, and optionally add PokeTrack as a non-Steam game (with `--fullscreen` launch option already set). Restart Steam afterwards to see it in your library.

Check out [my video](https://youtu.be/d_ZJUXr0rRQ) for manual setup instructions.

Joystick (analog/dpad/etc) worked for me without any config, but since SELECT is next to dpad, I also added controller-maps so triggers act as START/SELECT (on opposite sides) which allows for easier SELECT + dpad.

### cheap handhelds (R36Max, R36S, etc.)

These are gameboy-style ARM handhelds. I use the [R36Max](https://handhelds.wiki/R36MAX) (generally under $50) with [dArkOS](https://github.com/christianhaitian/dArkOS).

Notes for dArkOS / Rockchip BSP kernel (4.4.x):
- Use the `poketrack-linux-arm64-sdl` build — the DRM build does not work due to EGL incompatibility with the 4.4 kernel
- WiFi: built-in WiFi may not work; a **Realtek RTL8188EU** USB adapter (via OTG) works reliably. You can often find these for like $1-$2, and are great to have in your toolbox.
- Button mapping: if face buttons are swapped, edit `SDL_GAMECONTROLLERCONFIG` in `PokeTrack.sh` — swap `a`/`b` and `x`/`y` values to match your device's physical layout
- You can use `--height` and `--width` to change size. for example make it square (720x720) for R36Max
- SSH: default credentials are `ark` / `ark`. You can use [this script](https://gist.github.com/konsumer/880f6dfedb058763207053211fca858e) to enable SSH (put it on rom SD, in ports/ or tools/)

Here is the exact process I did on Mac, but you can read [arkos4clone](https://github.com/lcdyk0517/arkos4clone) docs:

- Find the latest akos4clone dArkOS image [here](https://github.com/lcdyk0517/arkos4clone/releases) I used `dArkOS4Clone-20260625`, download all 3 7zip files
- extract with 7zip (I installed 7zip CLI with brew, and ran `7zz e`)
- use [raspberry pi imager](https://www.raspberrypi.com/software/) to burn the image to SD
- re-insert disk, and run the `dtb_selector` on BOOT, for your platform. I chose "XiFan R36Max"




### EmulationStation

Essentially, this is a popular joystick-driven chooser for retro-games. poketrack is a "port".

## [EmulationStation Installer (Save As)](https://raw.githubusercontent.com/konsumer/poketrack/main/scripts/ports/Update%20PokeTrack.sh)

- Add files in [scripts/ports/](./scripts/ports/) to your roms `ports/` dir.
- Restart EmulationStation
- Run `Update PokeTrack` to get latest version — it'll also offer to download the [example](https://github.com/konsumer/poketrack/tree/main/examples) songs/soundfonts/samples/plugins into your working dir
- Restart EmulationStation
- Run `PokeTrack` in ports