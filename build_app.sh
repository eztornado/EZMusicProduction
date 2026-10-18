#!/bin/bash
# EZMusicProduction build script: engine + Flutter + package
set -e
cd "$(dirname "$0")"

# The brew Flutter (3.47+) needs macOS 14; this Mac runs 13 — pin the SDK.
# The pod wrapper runs CocoaPods with Homebrew's portable ruby (brew install
# hung on this network; the gem install needed the stdckdint shim — done).
export PATH="$HOME/.local/bin:$HOME/development/flutter/bin:$PATH"

echo "=== 1/3 Engine (C++/JUCE) ==="
cmake --build build -j $(sysctl -n hw.ncpu)

echo "=== 2/3 Flutter (UI) ==="
cd ui
# Versionado dinámico estilo Ubuntu: YY.MM.DD.N (N = build del día).
VER_DATE=$(date +%y.%m.%d)
COUNTER_FILE=".build_version_count"
LAST_DATE_FILE=".build_version_date"
LAST_DATE=$(cat "$LAST_DATE_FILE" 2>/dev/null || echo "")
if [ "$LAST_DATE" = "$VER_DATE" ]; then
  N=$(( $(cat "$COUNTER_FILE" 2>/dev/null || echo 0) + 1 ))
else
  N=1
fi
echo "$VER_DATE" > "$LAST_DATE_FILE"
echo "$N" > "$COUNTER_FILE"
mkdir -p lib/generated
cat > lib/generated/version.dart <<EOF
// GENERADO por build_app.sh — no editar a mano.
const String kAppVersion = '$VER_DATE.$N';
EOF
echo "versión: $VER_DATE.$N"
# The engine dylib copied in after the previous build breaks Xcode's
# codesign ("subcomponent not signed") — remove it first so flutter build
# signs a clean bundle.
APP=build/macos/Build/Products/Release/EZMusicProduction.app
rm -f "$APP/Contents/MacOS/libcadence_engine.dylib"
flutter build macos --release

echo "=== 3/3 Package (copy engine dylib into app bundles) ==="
# Copying the dylib AFTER flutter signed the bundle breaks the seal, so the
# bundle must be re-signed. Order matters: the dylib FIRST (re-signing the app
# with an unsigned subcomponent fails with "code object is not signed at all").
# This used to be a silent `|| true`, which left the app with a broken seal
# ("a sealed resource is missing or invalid") on every build.
sign_bundle() { # $1 = ruta del .app
  echo "firmando $1"
  codesign --force --sign - "$1/Contents/MacOS/libcadence_engine.dylib"
  codesign --force --sign - "$1"
  codesign -v "$1"
}
APP=build/macos/Build/Products/Release/EZMusicProduction.app
cp ../build/engine/libcadence_engine.dylib "$APP/Contents/MacOS/"
sign_bundle "$APP"
# The Debug bundle (left over from `flutter run` sessions) must not keep a
# stale engine: launching it would silently run old code.
DEBUG_APP=build/macos/Build/Products/Debug/EZMusicProduction.app
if [ -d "$DEBUG_APP" ]; then
  cp ../build/engine/libcadence_engine.dylib "$DEBUG_APP/Contents/MacOS/"
  sign_bundle "$DEBUG_APP"
fi

echo "=== Done ==="
echo "App: $(pwd)/$APP"
echo "Abrir: open $APP"
