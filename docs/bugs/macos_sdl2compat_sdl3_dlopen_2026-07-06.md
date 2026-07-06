# macOS bundle missing SDL3 for Homebrew sdl2-compat

**Status:** FIXED (release packaging)

**Symptoms:** The macOS v1.5 DMG installed successfully, but launching the app showed a fatal dialog like "Failed loading SDL3 library" and then crashed.

**Root cause:** The release workflow installs `sdl2` with Homebrew. Homebrew's current `sdl2` name can resolve to `sdl2-compat`, an SDL2 ABI wrapper that uses SDL3 behind the scenes. That SDL3 dependency is opened with `dlopen()` rather than declared as a Mach-O `LC_LOAD_DYLIB`, so `dylibbundler` only staged `libSDL2-2.0.0.dylib` into `Contents/Frameworks` and did not copy `libSDL3.dylib`. Developer machines with Homebrew SDL3 installed can mask this; clean user machines cannot.

**Fix:** After `dylibbundler`, `package-macos.sh` inspects the bundled `libSDL2-2.0.0.dylib` for sdl2-compat markers. If present, it copies Homebrew's real `libSDL3.0.dylib` into `Contents/Frameworks`, creates the unversioned `libSDL3.dylib` symlink that sdl2-compat probes, rewrites the SDL3 install name to `@loader_path/libSDL3.0.dylib`, and signs it with the rest of the bundle. `SSB64_FORCE_BUNDLE_SDL3=1` exists as a package-script test hook.

**Audit hook:** Dylib bundlers only see static Mach-O load commands. Any compatibility layer or plugin runtime that uses `dlopen()` needs an explicit bundle step and a clean-machine launch test.
