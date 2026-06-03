# ICE shared-LAN spurious host nomination (Android multi-NIC)

**Date:** 2026-05-28  
**Status:** FIX SHIPPED (soak pending)

## Symptoms

Linux (controlling) + Android (controlled) on same RFC1918 LAN (`192.168.66.x`): match + ICE `completed`, then:

- Guest: `selected remote typ=host addr=25.209.227.57:58771` → `path validation failed (LAN match but remote=… not reachable on local_lan=192.168.66.91:57106)` → CSS `ICE path validation failed`
- Host: correct LAN pair selected, then `bootstrap client timed out waiting for MATCH_CONFIG` → CSS `ICE bootstrap failed`

Matcher had correct `peer_lan=192.168.66.6:58771`; libjuice on Linux nominated a non-LAN **host** candidate from Android (same ephemeral port, wrong interface/public IP).

## Root cause

On shared LAN, `mmIceSetCandidatePolicy(allow_peer_host=1)` accepted **all** remote `typ host` candidates. Android multi-NIC gather can produce extra host candidates (e.g. mobile/public interface) that outrank the queue `peer_lan` in ICE pair selection.

Local signaling had the symmetric gap: with `signal_local_host=1`, every gathered host was queued/trickled, not just the discovered `local_lan` bind.

## Fix

`mmIceSetCandidatePolicy` now takes optional `peer_lan_hostport` / `local_lan_hostport` filters:

| Path | Behavior |
|------|----------|
| Remote SDP + trickle | Drop peer `typ host` not RFC1918 or not on `peer_lan` segment (`mmLanIpv4HostportsOnSameSharedLanSegment` + exact IP); inject `peer_lan` only when filtered SDP has no allowed remote host |
| Local queue SDP + trickle | Drop local `typ host` not on `local_lan` interface subnet (`mmLanPeerSharesLocalLanSubnet` + exact IP) |
| Verbose trickle log | Log filtered candidate line (not hardcoded "host") |

**Follow-up (2026-05-28):** First patch used `candidate:0 …` for `juice_add_remote_candidate` (libjuice requires `a=candidate:`) and filtered remote trickle with local interface-subnet checks (over-stripped multi-NIC peer hosts). Fixed inject prefix, split remote vs local filter helpers, and conditional inject via `mmIceSdpHasAllowedRemotePeerHost`.

Files: [`port/net/matchmaking/mm_ice.c`](port/net/matchmaking/mm_ice.c), [`port/net/matchmaking/mm_ice_automatch.c`](port/net/matchmaking/mm_ice_automatch.c), [`port/net/matchmaking/mm_lan_detect.c`](port/net/matchmaking/mm_lan_detect.c)

## Soak pass criteria

- Linux + Android on shared LAN: both `selected remote typ=host` on `192.168.66.x` ports; no path validation abort; bootstrap completes
- No `Failed to parse remote SDP candidate` / `peer_lan remote candidate add failed`
- Log may show `filtered … remote host candidate(s) not on peer_lan=`, `ensured peer_lan remote candidate`, and/or `stripped non-local-LAN host candidate(s) from signaling SDP`
