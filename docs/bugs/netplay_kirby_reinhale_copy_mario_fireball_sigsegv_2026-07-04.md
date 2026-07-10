# Netplay Kirby re-inhale → Copy Mario fireball SIGSEGV (`fault_addr=0x10`)

**Status:** FIX IMPLEMENTED (`PORT` crash guard; inhale copy_id hardening in `PORT`, soak pending)  
**Session:** soak2 `1786823451` (Link P0 / Kirby P1, both peers)

## Symptoms

- Clean drift scan through tick 1402 (no `LOAD_HASH_DRIFT`, no FC diverge).
- Both peers: `SIGSEGV fault_addr=0x10` immediately after sim tick 1402.
- Linux backtrace: `portFixupStructU16` ← `wpManagerMakeWeapon` ← `wpMarioFireballMakeWeapon` ← Kirby `CopyMarioSpecialN` (`gFTMarioFileSpecial1` NULL).
- Repro narrative: dispose Link copy, re-inhale Link, Kirby shows **Mario hat/ability**, neutral B crashes.

## Log timeline (Linux guest)

| Ticks | Kirby status | Meaning |
|-------|--------------|---------|
| 418–471 | 272→273→277 | 1st inhale → Copy Link |
| 616–1027 | 287 | CopyLinkSpecialN (worked) |
| 1308–1373 | 272→273→277 | 2nd inhale → copy commit |
| **1388** | **231** | **CopyMarioSpecialN** (wrong — expected 287 CopyLink) |
| 1402 | 231 | Crash on fireball spawn frame |

## Root causes

### 1. Immediate crash — absent Mario weapon file

Kirby Copy Mario/Luigi fireball uses `dWPMarioFireballWeaponAttributes[].p_weapon = &gFTMarioFileSpecial1` (or Luigi special1). Mario is not in a Link-vs-Kirby match, so `gFTMarioFileSpecial1` stays NULL. `wpManagerMakeWeapon` dereferences attributes via `portFixupStructU16(attr, 0x10, …)` on NULL → SIGSEGV @ `0x10`.

Same class as Giant DK / wrong copy dispatch documented in `ftmanager.c` Kirby copy-table fixup comment.

### 2. Wrong copy dispatch — passive `copy_id` became Mario (0)

`ftKirbySpecialNCopyInitCopyVars` applies `status_vars.kirby.specialn.copy_id` to `passive_vars.kirby.copy_id` on the copy-commit anim event. If that status field is **0** while passive still held Link (5), Kirby gets Mario hat and `dFTKirbySpecialNStatusList[0]` → CopyMario.

`ftKirbySpecialNStatusVars.copy_id` aliases `ftKirbySpecialLwStatusVars.duration` in the status-vars union — stale stone/inhale overlay bytes can stomp `specialn.copy_id` between catch and copy commit. Catch also indexed `copy[victim_fkind].copy_id` from the reloc table instead of using victim `fkind` directly (table consumer domain mismatch risk).

## Fixes

| Layer | Change |
|-------|--------|
| **Crash guard (`PORT`)** | `ftManagerEnsureCopyWeaponFilesLoaded(copy_id)` lazy-loads Mario/Luigi special1 via `ftManagerSetupFilesAllKind` before fireball spawn. |
| **NULL guard (`PORT`)** | `wpMarioFireballMakeWeapon` and `wpManagerMakeWeapon` return NULL if weapon file pointer or resolved attributes are NULL (no SIGSEGV). |
| **Inhale hardening (`PORT`)** | Catch + copy commit resolve copy_id from victim `fkind` (vanilla roster) with NKirby copy-table fallback; bounds-check modelpart lookup. |
| **Diagnostic (`PORT && SSB64_NETMENU`)** | `SSB64_NETPLAY_KIRBY_INHALE_COPY_TRACE=1` logs catch / copy_init: passive, status copy_id, resolved, victim fkind. |

## Files touched

- `decomp/src/ft/ftmanager.c`, `ftmanager.h` — lazy load helper
- `decomp/src/wp/wpmario/wpmariofireball.c`, `wpmanager.c` — ensure + NULL guards
- `decomp/src/ft/ftchar/ftkirby/ftkirbyspecialn.c` — catch/copy_id resolve + trace

## Test plan

1. Link vs Kirby soak: dispose Link copy (copy star or ability), re-inhale Link, neutral B → CopyLink boomerang, no crash.
2. Force Copy Mario in same match (debug / AI): fireball spawns without SIGSEGV (lazy load).
3. Optional: `SSB64_NETPLAY_KIRBY_INHALE_COPY_TRACE=1` on re-inhale — `resolved=5`, not `0`.
