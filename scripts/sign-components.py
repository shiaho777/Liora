#!/usr/bin/env python3
"""Sign a component catalog with the Ed25519 update key.

The signature covers the canonical JSON body with the "signature" field
removed: UTF-8, keys sorted, compact separators. This matches nlohmann's
json::dump() used by Core.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import pathlib
import sys

try:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
except ImportError:
    sys.exit("the 'cryptography' package is required to sign catalogs")


def canonical_body(document: dict) -> bytes:
    body = {key: value for key, value in document.items() if key != "signature"}
    return json.dumps(
        body, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", help="catalog JSON to sign")
    parser.add_argument(
        "--key",
        default="~/.config/liora/update-key.ed25519",
        help="private key path (raw 32-byte seed)",
    )
    parser.add_argument(
        "--in-place", action="store_true", help="rewrite the catalog instead of printing it"
    )
    args = parser.parse_args()

    key_path = pathlib.Path(os.path.expanduser(args.key))
    seed = key_path.read_bytes()
    if len(seed) != 32:
        sys.exit(f"{key_path} is not a 32-byte Ed25519 seed")
    key = Ed25519PrivateKey.from_private_bytes(seed)

    catalog_path = pathlib.Path(os.path.expanduser(args.catalog))
    document = json.loads(catalog_path.read_text(encoding="utf-8"))
    signature = key.sign(canonical_body(document))
    document["signature"] = base64.b64encode(signature).decode("ascii")

    output = json.dumps(document, ensure_ascii=False, indent=2) + "\n"
    if args.in_place:
        catalog_path.write_text(output, encoding="utf-8")
        print(f"signed {catalog_path}")
    else:
        print(output, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
