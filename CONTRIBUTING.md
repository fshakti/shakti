# Contributing to Shakti

Shakti is licensed under the Apache License 2.0 (see `LICENSE`).

## Developer Certificate of Origin (DCO)

All contributions must be signed off under the
[Developer Certificate of Origin](https://developercertificate.org/).
Add a `Signed-off-by` line to every commit:

```bash
git commit -s -m "your message"
```

By signing off you certify that you wrote the contribution, or otherwise
have the right to submit it under Apache-2.0, and that the project may
redistribute it (including relicensing where the project as a whole is
relicensed with contributor consent recorded at the time).

Past contributions by David Laing (paid engagement) and Geo Carncross
(explicit permission) were made under Apache-2.0 with the project's
relicensing permission; see `AUTHORS.md`.

## Commit identity

Use your GitHub noreply address so commit metadata does not expose
personal email addresses or machine hostnames:

```bash
git config user.name "Your Name"
git config user.email "<id>+<username>@users.noreply.github.com"
```

## Authorship records

Authorship (human and AI-assisted) is recorded in commit messages and in
the maintainer's private authorship log, which is retained with the
project records. Do not remove co-author or attribution trailers.

## Building and testing

```bash
make build
make test
```

Optional components are gated by `SHAKTI_*=0` make variables; see
`doc.md`. Do not commit binaries under `build/` from feature branches
without refreshing every platform slot (note: distribution remediation
is tracked separately).

## Security

Do not open public issues for security vulnerabilities; see
`SECURITY.md`.
