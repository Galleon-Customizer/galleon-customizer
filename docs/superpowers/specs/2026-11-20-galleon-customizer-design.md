# Galleon Customizer — design

Status: draft for approval · Date: 2026-11-20

## Intent

A Linux desktop app that lets anyone with a Corsair Galleon 100 SD customise the
keyboard's built-in Stream Deck module (12 LCD keys, 2 dials, the LCD strip) as
fully as the official Stream Deck software allows on Windows/macOS, with a nicer
interface than GalleonCore's Bosun, in English and Vietnamese.

Why: the owner wants a customisation tool for themselves and for other Galleon
owners on Linux ("cho mọi người dùng"). Today GalleonCore only lets a key start a
`.desktop` file, play media, switch page or type keypad digits; it deliberately
refuses commands. Everything built so far in this repo (screenshot/recording
keys, page switching on the left dial) works around that with launchers and a
local patch.

## Goals

1. Feature parity with the official Stream Deck app's *core* features:
   - key actions: open app / file / folder / URL, run a command or script,
     send a hotkey, type text, media control, system actions, page switch,
     folder (sub-page), back, multi-action (sequence with delays), toggle
     (two states, each with its own icon and title);
   - press, long press (separate action) on keys; turn left / turn right /
     press on each dial; dials configurable per page;
   - key appearance: icon from image file, emoji or icon theme; background
     colour; title text (font size, position, colour, show/hide); live titles
     (clock, CPU %, state of a toggle);
   - unlimited pages, folders, multiple profiles, automatic profile/page switch
     when a given application is focused;
   - import / export a profile as one file to share it;
   - an action library with search, and plugins for new action types.
2. A polished GNOME-native editor: live preview of the 12 keys, the 2 dials and
   the LCD strip; drag and drop actions onto keys; changes apply instantly.
3. English and Vietnamese UI, following the system language, switchable in the app.
4. Ships built-in actions for capture and productivity. Already working in
   this repo: full/region/delayed screenshots (`bin/galleon-shot`) and screen
   recording (`bin/galleon-screenrec`). New in this project: window recording,
   recording with microphone, colour picker, OCR (tesseract `eng`+`vie`),
   system toggles and clipboard tools.

## Non-goals

- Windows or macOS.
- Standalone Elgato Stream Deck devices (only the Galleon 100 SD module).
- Compatibility with Elgato Marketplace / Stream Deck SDK plugins.
- Keyboard RGB of the main keyboard part (`1b1c:2b0c`) in this project.
- Replacing or forking GalleonCore; this app is an alternative to it.

## Anti-goals (failure even if it "works")

- Anything that can freeze or break typing on the keyboard. The app never
  writes to the keyboard part `2b0c`, and only sends documented reports to the
  Stream Deck part `2b18`.
- A shared profile silently running commands on someone else's machine.
- Needing root to run day to day.
- An editor that looks or behaves unlike a GNOME app (custom-drawn chrome,
  non-native dialogs).

## Constraints and facts

- Hardware (from GalleonCore `lib/include/galleon/Protocol.h`, its
  `docs/protocol/README.md`, galleon-deck `docs/PROTOCOL.md`, and
  node-elgato-stream-deck ≥ 7.5.0, which supports the Galleon):
  - Stream Deck module `1b1c:2b18`, HID interface 0. Input report ID 1
    (512 bytes) carries key and dial events. Output report ID 2 (1024 bytes)
    carries image data. Feature report ID 3 (32 bytes) carries commands.
    Feature report 5 returns the firmware version.
  - Keys: 12, 3 columns × 4 rows, row-major from the top left. Key image:
    160 × 160 JPEG. LCD strip: 720 × 384 JPEG. 2 dials, 4 ring LEDs each.
  - The module stays in host mode only while a keep-alive feature report
    (ID 3, `0x27` …) is sent every 500 ms; otherwise it falls back to its
    built-in numpad mode.
  - Brightness is a feature report (ID 3, `0x08`, value).
  - Protocol details are to be re-implemented in Python from these sources.
    GalleonCore's `libgalleon` is LGPL-3.0; galleon-deck and
    node-elgato-stream-deck are documentation and cross-checks.
- Only one program can drive the module. Galleon Customizer replaces `galleond`:
  the installer disables the `galleond` user service, and the daemon refuses to
  start, with a clear message, if another program holds the device.
