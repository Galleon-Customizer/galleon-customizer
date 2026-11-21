#!/bin/sh
# Copy the live settings (e.g. after editing keys in Bosun) back into this repo.
set -eu
R="$(cd "$(dirname "$0")" && pwd)"
sed "s#$HOME#@HOME@#g" "$HOME/.config/galleon-customizer/pages.json" > "$R/config/pages.json"
cp "$HOME/.local/bin/galleon-shot" "$HOME/.local/bin/galleon-screenrec" "$R/bin/"
for f in "$HOME"/.local/share/applications/galleon-*.desktop; do
  sed "s#$HOME#@HOME@#g" "$f" > "$R/applications/$(basename "$f")"
done
git -C "$R" status --short
