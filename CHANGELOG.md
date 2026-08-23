# Changelog

## [Unreleased]

### Logging

- The log now names the config file it actually read (`Config loaded from <path>`), so an edit made to the wrong `HeadTracking.ini` is visible in the log instead of costing a support round trip.
- A one-shot `First tracker pose received: yaw/pitch/roll (local|remote connection)` line the first time a tracker packet reaches the mod. It is emitted ahead of every enable/gameplay gate, so its absence means the packets never arrived rather than that tracking was off or the camera hook had not engaged.
- Troubleshooting now names the log file to send (`<game>/re2_framework_log.txt`, truncated per launch) and the startup lines to look for in it.

### Legal and packaging

- `THIRD-PARTY-NOTICES.md` now records REFramework twice, once as the bundled
  loader and once as the plugin API headers compiled into
  `RE2HeadTracking.dll`, and reproduces the MIT text for both. It also
  reproduces cameraunlock-core's MIT notice, which is a different copyright
  holder from this mod's LICENSE and so is not covered by it.
- Corrected the REFramework revision recorded in the notices and in
  `vendor/reframework/README.md`. The commit previously named there belongs to
  the release-hosting repo, not to REFramework; the source revision the vendored
  loader was built from is `ec6c81fd39831b328027ae00e102bc9c9c3f8aa5`, matching
  the archive's own `reframework_revision.txt`.
- The Nexus ZIP now carries `LICENSE` and `THIRD-PARTY-NOTICES.md` at its root.
  It previously held only the DLL and INI, which met neither notice obligation.
  The packager fails rather than skipping a missing notice file, for both ZIPs.

### Changed
- The mod keeps no centre of its own. Every tracker app centres itself, so a
  centre in the mod was a second one in series with the tracker's, and the two
  drifted apart because each side moved at moments the other could not see.
  The mod now applies the pose it receives as absolute: centre it in your
  tracker app (OpenTrack's Center bind, the CENTER button in Headcam, SteamVR's
  reset). The `Home` key, the `Ctrl+Shift+T` chord and the
  `[Hotkeys] RecenterKey` ini entry are gone.
- Smoothing is now two user-configurable parameters in a new `[Smoothing]` section of `HeadTracking.ini`: `LocalSmoothing` (default 0.0) for a tracker running on this machine, and `RemoteSmoothing` (default 0.15) for a tracker on a remote network device. The value is picked per connection from the packet source address and is re-evaluated while the game runs, so switching between a local OpenTrack instance and a phone on WiFi takes effect without a restart.
- Removed the `[Position] Smoothing` key. Both new parameters cover rotation and position, so there is no separate position smoothing setting.
- Removed the hidden 0.15 baseline smoothing floor that silently overrode the configured value. Local users now get zero-latency tracking by default.

### Added
- Decoupled head tracking via OpenTrack (UDP 4242)
- 6DOF positional tracking with configurable sensitivity and limits
- Aim decoupling: head moves the camera, mouse controls aim independently
- Game state detection: tracking pauses during cutscenes, menus, loading, and pause screens
- Crosshair projection with smoothing to keep the reticle on the aim point
- Nav-cluster hotkeys: toggle (End), position toggle (PgUp), reticle toggle (Delete)
- Ctrl+Shift chord hotkeys (Y toggle, G position, H yaw mode, U reticle) for keyboards without a nav cluster
- INI configuration file with sensitivity, position limits, smoothing, and hotkey settings
- Automated installer with vendored REFramework
- Frame-rate independent smoothing and interpolation pipeline
