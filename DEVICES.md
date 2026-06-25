This is device-specific directions for setting up raypoketrack.

### basic idea

The basic idea is you need a Linux-based device. It will work on windows/mac, but that should be pretty straightforward. There are prebuilt versions for arm64 drm/x11 that should work on most devices. Adding `--fullscreen` CLI flag will improve experience on most devices.

### steamdeck

This is a x86-64 device, running SteamOS (Arch-based linux.) Setup is fairly straightforward. Check out [my video](https://youtu.be/d_ZJUXr0rRQ) for setup instructions.

### cheap handhelds

These are things that look like a gameboy. I got [this](https://www.amazon.com/gp/product/B0G3PB3R5K) for $43, on sale. They generally come with some random linux setup, but I like [dArkOS](https://github.com/christianhaitian/dArkOS). For that particular device, [follow these directions](https://www.youtube.com/watch?v=xszUofpKNRc). After that, follow EmulationStation directions, below.

### EmulationStation

Essentially, this is a popular joystick-driven chooser for retro-games. raypoketrack is a "port".

- Add files in [ports/](./ports/) to your roms dir.
- Restart EmulationStation
- Run `Update RayPokeTrack` to get latest version
- Run `RayPokeTrack`