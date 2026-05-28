# ICE automatch follow-ups (trickle, role, bind, poll) — 2026-05-26

## Symptoms

- libjuice stderr: `ICE role conflict (both controlling)`, `Remote candidate added after remote gathering done`
- Port log: `mmIceAddRemoteCandidate failed` ×4 at match, ICE stuck at `connected` without `completed`
- After ICE completed: `bind failed err=98` at staging (`syNetPeerStartVSSession` re-binds UDP 7778)
- Android: `GET match` curl HTTP 0 during `ICE_CONNECT` → immediate `matchmaking error`

## Root causes

1. **Both peers controlling** — libjuice defaults to controlling on `gather`; guest must be **controlled** before remote SDP. **2026-05-27:** default **controlled** at `IcePlayerReady`; host promoted at `BeginConnect` with fresh tiebreaker. libjuice **both-controlled** tiebreaker compared wrong STUN field (`ice_controlling` vs `ice_controlled`) — fixed in vendored [`third_party/libjuice/src/agent.c`](../../third_party/libjuice/src/agent.c).
2. **Early `end-of-candidates`** — Full `ice_sdp` from queue includes `a=end-of-candidates` after local gather completes, so peer SDP parse marks remote gathering finished before trickle `ice_signals` are applied.
3. **Premature `juice_set_remote_gathering_done`** — Called as soon as local gather + remote SDP applied, not after trickle queue drained.
4. **Double UDP bind** — Bootstrap skips `OpenSocket` when ICE transport is on; `syNetPeerStartVSSession` skips bind when `sSYNetPeerIceTransport` (see also [ice_vs_battle_transport_lifecycle](ice_vs_battle_transport_lifecycle_2026-05-27.md): do not shutdown ICE at VSBattle load).
5. **Poll errors during ICE** — Transient HTTP 0 / 5xx on match poll aborted automatch while ICE was still negotiating.

## Fixes

| Area | Change |
|------|--------|
| libjuice | `juice_set_ice_controlling()` / `agent_set_ice_controlling()` |
| `mm_ice.c` | Strip `a=end-of-candidates` from signaling SDP; `mmIceSetIceControlling()` |
| `mm_ice_automatch.c` | Host/controlling at `BeginConnect`; trickle quiet window before `SetRemoteGatheringDone`; `mnVSNetAutomatchAMIceShouldIgnorePollError()` |
| `netpeer.c` | Skip `syNetPeerOpenSocket()` in `syNetPeerStartVSSession` when ICE transport active |
| `scautomatch.c` | Ignore transient poll errors in `MN_AM_ICE_CONNECT` |

## Verification

- LAN automatch: both logs show `ICE: role=controlling/controlled`, `remote gathering done (trickle quiet)`, `ICE: completed`, bootstrap START, no `bind failed err=98`.
- stderr should not spam `Remote candidate added after remote gathering done` at match apply.
