# Kirby sync-code audit (2026-08-31)

Requested review of all Kirby-related netplay/rollback code, in light of the rbe scheduler
era and the fresh blade migration. Surface examined: 9 decomp ftkirby files with netmenu
blocks, wpkirbycutter, and the Kirby sites in netsync.c, netrollback.c,
netrollbacksnapshot.c (454 mentions), netplay_sim_quantize.c, and the diag TUs.

## Fixed in this pass

1. **Blade shells reached the eff fold as generic effects** (`netsync.c`,
   `syNetSyncFoldSingleEffectGObj`) — introduced by the 2026-08-31 migration, caught
   before it soaked. The generic tail folds raw `gobj->anim_frame`, and the PK-wave
   comment in the same function documents why that is wrong for presentation shells:
   anim advances 0/1/2 times per tick cross-ISA. Every dual-cutter window would have
   manufactured eff-only FC diverges between Linux and Android. Blades now take the
   PK-wave treatment — fold owner `status_id` + `status_total_tics`, skip anim/pos —
   via a new exported predicate (`syNetRbSnapshotLiveEffectIsKirbyCutterBladeInScope`).

2. **Reconcile pass B's attach re-arm retired** (`netrollbacksnapshot.c`). It wrote
   `fp->is_effect_attach = TRUE` outside the snapshot whenever an in-scope Kirby owned a
   live blade with the flag clear, on every load and every forward frame. Correct in the
   compensation era (wrongful ejects stripped the flag under live shells); post-migration
   it fights the fighter blob — a legitimately captured attach=FALSE-with-shell-alive
   window would be restored faithfully and then "repaired" into divergence on whichever
   peer ran the pass. The attach/eject ping-pong at ticks 1795–1805 of the dual-Kirby
   soak was this line trading blows with the decomp force-clear. The flag's only
   authority is now the fighter blob.

3. **NKirby gap in the stone-duration fold** (`netsync.c:406`) — the SpecialLw duration
   fold gated on `nFTKindKirby` only, while every snapshot-side scope accepts
   `nFTKindKirby || nFTKindNKirby`. One-sided coverage means an NKirby stone would
   hash-diverge silently. Aligned.

## Flagged, deliberately not changed

4. **`ftKirbySpecialNResolveInhaledCopyId` ships under bare `#ifdef PORT`**
   (`ftkirbyspecialn.c` ~305): fork-origin (commit "remove probes, patch bombs/items,
   resim patches") replacing the vanilla copy-id table read in OFFLINE builds too.
   Directive 7 says fork-only PORT blocks are netplay work → `PORT && SSB64_NETMENU` or
   JRickey review. If it is an LP64/bounds-safety fix it may be legitimately PORT-wide —
   but that call is JRickey's, not this audit's. **Needs review/decision.**

5. **Weapon-coupling scan reads union overlays ungated**
   (`netrollbacksnapshot.c`, the `status_vars.yoshi/link/samus/kirby/ness/pikachu`
   pointer-equality loop): raw union reads with no status gates — stale bytes from a
   dead overlay can pointer-match and wrongly keep a weapon coupled. Pre-existing,
   cross-character (not Kirby-specific), read-only, and low-probability (requires exact
   stale pointer collision), but it is the directive-6 anti-pattern verbatim. Worth an
   accessor-migration pass of its own.

6. **Bare `#if defined(SSB64_NETMENU)` (no PORT) in `ftkirbyspecialn.c`** (3 sites):
   diag-only inhale-copy trace, compile-equivalent today since NETMENU implies PORT.
   Consistency nit; fix opportunistically.

7. **`syNetRbSnapLiveEffectIsKirbyFinalCutterBlade` mutates during filtering**: its
   `RepinKirbyFinalCutterBladeJoint` side effect now runs from the capture/fold
   enumeration filters (post-migration, they call it far more often). The repin is
   idempotent joint-pointer normalization and arguably makes the fold MORE deterministic;
   accepted, but the function's name hides a write — future readers beware.

## Verified clean

- All five copy-ability files (`copymario/copyness/copypikachu/copylink/copysamus`):
  correct `PORT && SSB64_NETMENU` gates, runtime `syNetplayRollbackSemanticsActive()`
  gating, vanilla path preserved below.
- `ftkirbyspeciallw.c` stone-reset skip: properly scoped (mid-hold only, resist already
  active, duration alias guarded) — model netmenu block.
- `ftkirbyspecialhi.c` force-clear branches: consistent with the migration (their inputs
  — attach + shells — are now restored atomically from one slot).
- Inhale-wind machinery: distinct proc identity (`efManagerKirbyInhaleWindProcUpdate`),
  unaffected by the blade carve-out (which requires NoEject proc), still correctly
  excluded per the 2026-07-03 fold doc.
- The capture/inhale hash fold (`is_goto_pulled_wait`) goes through
  `ftStatusVarsCapture()` with status gates — accessor-clean.
- Quantize Kirby scopes (CopyLink boomerang/combat etc.): fkind-gated `passive_vars`
  reads (per-character block keyed by a match-stable fkind) — safe.
