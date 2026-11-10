#!/usr/bin/env python3
"""Generate the Ed25519 key pair used to sign component catalogs.

The private key stays outside the repository (default:
~/.config/liora/update-key.ed25519) and the public key is written into the
source tree so builds trust catalogs signed by it.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import sys

try:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
except ImportError:
    sys.exit("the 'cryptography' package is required to generate update keys")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--private",
        default="~/.config/liora/update-key.ed25519",
        help="private key path (raw 32-byte seed)",
    )
    parser.add_argument(
        "--public",
        default="data/update-key.pub",
        help="public key path written into the repository (hex)",
    )
    parser.add_argument("--force", action="store_true", help="overwrite existing files")
    args = parser.parse_args()

    private_path = pathlib.Path(os.path.expanduser(args.private))
    public_path = pathlib.Path(os.path.expanduser(args.public))
    for path in (private_path, public_path):
        if path.exists() and not args.force:
            sys.exit(f"{path} already exists; pass --force to replace it")

    key = Ed25519PrivateKey.generate()
    seed = key.private_bytes(
        serialization.Encoding.Raw,
        serialization.PrivateFormat.Raw,
        serialization.NoEncryption(),
    )
    public = key.public_key().public_bytes(
        serialization.Encoding.Raw,
        serialization.PublicFormat.Raw,
    )

    private_path.parent.mkdir(parents=True, exist_ok=True)
    private_path.write_bytes(seed)
    private_path.chmod(0o600)
    public_path.parent.mkdir(parents=True, exist_ok=True)
    public_path.write_text(public.hex() + "\n", encoding="ascii")

    fingerprint = hashlib.sha256(public).hexdigest()[:16]
    print(f"private key: {private_path} (mode 0600, keep it secret and backed up)")
    print(f"public key:  {public_path}")
    print(f"fingerprint: {fingerprint}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
