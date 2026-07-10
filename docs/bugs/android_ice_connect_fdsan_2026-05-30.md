# Android automatch ICE_CONNECT fdsan SIGABRT (2026-05-30)

**Date:** 2026-05-30  
**Status:** Fix shipped (Android soak pending) — 2026-06-01 follow-up for bootstrap fast-fail + sync role_ready trickle  
**Area:** `mm_ice.c`, libjuice concurrency, automatch `MN_AM_ICE_CONNECT`, `netpeer.c` bootstrap

## Symptoms

- Android guest **SIGABRT** ~300 ms after automatch (DK vs Link, Sector Z stage kind), during **`MN_AM_ICE_CONNECT`** while waiting for host `role_ready` (trickle `GET` in flight, ICE not connected).
- Logcat / bugreport (device `debug.fdsan=fatal`):

  ```
  fdsan: failed to exchange ownership of file descriptor:
  fd 128 is owned by unique_fd 0x75c0dfb15c, was expected to be unowned
  ```

- Crash thread **not** SDL game thread, **not** matchmaking worker (`SSB64MmWorker`), **not** ICE gather pthread — consistent with libjuice **`JUICE_CONCURRENCY_MODE_THREAD`** per-agent **"juice agent"** thread running `poll()` on the ICE UDP socket while other native networking (HTTPS trickle, resolver) is active.
- `ssb64-debug.log` ends with `!!!! CRASH SIGABRT` and empty backtrace (abort off main thread). No tombstone for this pid in the bugreport zip.

## Root cause

On Android, bionic **fdsan** tracks FD ownership tags. With **per-agent libjuice threads**, the ICE UDP socket lifecycle overlaps automatch **curl** HTTPS trickle polling on a separate worker thread during the narrow `ICE_CONNECT` window. A native path attempted to exchange ownership on **fd 128** while fdsan still recorded it as owned by a C++ **`unique_fd`** — fatal abort under `debug.fdsan=fatal`.

Desktop Linux host in the same session (`guest.log` misnamed — actually **host** at `192.168.66.91`) exited cleanly; fdsan is not fatal there.

Prior fixes (`ice_signal_queue_thread_race`, `sIceMutex` queue scope, async gather) addressed different races; they did not change libjuice’s default **THREAD** concurrency on Android.

## Fix