- Device access without root: udev rule with `TAG+="uaccess"` for `2b18`
  (already in this repo as `udev/70-galleon.rules`).
- Hotkeys and typed text go through a virtual keyboard on `/dev/uinput`
  (udev `uaccess` rule; the module must be loaded); this is the only method
  that works on Wayland and it also works on X11.
- The focused application: X11 through EWMH (`_NET_ACTIVE_WINDOW`); KDE
  Wayland through a small KWin script; GNOME Wayland through a small GNOME
  Shell extension; other Wayland compositors through their IPC where it exists
  (Sway, Hyprland), otherwise the feature is unavailable.
- Screen recording goes through the xdg-desktop-portal ScreenCast (as the
  current `galleon-screenrec` does), with its restore token. Full screenshots
  and the colour picker use the portal Screenshot interface; region capture
  with annotation uses flameshot when installed (optional dependency).
- Development hardware: the owner's Galleon 100 SD, firmware v2.3.88 (the same
  version GalleonCore's measurements were taken on). Phase 1 starts with a
  capture tool run on that device to record input reports for the tests. The
  daemon reads the firmware version (feature report 5): known versions run
  normally; unknown versions run with a warning in the editor.
- The LCD strip is display-only (not touch), and the official software splits
  it into four quadrants, two per dial. The configuration model has, from
  phase 1, a per-dial feedback area on the strip (e.g. the volume level while
  turning), so strip layouts in phase 3 need no schema migration.
- Licence: GPL-3.0-or-later (this repo already ships GPL material and patches).

## Approaches considered

- **A. Own driver and daemon, replacing GalleonCore (chosen).** Python talks
  to the module over HID, renders keys, runs actions; a separate GTK editor
  edits the configuration. Only approach that reaches parity: commands,
  hotkeys, toggles, long press, live titles, app-aware switching, plugins.
  Cost: most work, and we own hardware support (firmware changes may break it).
- B. Extend GalleonCore (C++/Qt) and keep Bosun's role for our app. Rejected:
  upstream refuses commands by design, so it means maintaining a fork in
  another stack and merging every release.
- C. Keep GalleonCore unchanged and add launchers around it. Rejected: no
  live titles, toggles, long press or fast page switching; cannot reach parity.

The owner picked A.

## Platform support (revised 2026-11-20)

The owner wants every Linux distribution and desktop, used as installed
("default settings, no custom theme"). Consequences:

- **Standards first.** Anything with a cross-desktop standard uses it and
  nothing else: xdg-desktop-portal (screenshot, screen recording, colour
  picker, open URI), MPRIS (media), PipeWire or PulseAudio (`wpctl`, else
  `pactl`), freedesktop notifications, `loginctl` (lock), `systemctl`
  (suspend), `.desktop` files and `Gio.AppInfo` (apps), uinput (hotkeys and
  typed text on X11 and Wayland alike).
- **Desktop backends** only for what has no standard (dark mode, do not
  disturb, night light, power dialog, focused app):
  - tier 1, tested every release: GNOME and KDE Plasma, Wayland and X11;
  - tier 2, best effort: Cinnamon, XFCE, MATE, Budgie, wlroots compositors
    (Sway, Hyprland) through a generic backend;
  - an action the current desktop cannot do is shown greyed out with the
    reason in the editor and is never put in the default profile; the
    default profile uses standard actions only.
- **No dependency on the user's setup.** The app ships its own icon set for
  every built-in action and a bundled font for key titles, so keys look the
  same with any icon theme or font configuration. Optional tools (flameshot,
  tesseract, OBS) only add extra actions; their absence never breaks a
  default key.
- **Packaging.** Primary: Flatpak (Flathub) — one package for every
  distribution, bringing GTK 4, libadwaita, Python, hidapi, Pillow, GStreamer
  and tesseract with it. The device needs a udev rule on the host, which a
  sandbox cannot install: on first launch the app detects missing access and
  shows the single command to run (`pkexec` helper that writes
  `70-galleon-customizer.rules` and the uinput rule), with an explanation.
  Secondary: `.deb`, `.rpm` and an AUR package built from the same source, for
  people who avoid Flatpak; `install.sh` stays for development.
- **Editor look.** GTK 4 + libadwaita is kept: inside the Flatpak it looks the
  same on every desktop and follows the system light/dark preference through
  the portal. A Qt/Kirigami editor was considered for a native look on KDE and
  rejected: two UI toolkits would double the work for little gain.
