#!/bin/sh
# Embeds Chromium into the app bundle: the framework plus the helper apps
# Chromium launches for renderer, GPU and utility processes.
#
#   bundle-cef.sh <app bundle> <cef root>
set -eu
app="$1"
cef="$2"
frameworks="$app/Contents/Frameworks"
exe="$app/Contents/MacOS/liora"

[ -d "$app" ] || { echo "no app bundle at $app" >&2; exit 1; }
[ -d "$cef/Release/Chromium Embedded Framework.framework" ] || {
  echo "no CEF framework under $cef" >&2
  exit 1
}
[ -f "$exe" ] || { echo "no executable at $exe" >&2; exit 1; }

mkdir -p "$frameworks"
rm -rf "$frameworks/Chromium Embedded Framework.framework"
cp -R "$cef/Release/Chromium Embedded Framework.framework" "$frameworks/"

make_helper() {
  name="$1"
  bundle_id="$2"
  helper="$frameworks/$name.app"
  rm -rf "$helper"
  mkdir -p "$helper/Contents/MacOS"
  cp "$exe" "$helper/Contents/MacOS/$name"
  # Chromium loads the framework relative to the helper executable, so each
  # helper points at the app's Frameworks directory.
  ln -s ../.. "$helper/Contents/Frameworks"
  cat > "$helper/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>$name</string>
  <key>CFBundleIdentifier</key><string>$bundle_id</string>
  <key>CFBundleName</key><string>$name</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSUIElement</key><true/>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
}

make_helper "Liora Helper" "io.liora.browser.helper"
make_helper "Liora Helper (GPU)" "io.liora.browser.helper.gpu"
make_helper "Liora Helper (Renderer)" "io.liora.browser.helper.renderer"
make_helper "Liora Helper (Alerts)" "io.liora.browser.helper.alerts"