- **`mm_ice.c`:** On **`__ANDROID__`**, default **`JUICE_CONCURRENCY_MODE_POLL`** (single shared **"juice poll"** thread for all agents). Game thread already calls **`mmIcePoll()`** each automatch tick to drain candidate/state queues.
- Override for A/B: `SSB64_MATCHMAKING_ICE_CONCURRENCY=thread|poll` (default on Android is `poll` if unset).
- Log at agent create: `SSB64 ICE: libjuice concurrency=poll|thread`.
- **`mm_ice_automatch.c` (2026-05-31 follow-up):** On Android, **`mmIceShutdown()`** when entering **`MN_AM_POLL`** after queue SDP is saved; **`mnVSNetAutomatchAMIceResumeForConnect()`** re-inits libjuice on **`MM_POLL_MATCHED`** with saved queue ufrag/pwd (`juice_set_local_ice_attributes`) and the discovered bind host:port. Removes the long poll-thread + curl overlap during queue wait (where fdsan still fired with poll mode alone). Disable: `SSB64_MATCHMAKING_ICE_SUSPEND_POLL=0`.
- **`scautomatch.c` (2026-06-02):** Call **`mnVSNetAutomatchAMIceSuspendForQueuePoll()`** in **`MN_AM_BIND`** immediately before **`mmMatchmakingEnqueueJoinQueueIce`** (after **`sIceQueueSdp`** is saved in bind tick), not only on **`MM_POLL_QUEUED`**. Fixes fatal fdsan on **`POST /v1/queue`** while gather/poll still owned the ICE UDP fd (~9 ms worker curl window).
- **`mm_ice_automatch.c` + `mm_matchmaking.c` (2026-06-01 testing patch):** On Android, **`SSB64_MATCHMAKING_ICE_HTTPS_SERIALIZE=1`** (default): matchmaking worker calls **`juice_pause_io()`** / **`juice_resume_io()`** around **`curl_easy_perform`** (shared libjuice poll thread skips agent UDP fds while paused). **`POST /v1/match/{ticket}/ice`** remains non-serialize. Disable: `SSB64_MATCHMAKING_ICE_HTTPS_SERIALIZE=0` (no pause gate; user bisect showed smooth connect but fdsan risk during ICE_CONNECT overlap).
- **`third_party/libjuice` (2026-06-01):** **`conn_poll_registry_pause` / `resume`** + public **`juice_pause_io` / `juice_resume_io`** for **`JUICE_CONCURRENCY_MODE_POLL`**. Replaces prior destroy/rehydrate serialize (removed).
- **`third_party/libjuice` (2026-06-02):** Poll-mode **`conn_destroy`**: unregister agent, **`conn_poll_detach_agent`** (defer socket close), **`release_registry`** (join poll thread when last agent), then close UDP/TCP. Fixes solo-queue **`MN_AM_POLL`** fdsan abort after **`mmIceShutdown()`**.
- **`mm_ice_automatch.c` (2026-06-02):** Android matchmaking HTTPS holds **`sIceNetSerializeMutex`** even during poll suspend (no juice pause when agent destroyed); serializes worker curl after ICE teardown.
- **`mm_ice_automatch.c` (2026-06-01 follow-up):** Reverted main-thread sync trickle — worker trickle **`GET`** stays on **`SSB64MmWorker`**.
- **Poll-mode recv blackout + VS startup lag (2026-06-02):** With default **`SSB64_MATCHMAKING_ICE_HTTPS_SERIALIZE=1`**, worker **`curl_easy_perform`** calls **`juice_pause_io()`** on the live agent. In **`JUICE_CONCURRENCY_MODE_POLL`**, paused agents are skipped by the shared poll thread — **UDP recv stops** while **`conn_poll_send` still uses `udp_sendto`**. Automatch **`GET /v1/match/{ticket}`** polls during **`CONNECTING`/`CONNECTED`** stacked pause depth and left Android unable to receive host **`input_bind_ack`** for ~120 execution-hold frames (`bind=0`, `recv=0`, `hr=0`). Bisect: **`SSB64_MATCHMAKING_ICE_CONCURRENCY=thread`** avoids pause (smooth connect); not fdsan-safe on strict Android.
  - **`mmIceShouldSerializeMatchmakingHttps()`** — **`FALSE`** only when agent is **`COMPLETED`** (VS staging recv); **`TRUE`** during **`CONNECTING`/`CONNECTED`** so worker/main HTTPS pauses libjuice poll (fdsan-safe). Do not disable serialize for connect phase.
  - **`mmIceEnsureIoResumed()`** — drain stacked pause depth after HTTPS / shutdown (VS staging ingress).
  - Main-thread **`IceConnectTick`** holds **`sIceNetSerializeMutex`**; nested HTTPS uses pause-only via **`sIceConnectTickInside`** (no mutex re-lock deadlock).
  - **`mmMatchmakingDropPendingPollMatchJobs()`** + worker skip while guest awaits **`role_ready`** — no worker **`GET /v1/match`** overlapping main-thread trickle.
  - Worker **`POST …/ice`** uses phase-aware serialize (pause during connect, not after completed).