- **Testing matrix** for each release: Ubuntu LTS (GNOME), Fedora (GNOME and
  KDE), Arch (KDE), Linux Mint (Cinnamon), each on a fresh install with
  default settings; GNOME and KDE each on Wayland and on X11.

## Design

### Components

1. **`galleon_customizer.device`** — pure protocol layer: build feature/output
   reports, cut JPEGs into 1024-byte image reports, parse input reports into
   events (key down/up, dial turn with steps, dial press/release). No I/O, fully
   unit-tested with recorded byte sequences.
2. **`galleon-customizerd`** — the daemon, a systemd *user* service started with the
   graphical session:
   - opens `/dev/hidraw*` for `1b1c:2b18` interface 0 with `hidapi`; sends the
     keep-alive every 500 ms; reconnects on unplug/replug and on resume;
   - renders keys (Pillow): background, icon, title, state, pressed feedback;
     renders the LCD strip (page name, clock, now playing — a configurable
     "strip layout");
   - runs actions in a worker pool so a slow action never blocks input or the
     keep-alive; commands run detached, with a timeout for status checks;
   - watches the configuration and applies changes immediately;
   - exposes a D-Bus interface on the session bus
     (`io.github.galleon_customizer.GalleonCustomizer1`: current profile/page, set page, press key, device status,
     preview images, reload) used by the editor and the shell extension;
   - holds the virtual keyboard (python-evdev `UInput`) for hotkey/text actions.
