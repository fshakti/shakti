# Security policy

## Reporting a vulnerability

Please report security issues privately through GitHub Security
Advisories for this project:

https://github.com/fshakti/shakti/security/advisories/new

Do not open a public issue, and please do not disclose details publicly
until a fix has been released and users have had a reasonable time to
upgrade.

Please include, where possible: the Shakti version (`shakti --version`),
platform, a minimal reproducer (a `.ie` script is ideal), and the
safe-mode context (`SHAKTI_SAFE=1`) if relevant.

## Supported versions

Only the latest tagged release is supported with security fixes.
Committed prebuilt binaries under `build/` may lag the source tree;
building from source is the supported way to obtain security fixes.

## Scope notes

- Safe mode (`SHAKTI_SAFE=1`) is a security boundary; escapes from it are
  in scope.
- The HTTP server is loopback-only by default; public binds require a
  token and TLS (or an explicit plaintext opt-in).
- IPC TCP: see the threat-model notes in `doc.md` before binding to a
  non-loopback address.
