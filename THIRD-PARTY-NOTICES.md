# Third-Party Notices

RE2HeadTracking bundles, statically links, or credits the third-party components
listed below. Each remains the property of its authors and is used under its own
licence. Where a licence requires the copyright notice, the conditions and the
disclaimer to accompany a binary distribution, the full text is reproduced here
verbatim, and this file ships at the root of every release ZIP we publish,
Nexus included.

Nothing in this repository is derived from, or redistributes any part of,
Resident Evil 2.

| Component | Version | Licence | How it ships |
|-----------|---------|---------|--------------|
| REFramework (loader) | `nightly-01394` / REFramework `ec6c81fd39831b328027ae00e102bc9c9c3f8aa5` | MIT | Bundled verbatim in the installer ZIP |
| REFramework plugin API headers | REFramework `ec6c81fd39831b328027ae00e102bc9c9c3f8aa5` | MIT | Compiled into `RE2HeadTracking.dll` |
| cameraunlock-core | `3465659888b2270addac9de0b2a728f59a00360c` | MIT | Compiled into `RE2HeadTracking.dll` |
| OpenTrack | n/a | ISC | Not bundled; UDP protocol interoperability only |

---

## REFramework

REFramework by praydog is used two ways, and both are covered by the single MIT
notice reproduced below.

**As the mod loader**, vendored at `vendor/reframework/`, shipped in the
installer ZIP and used as the install-time source. It is the upstream release
asset byte for byte, with no file added, removed or altered; the upstream
licence file ships beside it at `vendor/reframework/LICENSE`.

- Upstream project: https://github.com/praydog/REFramework
- Nightly release: https://github.com/praydog/REFramework-nightly/releases/tag/nightly-01394-ec6c81fd39831b328027ae00e102bc9c9c3f8aa5
- REFramework revision built: `ec6c81fd39831b328027ae00e102bc9c9c3f8aa5`
  (as recorded in the archive's own `reframework_revision.txt`)
- Upstream asset: `REFramework.zip`, stored here as `vendor/reframework/RE2.zip`.
  Only the filename differs, so that the installer and the launcher manifest can
  refer to a fixed name. Contents are unmodified.
- SHA-256 of the archive: `a3d24f04e41933a7a3a6e1d6402b7de18ca677245d9ca0dda9f6a5ca20e9b94e`

**As the plugin API**, vendored at `extern/reframework/` (`API.h`, `API.hpp`).
These headers are upstream's, unmodified, and they are compiled into
`RE2HeadTracking.dll`, so the notice below accompanies that binary in every ZIP
we publish. See `extern/reframework/README.md` for the provenance record.

```
MIT License

Copyright (c) 2019 praydog

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## cameraunlock-core

Git submodule at `cameraunlock-core/`, compiled into `RE2HeadTracking.dll`. It
is our own shared library, but it carries a different copyright holder from this
mod's LICENSE, so its notice is reproduced here in full rather than treated as
covered by ours.

- Upstream: https://github.com/itsloopyo/cameraunlock-core
- Pinned commit: `3465659888b2270addac9de0b2a728f59a00360c`

```
MIT License

Copyright (c) 2026 CameraUnlock

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## OpenTrack

Not bundled and not linked. This mod implements the OpenTrack UDP pose datagram
layout so that OpenTrack (https://github.com/opentrack/opentrack, ISC licence)
and compatible trackers can drive it. No OpenTrack code, headers or binaries
are copied, linked or redistributed, so its licence triggers no notice
obligation here. It is credited because the wire format is its work.

---

## Resident Evil 2

Resident Evil 2 and all related names, logos, characters and marks are
trademarks of Capcom Co., Ltd. They are used here only to identify the game this
mod applies to, which is nominative use and not a claim of any right in them.
This project is an unofficial, fan-made modification. It is not affiliated with,
endorsed by, or sponsored by Capcom or any other rights holder, and it requires
a legitimately purchased copy of the game.

No game code, no game assets, no game data files and no proprietary DLLs are
contained in this repository or in anything we publish. The mod holds no
addresses, struct offsets or byte signatures taken from the game: every hook and
every value it reads is resolved at runtime by name through REFramework's
managed type system. The engine and game type names that appear in the source
(`app.ropeway.camera.CameraSystem`, `via.Camera` and similar) are the identifiers
the running game reports through that reflection API. They are facts about an
interface, recorded so this mod can interoperate with it.

If a rights holder considers anything here to overstep, contact us through the
repository's issue tracker and we will address it.
