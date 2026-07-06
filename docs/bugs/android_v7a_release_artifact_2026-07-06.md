# Android armeabi-v7a missing from release assets

**Status:** FIXED (release packaging)

**Symptoms:** The `v1.5` GitHub Release produced an Android APK artifact, but there was no separate 32-bit ARM APK for `armeabi-v7a` devices.

**Root cause:** `android/app/build.gradle.kts` defaults `ssb64.abis` to `arm64-v8a`, and the main release workflow ran `./gradlew assembleRelease` without overriding it. The `armeabi-v7a` build existed only in `.github/workflows/android-v7a.yml`, which built a debug-signed validation APK and was not part of the tag-release artifact graph or the final release asset list. The first release-matrix run also exposed that the outer repo was still pinned to a decomp `port-patches` commit before the documented ILP32 `malloc(size_t)` / audio-layout fix.

**Fix:** The standalone validation workflow was removed, and the release workflow now builds Android as an ABI matrix. The `arm64-v8a` build keeps the historical `BattleShip-android.apk` filename; the `armeabi-v7a` build uploads `BattleShip-android-armeabi-v7a.apk`. The decomp submodule was advanced to a `port-patches` commit containing the ILP32 compatibility fix. The publish job includes both APKs, so release tags exercise and ship both Android ABIs.

**Audit hook:** Any supported ABI that matters to users should be in `.github/workflows/release.yml`, not only in a side validation workflow. Otherwise it can pass CI without ever becoming a release asset.
