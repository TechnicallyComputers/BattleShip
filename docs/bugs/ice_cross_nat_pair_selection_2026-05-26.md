# ICE cross-NAT pair selection + control-plane over ICE — 2026-05-26

## Symptoms

- LTE Android + LAN PC: both logs show `ICE: completed`, bootstrap START exchanged, then endless `execution hold` (`peer_ready=0`, `bind=0`, `recv=0`).
- PC log: `connected remote=192.168.66.3:64746` (peer LAN host) while Android used PC WAN `216.154.76.149:7778`.
- PC: `drop=` climbing with `recv=0`; automatch `returning to character select (connection timed out)`.
- Android: watchdog hang on staging scene 66.

## Root causes

1. **Asymmetric ICE nomination** — libjuice prefers `typ host` over srflx/relay. Peer phone on LTE still advertised Wi‑Fi host candidates in SDP; PC on same `/24` selected unreachable LAN pair.
2. **Control packets bypassed ICE** — `INPUT_BIND` and `BATTLE_EXEC_SYNC` used `syNetPeerOsSendTo` on the game UDP socket while INPUT used `mmIceSend`, so bind/exec sync never completed on pure ICE transport.

## Fixes

| Area | Change |
|------|--------|
| `netpeer.c` | `syNetPeerSendControlDatagram` → `syNetPeerSendGameDatagram` for bind, exec sync, session params, rollback, bootstrap warmup |
| `mm_ice.c` | Candidate policy (`allow_peer_host`, `signal_local_host`); strip/filter remote host SDP/trickle; `juice_get_selected_candidates` logging; post-connect path validation |
| `mm_ice_automatch.c` | Policy from `peer_lan` + `mmLanPeerSharesLocalLanSubnet`; suppress local host trickle when no LAN; TURN required when no LAN; connect tick returns -1 on validation failure |
| `scautomatch.c` | Abort with `ICE path validation failed` instead of generic timeout |
| `mm_matchmaking.c` / server | `peer_reports_lan` match JSON hint |
| Env | `SSB64_MATCHMAKING_ICE_SIGNAL_HOST`, `SSB64_MATCHMAKING_ICE_VERBOSE`, `SSB64_MATCHMAKING_ICE_REQUIRE_TURN` |

## Verification

- PC+Android LTE: `ICE: connected remote=` is srflx/relay/WAN (not peer `192.168.x.x`); `control send path=ICE`; `bind=1`, exec sync completes, VS starts.
- LAN vs LAN with both `peer_lan`: host candidates allowed; fast connect unchanged.

**Follow-up (2026-05-26):** Control-plane over ICE still required `mmIceSend` to map `JUICE_ERR_SUCCESS` → byte length for `INPUT_BIND` / exec-sync success checks — see [ice_juice_send_return_semantics_2026-05-26.md](ice_juice_send_return_semantics_2026-05-26.md).
