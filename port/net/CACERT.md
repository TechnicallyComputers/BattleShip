# `cacert.pem` (HTTPS matchmaking)

Pinned Mozilla CA bundle for portable netplay builds (Windows zip, Linux AppImage, macOS bundle, MinGW).

- **Source:** https://curl.se/ca/cacert.pem (via [caextract](https://curl.se/docs/caextract.html))
- **Bundled as of:** 2026-05-14 (see header inside `cacert.pem` for SHA256)
- **Runtime:** copied to `ssl/cacert.pem` next to the game; `mm_matchmaking.c` loads it via `mmCurlConfigureSsl`

To refresh:

```bash
curl -fsSL https://curl.se/ca/cacert.pem -o port/net/cacert.pem
```

Verify the new `SHA256:` line in the file header, then commit.
