#!/bin/sh
# Ad-hoc signs the app so macOS will run the nested helper executables.
#
# Apple Silicon validates code inside an app bundle. Nested code copied in after
# the main executable was linked is rejected with SIGKILL unless it is signed.
# Order matters: deepest code first, the app bundle last.
#
# Release builds must use a Developer ID identity and notarize instead.
set -e
app="$1"
[ -d "$app" ] || exit 0
command -v codesign >/dev/null 2>&1 || exit 0

sign() {
  [ -e "$1" ] || return 0
  codesign --force --sign - "$1" >/dev/null 2>&1 || true
}

framework="$app/Contents/Frameworks/Chromium Embedded Framework.framework"
if [ -d "$framework" ]; then
  # Sign nested helpers inside the framework first.
  for nested in "$framework"/Helpers/*.app "$framework"/Libraries/*.dylib; do
    [ -e "$nested" ] && sign "$nested"
  done
  sign "$framework"
fi

if [ -d "$app/Contents/Frameworks" ]; then
  for helper in "$app/Contents/Frameworks"/*.app; do
    [ -e "$helper" ] || continue
    for nested in "$helper"/Contents/Frameworks/*.framework; do
      [ -e "$nested" ] && sign "$nested"
    done
    sign "$helper"
  done
fi

helper="$app/Contents/Resources/liora-net"
[ -f "$helper" ] && sign "$helper"

sign "$app"
exit 0