3. **`galleon-customizer`** — the editor, GTK 4 + libadwaita (PyGObject):
   - left: profiles and pages (tree with folders); centre: a picture of the
     module — LCD strip, 3 × 4 keys, 2 dials — with the device's real rendering
     fetched from the daemon; right: action library (searchable, grouped) and
     the inspector for the selected key/dial;
   - drag an action from the library onto a key or dial; drag keys to reorder
     or swap; right-click: copy, paste, clear;
   - inspector: per-state icon, colour, title, font size, position; the action's
     own settings (generated from the action's schema);
   - works without the device plugged in (preview only) and tells the user when
     the daemon is not running;
   - preferences: language (system / English / Tiếng Việt), brightness,
     sleep timeout, start at login.
4. **Built-in actions** (each an action type with a JSON schema for its settings):
   - Apps & files: open app (pick from installed apps), open file/folder,
     open URL, run command (shell string or argv, optional "run in terminal").
   - Keyboard: hotkey (combination), type text, both via uinput.
   - Media & sound: play/pause, next, previous, stop (MPRIS); volume up/down/set,
     mute speaker, mute microphone (PipeWire `wpctl`); dial actions for volume
     and seek.
   - Capture: full screenshot, region, delayed, open last capture, screen
     recording toggle (screen / window, with or without mic), colour picker,
     OCR to clipboard (tesseract, English + Vietnamese) — ported from this repo.
   - System (GNOME): lock, suspend, power-off dialog, dark mode, do not disturb,
     night light, open settings, terminal, system monitor.
   - Clipboard & text: copy date/time, generate password, search / translate /
     open the clipboard, paste as plain text, clear clipboard.
   - Navigation: go to page, next/previous page, open folder, back, switch
     profile.
   - Composition: multi-action (list of actions with delays), toggle (two
     actions + two looks; state remembered; optionally tied to a status
     command so the key shows the real state).
   - Live titles: clock/date, CPU, RAM, GPU temperature (when available),
     output of a command run every N seconds (min 1 s).
5. **Plugins** — Python packages dropped in
   `~/.local/share/galleon-customizer/plugins/` (and system-wide in
   `/usr/share/galleon-customizer/plugins/`), each declaring action types: id,
   name (translatable), icon, settings schema, `run()`, optional live title.
   Built-in actions use the same API. Plugins run in the daemon's process
   (trusted code the user installed).
6. **Focused-app helpers** (small, optional) — a GNOME Shell extension and a
   KWin script that send the focused app ID to the daemon over D-Bus; X11 and
   Sway/Hyprland need no helper. Without one, automatic switching is shown as
   unavailable in the editor.

### Configuration

- `~/.config/galleon-customizer/config.json` — global settings (language,
  brightness, active profile, sleep timeout).
- `~/.config/galleon-customizer/profiles/<id>/profile.json` + `icons/` — one folder
  per profile: pages (each: 12 key slots, dial settings per page or inherited
  from the profile), folders as pages with a parent, app-match rules.
- JSON with a `schema_version`; the daemon migrates older files and keeps a
  `.bak`. The editor writes atomically; the daemon reloads on change.
- Export: `<name>.galleonprofile` (zip of the profile folder). Import shows the
  list of command / hotkey / text actions it contains and asks the user to
  confirm before enabling them (anti-goal: silent commands).
- A first-run import of the GalleonCore `pages.json` and of this repo's current
  pages, so existing setups carry over.

### Look of the editor (text sketch)

```
┌ Galleon Customizer ────────────────────────────────────── [≡] ┐
│ Profiles   │  ┌────────── LCD strip 720×384 ──────────┐ │ Actions  🔍  │
│ ▸ Default  │  └───────────────────────────────────────┘ │ ▸ Capture    │
│   Chụp&Quay│  [key][key][key]          (dial L)  (dial R)│ ▸ System     │
│   Hệ thống │  [key][key][key]                           │ ▸ Media      │
│   Tiện ích │  [key][key][key]                           │ ▸ Keyboard   │
│ ▸ OBS      │  [key][key][key]                           │ ▸ Plugins    │
│ + page     │                                             │─ Inspector ──│
│            │                                             │ icon, title… │
└────────────────────────────────────────────────────────────────────────┘
```

### Languages

gettext (`.po` files) for the editor, the daemon's notifications and action
names; English source strings, Vietnamese translation shipped. Default: system
locale; override in preferences.

### Packaging and install

See "Platform support": Flatpak first, then `.deb`/`.rpm`/AUR; `install.sh`
for development. The phase 1 daemon must already run from the Flatpak
(`flatpak run --command=galleon-customizerd …`) with `--device=all`,
`--talk-name=org.freedesktop.portal.*`, `--socket=pulseaudio` and access to
`/dev/uinput`.

### Delivery in phases

1. **Core**: protocol layer + daemon + JSON config + built-in actions
   (everything above except plugins, live titles, app switching, LCD strip
   layouts) + migration from GalleonCore. Usable by editing JSON.
2. **Editor**: the GTK app with drag and drop, inspector, preview, EN/VI.
3. **Advanced**: plugins, live titles, toggles tied to status commands, LCD
   strip layouts, GNOME Shell extension and app-aware switching, profile
   import/export, `.deb` releases.

### Testing

- Protocol: unit tests on recorded input reports and expected output bytes.
- Daemon: a fake device backend (records sent reports, injects events) so
  rendering, actions and page logic are tested without hardware.
- Editor: widget tests for the model; screenshots of the main window in both
  languages.
- Manual hardware checklist per release (keys, dials, strip, replug, suspend).

## Decided by the owner

- Approach A (own daemon replacing GalleonCore).
- Scope: as complete as the official Stream Deck app ("tất cả khả năng").
- English and Vietnamese.
- A nicer interface than Bosun.
- Public repo, no personal information.
- Name: **Galleon Customizer** (noun form chosen over "Customize Galleon").
- Home: GitHub organisation `galleon-customizer` (free plan), repo
  `galleon-customizer/galleon-customizer` (this repo, transferred and renamed;
  the current key setup moves to `examples/`).
- App ID `io.github.galleon_customizer.GalleonCustomizer`; commands
  `galleon-customizer` (editor) and `galleon-customizerd` (daemon).
- Runs on any Linux distribution and desktop, and works out of the box on a
  stock install: someone who never changed their desktop settings and has
  installed nothing extra must get a fully working default profile.

## Calls made by the designer (open to change)

- Python + GTK 4 + libadwaita; Pillow for rendering; hidapi; python-evdev.
- Flatpak as the primary package; tier 1 desktops GNOME and KDE Plasma.
- Bundled icon set and font for key rendering.
- Daemon + editor split, talking over session D-Bus.
- Plugins as Python packages, trusted, in-process.
- Import of shared profiles asks before enabling commands/hotkeys/text.
- Phase order above.
- Firmware: known version runs, unknown version warns (not refused).
- LCD strip per-dial feedback areas in the config model from phase 1.

## Left to the builder

- Exact D-Bus method signatures and profile JSON field names.
- Rendering details (fonts, padding, pressed animation).
- Worker pool sizing and command timeouts.
- How the editor previews keys when the daemon is not running.
