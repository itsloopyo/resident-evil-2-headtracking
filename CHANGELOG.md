# Changelog

## [Unreleased]

### Added
- Decoupled head tracking via OpenTrack (UDP 4242)
- 6DOF positional tracking with configurable sensitivity and limits
- Aim decoupling: head moves the camera, mouse controls aim independently
- Game state detection: tracking pauses during cutscenes, menus, loading, and pause screens
- Auto-recenter on first tracking connection
- Crosshair projection with smoothing to keep the reticle on the aim point
- Nav-cluster hotkeys: recenter (Home), toggle (End), position toggle (PgUp), reticle toggle (Delete)
- Ctrl+Shift chord hotkeys (T recenter, Y toggle, G position, H yaw mode, U reticle) for keyboards without a nav cluster
- INI configuration file with sensitivity, position limits, smoothing, and hotkey settings
- Automated installer with vendored REFramework
- Frame-rate independent smoothing and interpolation pipeline
