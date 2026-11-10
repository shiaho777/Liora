# TweetNaCl

Minimal Ed25519 implementation used to verify signed component catalogs.

- Upstream: https://tweetnacl.cr.yp.to/
- Version: 20140427
- License: public domain

Only `crypto_sign_open` (detached signatures are verified by prefixing the
64-byte signature to the message) and `crypto_sign` (used by the tests) are
called. `randombytes` is provided by `core/src/crypto.cpp` so the unused
key-generation entry points still link.
