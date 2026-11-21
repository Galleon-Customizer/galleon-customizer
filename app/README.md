# Galleon Customizer (app)

Daemon and settings window for the Corsair Galleon 100 SD Stream Deck module
(12 LCD keys, 2 dials, LCD screen) on Linux and Windows.

## Origin

This is a fork of [GalleonCore](https://github.com/Paullux/GalleonCore) v0.3.0
(commit `0174ec1`) by Paul Woisard, under GPL-3.0-or-later. The original copyright
notices are kept in every file; changes since the fork are by Minh Spark.

Changes from GalleonCore 0.3.0:

- Renamed: `galleond` → `galleon-customizerd`, `galleon-bosun` → `galleon-customizer`,
  user service `galleon-customizerd.service`, D-Bus `org.galleoncustomizer.*`,
  folders `~/.config/galleon-customizer`, `~/.local/share/galleon-customizer`,
  `~/.cache/galleon-customizer`. C++ namespaces and internal names are unchanged.
- A dial *turn* may be `{"type": "theme"}`: one page per detent.
- New key actions: `{"type": "combo", "keys": "super+shift+s"}` (1 to 5 keys) and
  `{"type": "text", "text": "/clear", "enter": true}` (printable ASCII, US layout).
- A picture file (`"icon": "name.png"`, next to `pages.json`) without a label fills
  the whole key, as in the Stream Deck app.
- Update checks look at this project's GitHub releases, not GalleonCore's.
- Not copied: GalleonCore's website, CI, Linux packaging (deb, rpm, AUR, AppImage)
  and release notes.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGALLEON_SOURCE_TREE_PATHS=OFF -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j"$(nproc)" && ctest --test-dir build && sudo cmake --install build
```

Needs Qt 6.4+ (Core, Gui, Network, Qml, Quick, Widgets, WebSockets, DBus; Test for the tests), hidapi and libusb-1.0.
Technical notes: `docs/` (protocol of the module in `docs/protocol/`).

## License

GPL-3.0-or-later, see `LICENSE` and `LICENSE.md`. Wallpapers, screensavers and some
pictures are CC-BY-SA-4.0; see `REUSE.toml` and `ASSETS.md`.
