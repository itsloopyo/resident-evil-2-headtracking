# reframework (vendored)

This directory contains a bundled copy of the upstream mod loader. It is the install-time
source of truth: install.cmd extracts directly from here and never reaches out to the network.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Upstream asset: `REFramework.zip`, stored here as `RE2.zip`. Only the filename
  differs, pinned so install.cmd, package-release.ps1, deploy.ps1 and
  launcher-manifest.json can refer to a fixed name. Contents are unmodified.
- Release tag: `nightly-01394-ec6c81fd39831b328027ae00e102bc9c9c3f8aa5`
- REFramework revision built: `ec6c81fd39831b328027ae00e102bc9c9c3f8aa5`
  (matches the archive's own `reframework_revision.txt`)
- Release-repo tag commit: `0436e043af6f81a5d3fef49ae27d35e63431e566` in
  praydog/REFramework-nightly. This is the release-hosting repo, not the
  REFramework source repo; the source revision is the line above.
- Upstream URL: https://github.com/praydog/REFramework-nightly/releases/download/nightly-01394-ec6c81fd39831b328027ae00e102bc9c9c3f8aa5/REFramework.zip
- SHA-256: `a3d24f04e41933a7a3a6e1d6402b7de18ca677245d9ca0dda9f6a5ca20e9b94e`
- Fetched at: 2026-08-03T11:49:31.3681449+01:00
- Source: github

Do not edit this directory by hand. Run ``pixi run package`` (or CI release) to refresh.
