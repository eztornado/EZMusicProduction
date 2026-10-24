#!/usr/bin/env bash
# Downloads the Magenta Studio "Generate" models used by Cadence's AI clip
# generator and installs them where the engine looks for them.
#
#   ./tools/fetch_magenta_models.sh [destination]
#
# Default destination: $CADENCE_AI_MODELS, falling back to
#   ~/Music/EZMusicProduction/AI Models
#
# The archive (~260 MB) contains:
#   mel_4bar_med_lokl     MusicVAE melody checkpoint (4 bars, 90-class)
#   drums_4bar_med_lokl   MusicVAE drums checkpoint  (4 bars, 10/512-class)
#
# Models are Apache-2.0, (c) Google LLC / the Magenta team. See
# docs/design/ai-generation.md for attribution and licensing.

set -euo pipefail

URL="https://storage.googleapis.com/magenta-studio/models/generate.zip"

if [[ $# -ge 1 && -n "${1:-}" ]]; then
  DEST="$1"
else
  DEST="${CADENCE_AI_MODELS:-$HOME/Music/EZMusicProduction/AI Models}"
fi

for tool in curl unzip; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "error: '$tool' is required" >&2
    exit 1
  fi
done

tmp="$(mktemp -d "${TMPDIR:-/tmp}/cadence-magenta.XXXXXX")"
cleanup() { rm -rf "$tmp"; }
trap cleanup EXIT

echo "Downloading $URL"
curl -fL --progress-bar "$URL" -o "$tmp/generate.zip"

echo "Extracting..."
unzip -q "$tmp/generate.zip" -d "$tmp/unpacked"

mkdir -p "$DEST"
# The zip nests everything under models/. Accept both layouts.
if [[ -d "$tmp/unpacked/models" ]]; then
  cp -R "$tmp/unpacked/models/." "$DEST/"
else
  cp -R "$tmp/unpacked/." "$DEST/"
fi

missing=0
for name in mel_4bar_med_lokl drums_4bar_med_lokl; do
  if [[ -f "$DEST/$name/config.json" ]]; then
    echo "ok: $name"
  else
    echo "warning: $name not found under $DEST" >&2
    missing=1
  fi
done

if [[ $missing -ne 0 ]]; then
  echo "Installation incomplete. Check the archive layout in $DEST." >&2
  exit 1
fi

echo
echo "Installed Magenta models in: $DEST"
echo "Set CADENCE_AI_MODELS to this path if you use a different location."
