# custom-galleon-100-sd-linux

Settings for the Corsair Galleon 100 SD Stream Deck keys on Ubuntu (GNOME Wayland),
driven by [GalleonCore](https://github.com/Paullux/GalleonCore) v0.3.0 built from source.

## Layout of the keys (page "Chụp & Quay", home page)

| | | |
|---|---|---|
| Chụp màn (full screenshot → ~/Pictures/Screenshots + clipboard) | Chụp vùng (select area) | Chụp 3s (delayed full screenshot) |
| Quay ●/■ (toggle screen recording → ~/Videos/Screencasts) | Ảnh (open screenshots folder) | Video (open recordings folder) |
| OBS | | |
| | | Nhạc (go to the media page) |

Dials:
- Left: turn = next/previous page, press = back to "Chụp & Quay".
- Right: turn = volume (player when one is playing, else system), press = mute.

## Files

- `config/pages.json` — keys and dials (`@HOME@` is replaced at install).
- `bin/galleon-shot` — screenshots via flameshot (`full`, `area`, `delay`).
- `bin/galleon-screenrec` — toggle recording via the screen-cast portal + GStreamer (NVENC, x264 fallback).
- `applications/*.desktop` — launchers the keys start (GalleonCore keys can only start `.desktop` files).
- `systemd/galleond.service` — user service for `/usr/local/bin/galleond`.
- `autostart/galleon-bosun.desktop` — starts Bosun at login, hidden in the notification area (`--minimized`; needs the AppIndicator extension on GNOME).
- `udev/70-galleon.rules` — lets the logged-in user open the keyboard.
- `patches/0001-dial-turn-steps-pages.patch` — GalleonCore patch: a dial *turn* may be `{"type":"theme"}` (one page per detent).

## Use

```sh
./install.sh            # copy settings into ~/.config, ~/.local, enable service
./install.sh --system   # also install the udev rule (sudo)
./save.sh               # copy live settings back here after editing keys in Bosun
```

Rebuilding GalleonCore with the patch:

```sh
git clone --branch v0.3.0 https://github.com/Paullux/GalleonCore.git && cd GalleonCore
git apply ~/custom-galleon-100-sd-linux/patches/0001-dial-turn-steps-pages.patch
cmake -B build -DCMAKE_BUILD_TYPE=Release -DGALLEON_SOURCE_TREE_PATHS=OFF -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j"$(nproc)" && ctest --test-dir build && sudo cmake --install build
```
