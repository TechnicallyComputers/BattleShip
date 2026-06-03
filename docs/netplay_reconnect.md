# Mid-game ICE reconnect

Symmetric sim-tick hold during brief connectivity loss in ICE automatch 2p VS. Both peers freeze at tick **T**, show the in-game pause UI plus a **Reconnecting...** overlay, recycle ICE via HTTPS re-signaling on the preserved match ticket, then resume with synced Start. If grace expires, the sim host forfeits the disconnected player into normal VS results and posts an authoritative match result to the server.

## Phases (implementation)

| Phase | Behavior |
|-------|----------|
| 1 | Hold FSM (`netreconnect.c`), wire packets 28–31, hold reason `'H'`, strict-R grace suppression, pause UI, GameOverlay |
| 2 | ICE agent recycle (`mm_ice_reconnect.c`), `POST /v1/match/{ticket}/ice/restart`, trickle over preserved ticket. TURN fetch + libjuice init/gather run on **`SSB64MmWorker`**; sim thread polls `mmIcePoll` and drives trickle/signaling. |
| 3 | Host FORFEIT after 30 s grace → deterministic stock/results end (`scvsbattle_reconnect.c`) |
| 3b | `POST /v1/match/{id}/result` + `GET outcome` for Glicko / disconnects |
| 4 | Android `ConnectivityManager` JNI + optional Linux netlink env gate |

## Environment variables

| Variable | Default | Meaning |
|----------|---------|---------|
| `SSB64_NETPLAY_RECONNECT` | `1` | Enable mid-game reconnect |
| `SSB64_NETPLAY_RECONNECT_GRACE_FRAMES` | `1800` | Forfeit timeout (~30 s @ 60 Hz) |
| `SSB64_NETPLAY_RECONNECT_DETECT_FRAMES` | `30` | Consecutive bad transport frames before HOLD |
| `SSB64_NETPLAY_RECONNECT_OVERLAY` | `1` | Show GameOverlay reconnect text |
| `SSB64_NETPLAY_RECONNECT_NETLINK` | `0` | Linux: drain hook for proactive network-change flag |
| `SSB64_NETPLAY_RECONNECT_ARM_BOOT_TICKS` | `60` | Sim ticks after execution-begin before transport arm |

## Mid-match transport arm

Proactive network monitoring (`NetworkMonitor` on Android) and reconnect transport-bad detection arm only after:

- VS session active, bootstrap finished, execution gate + ingress symmetry + session params negotiated
- `scene_curr == VSBattle`, `game_status == Go`
- `sim_tick >= execution_begin_mark + ARM_BOOT_TICKS`
- All remote human slots have a ring cell at `wire_base = sim_tick + D` (latched once per session)

Hold / ICE recycle / disconnect export remain gated on the same `syNetReconnectMidMatchEligible()` path.

On Android, `port_android_network_try_arm_monitoring()` only sets a pending flag; `NetworkMonitor.install()` runs on **SDL_main** inside `port_android_network_drain()` (same JNI rule as deferred display-list rendering). Uses `getApplicationContext()` and clears JNI exceptions before returning.

## Test matrix (soak)

| Case | Expect |
|------|--------|
| Remote WiFi off &lt; 30 s, WiFi on | Hold → overlay → ICE recycle → Start unpause → continue at T |
| Android LTE → WiFi mid-match | Same; TURN regather if needed |
| PC WiFi → Ethernet | Fast host-candidate renomination |
| Grace exceeded | Connected player: results win; forfeiter: loss |
| Both sides flap | Single epoch bump; one peer initiates HOLD |
| Pause during hold | No sim advance |
| Forfeit + guest offline 5 min | Guest GET outcome → loss; Glicko once; duplicate POST 409 |
| Normal match end | Host POST `normal`; ratings update |

## Key modules

- `port/net/sys/netreconnect.c` — hold FSM, grace, forfeit orchestration
- `port/net/matchmaking/mm_ice_reconnect.c` — mid-game ICE recycle
- `port/net/sc/sccommon/scvsbattle_reconnect.c` — deterministic forfeit → results
- `BattleShip-Server` — `ice/restart`, extended `result`, `GET outcome`

See also [`netplay_environment_variables.md`](netplay_environment_variables.md) and [`docs/bugs/netplay_midgame_reconnect_2026-06-01.md`](bugs/netplay_midgame_reconnect_2026-06-01.md).
