#!/bin/sh
# Downloads a CEF binary distribution and prints its path.
#
#   ./scripts/fetch-cef.sh [version] [destination]
#
# Defaults to the version the adapter was last verified against. Pass the path
# it prints to CMake as LIORA_CEF_ROOT.
set -eu

VERSION="${1:-156.0.26+g8ea0c73+chromium-156.0.8078.88}"
DEST="${2:-$(pwd)/build/cef}"
PLATFORM="macosarm64"
ROOT="$DEST/cef_binary_${VERSION}_${PLATFORM}_minimal"

if [ -d "$ROOT/include" ]; then
  echo "$ROOT"
  exit 0
fi

mkdir -p "$DEST"
echo "Fetching CEF $VERSION ($PLATFORM)..." >&2
curl -fsSL --max-time 120 -o "$DEST/index.json" https://cef-builds.spotifycdn.com/index.json

FILE=$(python3 - "$DEST/index.json" "$VERSION" "$PLATFORM" <<'PY'
import json, sys
index, version, platform = sys.argv[1], sys.argv[2], sys.argv[3]
data = json.load(open(index))
for v in data.get(platform, {}).get("versions", []):
    if v["cef_version"] == version:
        for f in v["files"]:
            if f["type"] == "minimal":
                print(f["name"], f["sha1"], f["size"])
                break
        break
PY
)

if [ -z "$FILE" ]; then
  echo "CEF $VERSION ($PLATFORM) is not available in the index." >&2
  exit 1
fi

NAME=$(echo "$FILE" | awk '{print $1}')
SHA1=$(echo "$FILE" | awk '{print $2}')
URL="https://cef-builds.spotifycdn.com/$NAME"

ARCHIVE="$DEST/$NAME"
for attempt in 1 2 3; do
  curl -fsSL -C - --max-time 1800 -o "$ARCHIVE" "$URL" && break
  echo "download interrupted, resuming (attempt $attempt)..." >&2
done

ACTUAL=$(shasum -a 1 "$ARCHIVE" | awk '{print $1}')
if [ "$ACTUAL" != "$SHA1" ]; then
  echo "checksum mismatch: expected $SHA1, got $ACTUAL" >&2
  exit 1
fi

echo "Extracting..." >&2
tar xjf "$ARCHIVE" -C "$DEST"
echo "$ROOT"
