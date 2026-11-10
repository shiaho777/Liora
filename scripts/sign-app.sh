#!/bin/sh
# Ad-hoc signs the app so macOS will run the nested helper executable.
#
# Apple Silicon validates code inside an app bundle. A helper copied into
# Resources after the main executable was linked is rejected with SIGKILL
# unless the bundle is signed as a whole. Release builds must replace this with
# a Developer ID signature and notarization.
set -e
app="$1"
[ -d "$app" ] || exit 0
command -v codesign >/dev/null 2>&1 || exit 0

helper="$app/Contents/Resources/liora-net"
if [ -f "$helper" ]; then
  codesign --force --sign - "$helper" >/dev/null 2>&1 || true
fi
codesign --force --sign - "$app" >/dev/null 2>&1 || true
exit 0
