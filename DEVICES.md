This is device-specific directions for setting up raypoketrack.

### basic idea

The basic idea is you need a Linux-based device. It will work on windows/mac, but that should be pretty straightforward. There are prebuilt versions for arm64 drm/x11 that should work on most devices. Adding `--fullscreen` CLI flag will improve experience on most devices.

For many cheap ARM handhelds, I like [dArkOS](https://github.com/christianhaitian/dArkOS).

### steamdeck

This is a x86-64 device, running SteamOS (Arch-based linux.) Setup is fairly straightforward. Check out [this video](https://youtu.be/d_ZJUXr0rRQ).


### R36Max

This is a cheap clone, that you can get all over (I got [this](https://www.amazon.com/gp/product/B0G3PB3R5K) for $43, on sale.)

- Essentially, just follow [these directions](https://www.reddit.com/r/R36S/comments/1q2mgtk/darkos_in_clon_r36s/). Make note of the comment about the download for boot-partition, as it will save you some time.
- go to "EmulationStation" section and finish setting it up.


### EmulationStation

Essentially, this is a popular joystick-driven chooser for retro-games. raypoketrack is a "port".

- Add files in [ports/](./ports/) to your roms dir.
- Restart EmulationStation
- Run `Update RayPokeTrack` to get latest version
- Run `RayPokeTrack`