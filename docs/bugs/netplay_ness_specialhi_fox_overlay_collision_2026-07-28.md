# Netplay: Ness PK Thunder delays wiped by Fox SpecialHi C2 overlay collision

**Date:** 2026-07-28  
**Status:** FIX IMPLEMENTED (`PORT && SSB64_NETMENU`, soak pending)  
**Soak:** Android guest ↔ Linux host, seed `386129764`, session `789297724`  
**Related:** [hold gravity blob authority](netplay_hold_gravity_blob_authority_2026-07-28.md), [healed frontier honest FC](netplay_fc_healed_frontier_honest_fc_2026-07-28.md)

## Symptom

Advance / FC hang path looked OK after honest-FC + gravity clamp-only. During Ness **air PK Thunder**, a light/FC resim through **SpecialAirHiStart** left Hold with no gravity freeze — Ness dropped at full terminal speed, thunder could not be caught for jibaku (non-vanilla).

```text
resim … load=1480 status=232 SpecialAirHiStart
sanitize_delay was=0 now=30 expected=30 status_tics=0   ← jibaku resurrected from tracking
hold_enter @1503 Android delay=9 gravity_delay=0
hold_enter @1503 Linux   delay=30 gravity_delay=0 + harden vel_y=-11.5
later resim harden vel_y=-36 … -55
```

## Root cause

1. **C2 ownership table is status_id-only.** `FillRange(FoxSpecialHiStart, FoxSpecialAirHi)` tags **227..232** as `FoxSpecialHi`.
2. Ness reuses those numbers for **SpecialHiStart..SpecialAirHiStart** (228..232). Ness Air Start **232** == Fox Air Firefox **232**.
3. Capture for Ness Start therefore read **`bank[FoxSpecialHi]`** (never written by Ness — zeros) instead of the live union. Apply projected those zeros over `status_vars.ness.specialhi` → `pkthunder_gravity_delay` / `pkjibaku_delay` wiped.
4. Tag validation probe set only `status_id` (no `fkind`), so heal stayed on the wrong overlay.
5. `sanitize_delay` resurrected jibaku from tracking (`was=0→30`) asymmetrically; gravity sanitize was already clamp-only → **hover permanently gone**, Harden drove terminal fall.

## Fix (architecture)

`PORT && SSB64_NETMENU`:

1. **`syNetplayStatusVarsExpectedOverlay`** — if table says `FoxSpecialHi`, require `fkind` Fox/NFox; otherwise `None` (union capture for Ness PK Thunder).
2. **Tag validate probe** — set `probe_fp.fkind = blob->fkind` before ExpectedOverlay.
3. **`SanitizePKThunderDelayIfZero`** — clamp-only (mirror gravity); remove ExpectedPkjibaku / HoldDelayZero rewrite path.

Follow-up (not this change): first-class `NessSpecialHi` bank overlay + accessors when C2 migrates character specials; same fkind gate pattern for any future character FillRange.

## Acceptance

- [ ] Re-soak Ness air PK: Start resim must not log `sanitize_delay was=0 now=…`; `hold_enter` gravity_delay matches Start age; Android/Linux same `delay` / `gravity_delay`.
- [ ] No terminal Harden (`vel_y` ≈ −tvel) immediately after mid-Start/Hold resim when inputs agree.
- [ ] Fox Firefox C2 path unchanged (Fox/NFox still tag `FoxSpecialHi`).