- **Android queue wait (`MN_AM_POLL`) fdsan (2026-06-02):** Rapid worker **`GET /v1/match/{ticket}`** while ICE suspended (`still queued` storm + **`http=0`** immediate re-enqueue) could fdsan-abort (`fd owned by Parcel`) seconds into queue wait.
  - **`mnVSNetAutomatchAMQueuePollMayEnqueue()`** — wall-clock min interval (default **400 ms**, env `SSB64_MATCHMAKING_ANDROID_QUEUE_POLL_MIN_MS`) while **`sIcePollSuspended`**; exponential backoff on consecutive **`still queued`** (cap **4 s**).
  - **`mnVSNetAutomatchAMQueuePollNoteHttp0Cooldown()`** — **750 ms** cooldown after transient **`http=0`** (env `SSB64_MATCHMAKING_ANDROID_QUEUE_POLL_HTTP0_COOLDOWN_MS`); replaces **`sMnAMPollPeriodTics = interval - 1`** burst.
  - **`mmMatchmakingPollMatchOutstanding()`** — skip enqueue when worker poll in flight or job already queued.

Desktop and other platforms keep **`JUICE_CONCURRENCY_MODE_THREAD`** (unchanged LAN dual-NIC / staging behavior).

## Regression (2026-05-30 soak)

Trimmed session log showed **Linux host** `ICE state=completed` then **`ICE bootstrap failed`** / `bootstrap fast-fail (no peer activity)` while **Android guest** stayed on `waiting for peer controlling role_ready` (trickle polls `signals=0`, never `peer SDP applied`).

Contributing issues (fixed in same follow-up):

- Host `role-ready` was **async on the matchmaking worker** (could lag guest trickle polls).
- `peer_controlling_ready` parser used the first JSON key in the whole body (fragile vs nested `ice_connect.edges[]`).
- Guest had **no fallback** if polls never flipped ready before user cancel.

Follow-up client fixes: synchronous host `mmMatchmakingPostIceRoleReadySync`, scoped `peer_controlling_ready` parse, guest timeout apply deferred SDP (~8 s).

## Follow-up (2026-06-01 soak — `ssb64-debug.log`)

Two failure modes in one Android session (guest) vs Linux host:

| Attempt | ICE | Bootstrap | Crash |
|---------|-----|-----------|-------|
| 1 | `completed` LAN | `bootstrap fast-fail (no peer activity ~3s)` | No |
| 2 | stuck `waiting for peer controlling role_ready` (trickle `signals=0`) | never reached | **fdsan** `fd owned by Parcel` |

**Attempt 1 root cause:** Guest finished ICE and entered `syNetPeerRunBootstrap()` while the host was still in ICE_CONNECT. Bootstrap **peer-idle fast-fail** (~180 slices) aborted the client before the host sent `START` over the ICE channel.

