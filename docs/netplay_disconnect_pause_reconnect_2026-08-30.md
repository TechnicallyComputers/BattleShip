# Disconnect pause + reconnection: architecture and the 2026-08-30 gap fixes

## What already existed

`port/net/sys/netreconnect.c` is a complete mid-match reconnect state machine, live in
current builds (both 2026-08-30 soaks log `Reconnect: transport armed`):

- **Phases**: IDLE → HOLD_PENDING → HOLD_ACTIVE → ICE_RECYCLING → READY_PENDING → back to
  IDLE (or FORFEIT_PENDING).
- **Pause**: entering hold opens the real battle pause interface
  (`ifCommonBattlePauseSetupFromPlayer`) attributed to the disconnecting slot, and
  `syNetReconnectBlocksUnpause()` keeps it held. Sim tick freezes with the pause, so the
  peers stop within the prediction window of each other.
- **Recovery**: full ICE recycle through the signaling server (`mmIceReconnectBegin/Tick`)
  — which also covers a peer coming back on a **different network/address**, since the
  recycled session renegotiates candidates from scratch.
- **Handshake**: HOLD (retransmitted every 30 frames, both directions) → READY when ICE
  reconnects → ACK → hold cleared, game unpauses.
- **Give-up**: host-side forfeit after `SSB64_NETPLAY_RECONNECT_GRACE_FRAMES` (default
  1800 ≈ 30 s), posted to matchmaking as `forfeit_timeout`.
- **Env**: `SSB64_NETPLAY_RECONNECT` (default on), `_OVERLAY`, `_GRACE_FRAMES`,
  `_DETECT_FRAMES`, `_ARM_BOOT_TICKS`.

## The gap: nothing detected a peer going quiet

Mid-match entry had exactly two triggers:

1. `mmIceIsFailed()` — the ICE layer itself declaring failure. A peer whose app is
   minimized or whose network drops mid-transit just goes **silent**; on LAN host-candidate
   pairs this never trips, or trips minutes late.
2. The `peer_connect_status` disconnect latch — carried **in the peer's own packets**, so
   it only works while packets are still arriving. It reports controller disconnects, not
   link loss.

Android network *switching* was wired (`NetworkMonitor` JNI → `NotifyNetworkChange`), but
app minimize had no hook anywhere, and a silent peer produced the exact symptom reported:
the session stalls at the prediction cap with no pause, no reconnect attempt, and
watchdog-adjacent machinery churning.

## What was added (2026-08-30)

**1. Peer-silence detector** (`netpeer.c`, per frame while executing):
every valid inbound session packet stamps `sSYNetPeerLastInboundUnixMs` (all 11 ingress
accounting sites). If the session is mid-match eligible, no hold is active, and silence
exceeds `SSB64_NETPLAY_RECONNECT_SILENCE_MS` (default 1000, 0 disables), it feeds
`syNetReconnectNotifyTransportBad()` — the same debounced entry ICE failure uses, needing
30 consecutive frames. **Time-to-pause ≈ 1.5 s of silence.** HOLD retransmits refresh the
clock during a hold, so an active hold never re-triggers itself. This is the *guaranteed*
path for every silent-peer case: minimize, network drop, crash, cable pull.

**2. App-lifecycle hooks** (`android_network.cpp` + `netreconnect.c`):
an `SDL_AddEventWatch` armed on every JNI entry path catches
`SDL_APP_WILLENTERBACKGROUND` / `SDL_APP_DIDENTERFOREGROUND`. The watch runs on the
Android activity thread, so it does only thread-safe work: quiets the hang watchdog
(`port_watchdog_set_connect_phase_pause`, so a blocked loop stops producing spurious
hang backtraces) and flags the transition. The main-thread drain
(`port_android_network_drain`, already in the frame loop) then calls
`syNetReconnectNotifyAppBackground()` — which begins the hold **immediately**, skipping
the debounce, so the graceful HOLD packet reaches the peer before the loop blocks — or
`syNetReconnectNotifyAppForeground()` (resets the debounce; the hold machine resumes its
ICE recycle on its own). SDL's default `BLOCK_ON_PAUSE` means the background drain may
never run; that is fine — the fast path is opportunistic, the silence detector is the
guarantee, and a wake-up drain applies only the latest state.

