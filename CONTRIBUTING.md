# Contributing

Thanks for helping. Khan is early, so the most useful contributions right now
are bug reports, build fixes on your platform, and tests.

## Ground rules

- Read the architecture rules in [CLAUDE.md](CLAUDE.md). Pull requests that break the
  layer rules (for example OCCT types outside `kh_kernel_occt`) will be asked to change.
- Keep pull requests small and focused on one thing.
- Add or update tests with every change. CI must pass on Windows, macOS and Linux.
- Format code with `clang-format` using the repository's `.clang-format`.

## Developer Certificate of Origin

Every commit must be signed off to certify you have the right to submit it under the
project's license ([Developer Certificate of Origin](https://developercertificate.org/)):

```bash
git commit -s -m "Describe the change"
```

This adds a `Signed-off-by: Your Name <you@example.com>` line. No separate contributor
license agreement is required.

## AI-assisted contributions

AI tools are welcome. You are responsible for every line you submit: read it, test it,
and make sure it follows the architecture rules.
