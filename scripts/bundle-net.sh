#!/bin/sh
# Copies the first existing liora-net build into the app bundle.
# Usage: bundle-net.sh <destination> <candidate>...
set -e
dest="$1"
shift
for candidate in "$@"; do
  if [ -f "$candidate" ]; then
    cp "$candidate" "$dest"
    chmod +x "$dest"
    exit 0
  fi
done
exit 0
