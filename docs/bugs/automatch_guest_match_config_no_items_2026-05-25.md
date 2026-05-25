# Automatch guest rejected no-items MATCH_CONFIG — 2026-05-25

**Status:** SHIPPED  
**Symptom:** LAN automatch: UDP link sync OK, then `bootstrap host timed out waiting for READY`, guest `dropped=334`, UI "connection failed". Only when `SSB64_NETPLAY_AUTOMATCH_ITEMS` unset or `0`; `=1` worked.

## Root cause

Host `syNetPeerComposeAutomatchMatchMetadata()` defaults to **no items** (`item_toggles=0`) unless `SSB64_NETPLAY_AUTOMATCH_ITEMS=1`.

Guest automatch filter in `syNetPeerHandleMatchConfigPacket()` required `item_toggles == ~(u32)0`, silently dropping every host `MATCH_CONFIG` when items were off — guest never staged metadata or sent `READY`.

## Fix

Guest filter accepts host-authoritative `item_toggles` of **`0`** (no items) or **`~(u32)0`** (all items); still rejects partial/random toggle bitmasks.

## Code

`port/net/sys/netpeer.c` — `syNetPeerHandleMatchConfigPacket`
