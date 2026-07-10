# macOS release DMG lost styled Finder layout

**Status:** FIXED (release packaging)

**Symptoms:** Public macOS release DMGs mounted as a plain Finder window: no banner/background image, no arranged drag-to-Applications layout, and small/default icons. v1.3 still had the polished `create-dmg` installer window; v1.4 and later did not.

**Root cause:** `025c8b868d7f2f4559181be758462801e7dc2141` changed release CI to prefer a plain `hdiutil` DMG whenever `CI` is set. The commit was defensive: GitHub's macOS runner image changed from `20260525.0091` to `20260608.0118`, and `create-dmg`'s headless Finder/AppleScript styling step hung for 30+ minutes during the v1.4 release jobs. The fallback fixed release liveness, but it also made every shipped CI artifact lose the old styled installer flow.

**Fix:** Release CI now explicitly sets `DMG_STYLED=1`, `DMG_REQUIRE_STYLED=1`, and `DMG_TIMEOUT=300`. `package-macos.sh` still supports the plain `hdiutil` path for local/emergency use, but when `DMG_REQUIRE_STYLED=1` is set it fails if `create-dmg` is unavailable, fails, or times out. That preserves the old installer UX and keeps the timeout guard that prevents a stuck Finder/AppleScript run from wedging the release job indefinitely.

**Audit hook:** If a macOS release DMG looks like a plain folder again, check the release job log for `Building styled DMG (create-dmg)`. A fallback to `Building plain DMG` should only happen when `DMG_REQUIRE_STYLED` is not set.
