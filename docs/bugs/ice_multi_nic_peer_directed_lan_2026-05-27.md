# ICE automatch multi-NIC peer-directed LAN pick (2026-05-27)

**Status:** FIX SHIPPED

## Symptoms

- Dual-NIC host (e.g. `192.168.66.91` + `192.168.66.12`) queued `.91` while Android `.6` stayed `still queued`, or Linux matched **same host IP** with a different ephemeral port (stale queue slot).
- ICE could **complete** (`.6` ↔ `.91`) then **bootstrap failed** with `bind=0.0.0.0:0` / `invalid bind or peer IPv4 host:port`.
- UI showed **“ICE path validation failed”** for generic `ICE: state=failed`.

## Root cause

1. **Pre-match LAN pick** used global RFC1918 scoring (`mmLanPickBestHostportFromCandidates`) with no knowledge of the matched peer — wrong NIC advertised vs route to Android.
2. **NetPeer bootstrap** passed `MN_AM_BIND_DEFAULT` (`0.0.0.0:0`) into `syNetPeerConfigureUdpForAutomatch`; parser rejects port `0`.
3. **Matcher** allowed same `player_id` to pair (dev bypass) and kept stale queue rows when re-searching on one PC.

## Fix

- **`mmLanRouteSourceIpv4ForPeer`** + **`mmLanPickHostportForPeer`**: score candidates by kernel route source (+200), shared subnet with peer (+150), then RFC1918 / `LAN_ENDPOINT` / `BIND` host bias.
- **`mmIceGetLocalHostHostportForPeer`**, **`mmIceGetBootstrapBindHostport`**: peer-directed queue refresh at `BeginConnect`; bootstrap uses ICE selected local or peer-directed host:port.
- **`mnVSNetAutomatchAMIceConnectFailureReason`**: distinct abort strings for path validation vs ICE failed.
- **Matchmaking server**: `cancel_other_queued_for_player` on enqueue; re-enable same-`player_id` pairing block.

## Soak

Linux host (`.91`/`.12`) + Android `.6`, default `0.0.0.0:0` bind, no `LAN_ENDPOINT`. Expect `peer-directed local bind` log, bootstrap `bind=192.168.66.x:ephemeral`, distinct peers in match JSON.
