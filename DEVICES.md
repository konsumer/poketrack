This is device-specific directions for setting up raypoketrack.

### basic idea

The basic idea is you need a Linux-based device. It will work on windows/mac, but that should be pretty straightforward. There are prebuilt versions for arm64 drm/x11 that should work on most devices. Adding `--fullscreen` CLI flag will improve experience on most devices.

### steamdeck

This is a x86-64 device, running SteamOS (Arch-based linux.) Setup is fairly straightforward. Check out [my video](https://youtu.be/d_ZJUXr0rRQ) for setup instructions.

### cheap handhelds (R36Max, R36S, etc.)

These are gameboy-style ARM handhelds. I use the [R36Max](https://www.amazon.com/gp/product/B0G3PB3R5K) (~$43) with [dArkOS](https://github.com/christianhaitian/dArkOS).

Notes for dArkOS / Rockchip BSP kernel (4.4.x):
- Use the `raypoketrack-linux-arm64-sdl` build — the DRM build does not work due to EGL incompatibility with the 4.4 kernel
- The `Update RayPokeTrack` port script selects the SDL build automatically on these devices
- WiFi: built-in WiFi may not work; a **Realtek RTL8188EU** USB adapter (via OTG) works reliably
- Button mapping: if face buttons are swapped, edit `SDL_GAMECONTROLLERCONFIG` in `RayPokeTrack.sh` — swap `a`/`b` and `x`/`y` values to match your device's physical layout
- SSH: default credentials are `ark` / `ark`. You can use [this script](https://gist.github.com/konsumer/880f6dfedb058763207053211fca858e) to enable SSH (put it on rom SD, in ports/ or tools/)

### EmulationStation

Essentially, this is a popular joystick-driven chooser for retro-games. raypoketrack is a "port".

- Add files in [ports/](./ports/) to your roms dir.
- Restart EmulationStation
- Run `Update RayPokeTrack` to get latest version
- Run `RayPokeTrack`