## End-to-end flows

- **Minimize**: guest backgrounds → (fast path) HOLD sent, host pauses same frame; or
  (fallback) host pauses after ~1.5 s silence. Host counts down 30 s grace. Guest returns
  → ICE recycles → READY/ACK → unpause. Past grace → host forfeits the absent slot.
- **Network switch**: `NetworkMonitor` fires (existing) or silence trips (new) → hold →
  ICE recycle renegotiates on the new network → resume with the new address.
- **Hard drop / crash**: silence → hold → grace expires → forfeit, result posted.

## Verification (needs a two-peer session; not testable solo)

1. Mid-match, minimize the Android app: Linux log should show
   `Reconnect: hold pending/active` within ~1.5 s and the battle pause opening.
2. Restore the app within 30 s: `ICE ready, sent RECONNECT_READY` → `hold cleared`,
   both resume, no desync (`sim_state_tick` hashes agree after resume).
3. Airplane-mode toggle on Android mid-match: same flow via network-change/silence.
4. Stay minimized past 30 s: host logs `forfeit applied`, match ends cleanly.
5. Android logcat should NOT show hang-watchdog backtraces during background.

---

## First live firing (soak 2026-08-30): detector right, overlay fatal

The silence path worked end-to-end for the first time: Android entered
`hold pending/active tick=4088` locally, Linux followed via its own detection at 4093
(after correctly ignoring the peer HOLD on the strict tick check), both began the ICE
recycle — **and both then crashed identically** (`SIGSEGV fault_addr=0x318`, Linux
symbolized as `ImGui::PushFont+0x100`).

The overlay drew via `GameOverlay::TextDraw` directly from `PortPushFrame` — outside the
ImGui frame, where PushFont/SetCursorPos dereference a null current window. The code had
simply never executed before this feature made mid-match reconnect reachable.
`first_run.cpp` documents the same crash class for early notifications.

Fixed: the overlay is now a `GuiWindow` registered with the LUS Gui (drawn inside the GUI
frame), rendering with the default font — no `PushFont` on the path at all. Same visual:
centered shadowed "Reconnecting... (Ns)" countdown while the hold is active.

Also observed for later: the strict `sim_tick == local` HOLD-ingress check made Linux
discard Android's HOLD (peer=4088 vs local=4093) and rely on its own detection. It
converged anyway; if future soaks show holds failing to pair, that check is the place to
loosen (accept within the prediction window).

---

## Second live firing (minimize test): pause pipeline works; recycle had a LAN bug

The whole front half of the feature worked: Android minimized -> both peers entered hold
(Android via lifecycle/local detection at 1802, Linux via silence at 1804), the overlay
drew with **no crash**, both began the ICE recycle, and Android's restore fired
`app foreground — hold active, resuming recycle`.

Two failures behind that:

1. **Recycle candidate policy stripped LAN candidates.** `mmIceRcInitGather` called
   `mmIceSetCandidatePolicy(FALSE, FALSE, NULL, NULL)` -- refuse the peer's host
   candidates and omit ours from signaling. On a LAN match that removes exactly the
   candidates that carry the connection; Linux logged "omitted local host candidate(s)
   from signaling SDP (no local LAN)" and the recycle failed to reflexive/relay-only.
   Fixed: `(TRUE, TRUE, NULL, NULL)` -- host candidates on, but **unfiltered** (no pinned
   LAN host:port), because the peer may legitimately be on a new network; connectivity
   checks pick the working pair.
2. **The Android app exited one frame after foreground** (`WindowIsRunning=0` right after
   the resume log). Whether the user closed it or Android destroyed the Activity in the
   background, the process was gone -- no recycle can survive that; the 30 s forfeit is
   the designed outcome. A cold-relaunch rejoin ("resume my held match on startup") would
   be a separate feature.

Also hardened: frame-commit cross-peer validation is now skipped while a reconnect hold
is active. Android's frozen ring held an invalid blob for tick 1800, which "diverged"
against the healthy peer and armed a recovery with nothing to recover from. The
post-resume window revalidates normally.

