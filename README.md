# Resident Evil 2 Head Tracking

![Resident Evil 2 running with this mod](https://raw.githubusercontent.com/itsloopyo/resident-evil-2-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Resident Evil 2 that moves the camera with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

> [!CAUTION]
> ## Experimental prototype - expect missing core features
>
> This is **not** a finished mod.
>
> Current builds may only test whether head tracking can drive the camera. Bug fixes and core features like decoupled look/aim, independent reticle behavior, correct shot direction, off-screen reticle support, movement handling, and comfort tuning may be missing at this early stage of development.

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

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets, use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

## Configuration

The mod creates a config file at `reframework/plugins/HeadTracking.ini` on first run. Edit it to customize behavior, then relaunch the game. Delete the file to reset to defaults.

A comment has to sit on its own line, above the key. The parser hands the whole
text after `=` to the value reader. For a `true`/`false` or text setting that
text is compared as a whole, so a trailing `; note` makes the comparison fail
and the setting silently keeps its default. Numeric settings survive a trailing
comment because the number is read off the front of the text, which is why some
lines below still carry one. Putting every comment on its own line always works.

```ini
[Network]
UDPPort=4242                    ; Must match OpenTrack output port (1024-65535)

[Sensitivity]
YawMultiplier=1.0               ; Horizontal rotation (0.1-5.0)
PitchMultiplier=1.0             ; Vertical rotation (0.1-5.0)
RollMultiplier=1.0              ; Head tilt (0.0-2.0)

[Smoothing]
LocalSmoothing=0.0              ; Tracker on this machine, loopback (0.0-1.0)
RemoteSmoothing=0.15            ; Tracker is a remote network device (0.0-1.0)

[Position]
SensitivityX=2.0                ; Lateral sensitivity (0.1-10.0)
SensitivityY=2.0                ; Vertical sensitivity (0.1-10.0)
SensitivityZ=2.0                ; Depth sensitivity (0.1-10.0)
LimitX=0.30                     ; Max lateral offset in meters
LimitY=0.20                     ; Max vertical offset in meters
LimitZ=0.40                     ; Max forward offset in meters
LimitZBack=0.10                 ; Max backward offset (prevents camera clipping)
; Invert lateral axis
InvertX=false
; Invert vertical axis
InvertY=false
; Invert depth axis
InvertZ=false
; Enable or disable 6DOF position tracking
Enabled=true

[Hotkeys]
; Virtual key codes (hex)
ToggleKey=0x23                  ; End - enable or disable tracking
PositionToggleKey=0x21          ; Page Up - cycle tracking mode
YawModeKey=0x22                 ; Page Down - toggle world/local yaw

[General]
; Auto-enable tracking on game start
AutoEnable=true
; true = horizon-locked yaw (default), false = camera-local
WorldSpaceYaw=true
```

## Troubleshooting

**Sending a log:**
- REFramework writes one log per game launch at `<game>/re2_framework_log.txt`. That generic name is used for every RE Engine title, so it is the right file for this game too. If the game folder is not writable it lands in `%APPDATA%\REFramework\<exe name>\` instead.
- The file is truncated on every launch, so it only ever holds the current session. Attach it as-is to a bug report.
- This mod's lines are prefixed `[RE2HT]`. The startup sequence to look for is: `Plugin loaded`, `Config loaded from ...`, `UDP receiver started on port ...`, `Initialization complete`, then `First tracker pose received: ...` once the tracker sends anything.

**Mod not loading:**
- Ensure REFramework is installed (`dinput8.dll` in the game root).
- Check that the `reframework/` folder exists with `plugins/RE2HeadTracking.dll` inside.
- Try running the game as administrator once.

**No tracking response:**
- Verify OpenTrack is running and outputting data.
- Check that the UDP port matches (default 4242).
- Press **End** to enable tracking.
- If the view sits off to one side, centre it in your tracker app. The mod has no centre of its own.
- Check that your firewall isn't blocking UDP port 4242.

**Jittery or unstable tracking:**
- Increase `RemoteSmoothing` (phone or other network tracker) or `LocalSmoothing` (tracker on this PC) in the `[Smoothing]` section of `HeadTracking.ini`.
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
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- [Capcom](https://www.capcom.com/) - Resident Evil 2 Remake
- [praydog](https://github.com/praydog/REFramework) - REFramework
- [OpenTrack](https://github.com/opentrack/opentrack) - head tracking software
- [CameraUnlock](https://github.com/itsloopyo/cameraunlock-core) - shared head tracking library

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Capcom. Use at your own risk.
