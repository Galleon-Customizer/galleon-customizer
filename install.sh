#!/bin/sh
# Install Galleon Customizer and the Galleon 100 SD key settings from this repo.
#   ./install.sh            copy config, scripts, launchers, user service
#   ./install.sh --app      also build the app in app/ and install it in /usr/local (sudo)
#   ./install.sh --system   also install the udev rule (sudo)
set -eu
R="$(cd "$(dirname "$0")" && pwd)"
C="$HOME/.config/galleon-customizer"

for arg in "$@"; do
  if [ "$arg" = "--app" ]; then
    B="$HOME/.cache/galleon-customizer-build"
    cmake -S "$R/app" -B "$B" -DCMAKE_BUILD_TYPE=Release -DGALLEON_SOURCE_TREE_PATHS=OFF -DCMAKE_INSTALL_PREFIX=/usr/local
    cmake --build "$B" -j"$(nproc)"
    ctest --test-dir "$B" -j"$(nproc)" --output-on-failure
    sudo cmake --install "$B"
  fi
done

# Settings of the GalleonCore era move once to the new folders.
if [ -d "$HOME/.config/galleoncore" ] && [ ! -d "$C" ]; then
  cp -a "$HOME/.config/galleoncore" "$C"
  if [ -f "$C/galleond.conf" ]; then mv "$C/galleond.conf" "$C/galleon-customizerd.conf"; fi
fi
if [ -d "$HOME/.local/share/galleoncore" ] && [ ! -d "$HOME/.local/share/galleon-customizer" ]; then
  cp -a "$HOME/.local/share/galleoncore" "$HOME/.local/share/galleon-customizer"
fi
# The GalleonCore service and autostart entry are replaced by the ones below.
if [ -f "$HOME/.config/systemd/user/galleond.service" ]; then
  systemctl --user disable --now galleond.service 2>/dev/null || true
  rm -f "$HOME/.config/systemd/user/galleond.service"
fi
rm -f "$HOME/.config/autostart/galleon-bosun.desktop"

mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications" "$C" "$HOME/.config/systemd/user"
install -m 755 "$R"/bin/* "$HOME/.local/bin/"
for f in "$R"/applications/*.desktop; do
  sed "s#@HOME@#$HOME#g" "$f" > "$HOME/.local/share/applications/$(basename "$f")"
done
P="$C/pages.json"
if [ -f "$P" ]; then cp "$P" "$P.bak"; fi
sed "s#@HOME@#$HOME#g" "$R/config/pages.json" > "$P"
chmod 600 "$P"   # app keys are disabled if others can write the pages file
install -m 644 "$R"/config/icons/*.png "$C/"  # key pictures, read next to pages.json
install -m 644 "$R/systemd/galleon-customizerd.service" "$HOME/.config/systemd/user/"
mkdir -p "$HOME/.config/autostart"
install -m 644 "$R/autostart/galleon-customizer.desktop" "$HOME/.config/autostart/"  # tray at login
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
for arg in "$@"; do
  if [ "$arg" = "--system" ]; then
    sudo install -m 644 "$R/udev/70-galleon.rules" /etc/udev/rules.d/
    sudo udevadm control --reload && sudo udevadm trigger
  fi
done
systemctl --user daemon-reload
systemctl --user enable --now galleon-customizerd.service
systemctl --user restart galleon-customizerd.service
echo "Done. Galleon Customizer settings installed."
