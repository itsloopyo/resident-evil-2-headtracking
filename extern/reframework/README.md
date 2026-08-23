# reframework (vendored headers)

REFramework's plugin API headers, copied from upstream and compiled into
`RE2HeadTracking.dll`. They are the only third-party source we build against,
so the MIT notice beside them travels with every binary we publish.

## Snapshot

- Upstream: https://github.com/praydog/REFramework
- Revision: `ec6c81fd39831b328027ae00e102bc9c9c3f8aa5`, matching the REFramework
  loader vendored at `vendor/reframework/`
- Files: `include/reframework/API.h`, `include/reframework/API.hpp`
- Modified: no. Both files are byte-identical to upstream at that revision.
- Licence: MIT, reproduced verbatim at `LICENSE` here and in
  `THIRD-PARTY-NOTICES.md`

Keep this revision in step with `vendor/reframework/README.md`. If the vendored
loader is bumped, refresh these headers from the same REFramework revision and
re-verify them against upstream before updating the line above.
