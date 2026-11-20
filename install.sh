#!/bin/sh
# Install the Galleon 100 SD key settings from this repo into the current user.
#   ./install.sh            copy config, scripts, launchers, user service
#   ./install.sh --system   also install the udev rule (sudo)
set -eu
R="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications" \
         "$HOME/.config/galleoncore" "$HOME/.config/systemd/user"
install -m 755 "$R"/bin/* "$HOME/.local/bin/"
for f in "$R"/applications/*.desktop; do
  sed "s#@HOME@#$HOME#g" "$f" > "$HOME/.local/share/applications/$(basename "$f")"
done
P="$HOME/.config/galleoncore/pages.json"
[ -f "$P" ] && cp "$P" "$P.bak"
sed "s#@HOME@#$HOME#g" "$R/config/pages.json" > "$P"
chmod 600 "$P"   # app keys are disabled if others can write the pages file
install -m 644 "$R/systemd/galleond.service" "$HOME/.config/systemd/user/"
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
if [ "${1:-}" = "--system" ]; then
  sudo install -m 644 "$R/udev/70-galleon.rules" /etc/udev/rules.d/
  sudo udevadm control --reload && sudo udevadm trigger
fi
systemctl --user daemon-reload
systemctl --user enable --now galleond.service
systemctl --user restart galleond.service
echo "Done. Galleon settings installed."