**Attempt 2 root cause:** Worker-thread trickle `GET /v1/match/{ticket}` during `ICE_CONNECT` + `juice_pause_io` still collides with libjuice poll I/O on strict Android fdsan (`Parcel` ownership variant). Guest never saw `peer_controlling_ready` before crash (~tick 900, trickle poll #17).

### Fixes (2026-06-01)

| Layer | Change |
|-------|--------|
| **`netpeer.c`** | Disable bootstrap peer-idle fast-fail when `sSYNetPeerIceTransport` (keep ICE-failed abort). Call `mmIcePoll()` in `syNetPeerReceiveBootstrapPackets`. |
| **`mm_matchmaking.c`** | `mmMatchmakingPollMatchIceTrickleSync()` — caller-thread GET for trickle + `ice_connect`. `POST ice/role-ready` uses `mmHttpsRequestInternal(..., ice_serialize=FALSE)` so host role-ready does not pause juice. |
| **`mm_ice_automatch.c`** | While `sIceAwaitPeerControllingReady`, disable worker trickle polls; sync GET every ~16 connect ticks on main thread (under existing `sIceNetSerializeMutex` — no nested juice pause). |

### Follow-up (2026-06-02 — ICE_CONNECT game-thread freeze)

Guest log after match: `deferring peer SDP` → `ICE_CONNECT`, then **no** `ICE trickle poll #1` / `waiting for peer controlling role_ready`, **`WATCHDOG HANG`** on tid=5 with frame counter stalled.

**Cause:** `IceConnectTickEnter` held **`sIceNetSerializeMutex`** across the blocking sync trickle **`curl_easy_perform`**, stalling the worker’s enqueued **`POST …/ice`** and preventing the first role_ready poll from completing; watchdog pause was only set at the **end** of the automatch tick (never reached while blocked).

**Fix (mutex + pause):** `mnVSNetAutomatchAMIceRoleReadyTricklePollSync()` — unlock serialize mutex, **`mmIcePauseIo()`** directly ( **`ice_serialize=FALSE`** ), sync GET, re-lock; **`port_watchdog_set_connect_phase_pause(1)`** on **`MN_AM_ICE_CONNECT`** entry.

**Fix (2026-06-02 architectural):** Remove game-thread sync trickle during **`role_ready`** wait. **`IceConnectTick`** enqueues async worker **`GET /v1/match/{ticket}`** (trickle-only, **`juice_pause_io`** via serialize, **8 s** curl timeout); **`mmRunPoll`** allows trickle-only while full match polls stay blocked; game thread skips **`mmIcePoll`** while worker poll is outstanding; **`ConnectTickEnter`** skips serialize mutex during role_ready wait.

**Fix (2026-06-02 — connected→completed stall):** Worker trickle during **`ICE_CONNECT`** paused libjuice I/O while the game thread called **`mmIcePoll()`**, leaving ICE stuck at **`state=connected`**. Reverted to main-thread sync trickle for **`role_ready`**; block **all** worker **`GET /v1/match`** while connect phase active and agent not **`COMPLETED`**; zero **`ConnectTricklePollInterval`** during **`connected→completed`**; worker HTTPS always takes **`sIceNetSerializeMutex`** (removed nested pause when **`sIceConnectTickInside`**).

**Fix (2026-06-02 — `POST /v1/queue` fdsan after suspend):** Suspend logged but worker **`curl`** still overlapped poll-thread UDP teardown (`fd owned by unique_fd` on first queue POST). **`mnVSNetAutomatchAMIceSuspendForQueuePoll`** now holds **`sIceNetSerializeMutex`** across **`mmIcePoll`/`mmIceShutdown`**; **`mmIceShouldSerializeMatchmakingHttps()`** returns **`FALSE`** when no live agent; **`mmRunJoin`** uses **`ice_serialize=FALSE`** (ICE already torn down before enqueue).

## Verification

- Android automatch guest + Linux host: survive **≥15 s** after match through `ICE_CONNECT` → `ICE connected` → staging without fdsan abort.
- Guest log should show `peer SDP applied after controlling role_ready` or timeout apply, then both sides pass bootstrap (no `no peer activity` fast-fail).
- Log should show `libjuice concurrency=poll` on Android.
- Android guest (poll mode, default serialize): **`execution hold` ≪ 30**, **`input_bind_ack`** within ~100–200 ms, no multi-second **`bind=0`/`recv=0`** during VS staging.
- If reproducing old crash for bisect: `SSB64_MATCHMAKING_ICE_CONCURRENCY=thread` restores per-agent threads (expect fdsan risk on strict devices).
- **`SSB64_MATCHMAKING_ICE_HTTPS_SERIALIZE=0`** — no pause gate (smooth connect bisect; fdsan risk during ICE_CONNECT overlap).
- Optional dev-only: `adb shell setprop debug.fdsan warn` (does not replace soak on `fatal`).

## Related

- [ice_lan_dual_nic_and_thread_safety_2026-05-26.md](ice_lan_dual_nic_and_thread_safety_2026-05-26.md) — THREAD mode + `sIceMutex` scope on desktop
- [ice_signal_queue_thread_race_2026-05-26.md](ice_signal_queue_thread_race_2026-05-26.md) — trickle signal ring mutex
