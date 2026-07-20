> [!CAUTION]
> ## Experimental prototype - expect missing core features
>
> This is **not** a finished mod.
>
> Current builds may only test whether head tracking can drive the camera. Bug fixes and core features like decoupled look/aim, independent reticle behavior, correct shot direction, off-screen reticle support, movement handling, and comfort tuning may be missing at this early stage of development.

# Resident Evil 2 Head Tracking

Resident Evil 2 Head Tracking lets you move the in-game camera with your head while your mouse or controller still controls aim independently, adding immersion with no VR headset required.

<!-- Add a clip once one exists:
![Mod GIF](https://raw.githubusercontent.com/itsloopyo/resident-evil-2-headtracking/main/assets/readme-clip.gif)
-->

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse or controller
- **6DOF positional tracking** - lean and peek with head position

## Requirements

- [Resident Evil 2 Remake](https://store.steampowered.com/app/883710/Resident_Evil_2/) on Steam
- [OpenTrack](https://github.com/opentrack/opentrack) or a compatible head tracking source (smartphone, webcam, or dedicated hardware)
- Windows 10 or 11 (64-bit)

## Installation

1. Download the latest installer ZIP from the [Releases page](https://github.com/itsloopyo/resident-evil-2-headtracking/releases).
2. Extract the ZIP anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game. Head tracking is enabled automatically.

If the installer can't find your game:

- Set the `RE2_PATH` environment variable to your game folder, or
- Pass the path positionally: `install.cmd "D:\Games\RE2"`

### Manual Installation

If you prefer to place files by hand (or are using the Nexus ZIP, which contains only the plugin files):

1. Install [REFramework](https://github.com/praydog/REFramework-nightly/releases) for RE2 by extracting it to the game root.
2. Copy `RE2HeadTracking.dll` and `HeadTracking.ini` into `<game>/reframework/plugins/`.

## Setting Up OpenTrack

1. Download and install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Configure your tracker as the input.
3. Set the output to **UDP over network**.
4. Set Host to `127.0.0.1` and Port to `4242`.
5. Start tracking before launching the game.

### VR Headset Setup

A VR headset makes an excellent tracker, with smooth, low-latency rotation.

1. Connect your headset to the PC with Air Link or [Virtual Desktop](https://www.vrdesktop.net/).
2. Install [SteamVR](https://store.steampowered.com/app/250820/SteamVR/) and start it so the headset is tracked.
3. In OpenTrack, set the input to **SteamVR**.
4. Set the output to **UDP over network** (`127.0.0.1:4242`).
5. Start tracking before launching the game.

### Webcam Setup

No special hardware is needed. OpenTrack's built-in neuralnet tracker uses any webcam for face tracking.

1. In OpenTrack, set the input to **neuralnet tracker**.
2. Select your webcam in the tracker settings.
3. Set the output to **UDP over network** (`127.0.0.1:4242`).
4. Start tracking before launching the game.
5. Recenter in OpenTrack via its hotkey, and press **Home** in-game to recenter the mod as needed.

### Phone App Setup

This mod includes built-in smoothing for network jitter, so you can send directly from your phone on port 4242 without running OpenTrack on the PC.

1. Install an OpenTrack-compatible head tracking app.
2. Configure it to send to your PC's IP on port 4242 (run `ipconfig` to find your IP).
3. Set the protocol to OpenTrack/UDP.

If you want curve mapping or a visual preview, route through OpenTrack instead. Set OpenTrack's input to **UDP over network** on a different port (for example 5252), point your phone app at that port, and set OpenTrack's output to `127.0.0.1:4242`. Make sure your firewall allows incoming UDP on the input port.

## Controls

Two equivalent binding sets, use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Recenter            | `Home`      | `Ctrl+Shift+T`  |
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |
| Toggle aim reticle  | `Insert`    | `Ctrl+Shift+U`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

## Configuration

The mod creates a config file at `reframework/plugins/HeadTracking.ini` on first run. Edit it to customize behavior, then relaunch the game. Delete the file to reset to defaults.

```ini
[Network]
UDPPort=4242                    ; Must match OpenTrack output port (1024-65535)

[Sensitivity]
YawMultiplier=1.0               ; Horizontal rotation (0.1-5.0)
PitchMultiplier=1.0             ; Vertical rotation (0.1-5.0)
RollMultiplier=1.0              ; Head tilt (0.0-2.0)

[Position]
SensitivityX=2.0                ; Lateral sensitivity (0.1-10.0)
SensitivityY=2.0                ; Vertical sensitivity (0.1-10.0)
SensitivityZ=2.0                ; Depth sensitivity (0.1-10.0)
LimitX=0.30                     ; Max lateral offset in meters
LimitY=0.20                     ; Max vertical offset in meters
LimitZ=0.40                     ; Max forward offset in meters
LimitZBack=0.10                 ; Max backward offset (prevents camera clipping)
Smoothing=0.15                  ; Position smoothing (0.0-0.99)
InvertX=false                   ; Invert lateral axis
InvertY=false                   ; Invert vertical axis
InvertZ=false                   ; Invert depth axis
Enabled=true                    ; Enable or disable 6DOF position tracking

[Hotkeys]
; Virtual key codes (hex)
ToggleKey=0x23                  ; End - enable or disable tracking
RecenterKey=0x24                ; Home - recenter view
PositionToggleKey=0x21          ; Page Up - cycle tracking mode
ReticleToggleKey=0x2D           ; Insert - toggle reticle
YawModeKey=0x22                 ; Page Down - toggle world/local yaw

[Reticle]
Enabled=true                    ; Show the head tracking reticle overlay

[General]
AutoEnable=true                 ; Auto-enable tracking on game start
WorldSpaceYaw=true              ; true = horizon-locked yaw (default), false = camera-local
```

## Troubleshooting

**Mod not loading:**
- Ensure REFramework is installed (`dinput8.dll` in the game root).
- Check that the `reframework/` folder exists with `plugins/RE2HeadTracking.dll` inside.
- Try running the game as administrator once.

**No tracking response:**
- Verify OpenTrack is running and outputting data.
- Check that the UDP port matches (default 4242).
- Press **End** to enable tracking and **Home** to recenter.
- Check that your firewall isn't blocking UDP port 4242.

**Jittery or unstable tracking:**
- Increase position smoothing in `HeadTracking.ini`.
- If using a phone app over WiFi, some jitter is expected. The built-in interpolation helps.

**Wrong rotation axis or yaw feels wrong at extreme angles:**
- Adjust the sensitivity multipliers or use the Invert settings in the Position section.
- Toggle between world-locked and camera-local yaw with `Page Down`. World-locked (default) is horizon-stable; camera-local follows the camera's current up-axis.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. REFramework is only removed if the installer put it there. To force-remove REFramework anyway:

```powershell
uninstall.cmd /force
```

## Building from Source

Prerequisites:

- [CMake](https://cmake.org/) 3.20+
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the C++ desktop workload
- [pixi](https://pixi.sh) task runner

```bash
git clone --recurse-submodules https://github.com/itsloopyo/resident-evil-2-headtracking.git
cd resident-evil-2-headtracking
pixi run build        # build the mod (debug)
pixi run install      # build release and deploy to the game
pixi run package      # create the release ZIPs
```

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - use your iPhone or Android phone as the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- [Capcom](https://www.capcom.com/) - Resident Evil 2 Remake
- [praydog](https://github.com/praydog/REFramework) - REFramework
- [OpenTrack](https://github.com/opentrack/opentrack) - head tracking software
- [CameraUnlock](https://github.com/itsloopyo/cameraunlock-core) - shared head tracking library

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Capcom. Use at your own risk.
