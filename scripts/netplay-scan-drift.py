#!/usr/bin/env python3
"""
Quick pass/fail scan of a host/guest netplay log pair for rollback resim gaps.

Focuses on the failure classes that indicate a resim capture/reload hole:
  * LOAD_HASH_DRIFT          - snapshot reload hash != live hash after apply
  * "respawn unsupported"    - a GObj kind the resim could not recreate
                               (also catches the related "respawn failed" lines)
  * SYNCTEST_FAIL            - periodic save/load/verify probe failed
                               (SSB64_NETPLAY_ROLLBACK_SYNCTEST=1)
  * FRAME_COMMIT_STATE_DIVERGE - the AUTHORITATIVE cross-peer check. SYNCTEST only
                               verifies a peer-LOCAL save->reload round trip; this is the
                               actual host-vs-guest committed-state comparison. A partition
                               delta with inp_local==inp_peer (identical inputs) is a hard
                               cross-ISA determinism failure even if a deferred resim later
                               "recovers" it. Without this check a run that visibly desynced
                               could still print RESULT: WARN.
  * PEER_BASELINE_RESYNC_STORM - the deferred resim could not reanchor and aborted
                               (terminal, unrecoverable desync -> usually VS session stop)
  * peer-only map phase fork   - paired map_hash_save / pupupu_ground show a sustained
                               +1 tick phase skew (Android[t] == Linux[t-1]) after resim
                               even when local SYNCTEST / LOAD_HASH_DRIFT stay clean.
                               Catches silent Whispy blink/wind_wait forks that only
                               surface later as PEER_SNAPSHOT_DIVERGE.
  * PHYSICS_FORK_ONSET         - first cross-peer MpLanding tr_x/tr_y mismatch (often
                               tens of ticks before FRAME_COMMIT). Multi-actor guts
                               report domain=ft|wp (+ player/kind); prefer wp over
                               last-write fighter Hold when both land same gut.
                               topn_ty-only fighter forks are tagged hold_gravity_risk
                               (not SoftLipX cliff X).
  * HOLD_GRAVITY_RESURRECT     - NESS_PKTHUNDER_GATE sanitize_gravity was<now during
                               Hold; demoted to noise when both peers match (symmetric
                               Start sanitize). Asymmetric only → FAIL risk.
  * TURN_DASH_LR_DASH_FORK     - TURN_DASH_WITNESS lr_dash / did_dash disagree (InvertLR
                               stomp → Turn vs Dash; soak 1646535146).
  * STATUS_FORK_ONSET          - first fighter_slot_hash status_id cross-peer diverge
                               (demoted when statuses re-agree within lookforward —
                               jump-button GGPO noise; soak 2120480047 @391).
  * EARLIEST_FORK              - min(PHYSICS_FORK, STATUS_FORK, FC, PEER_SNAPSHOT).
  * SOFTLIP_PHASE_FORK         - first SoftLipPhase gut+phase topn_x/vel_x/ja_* mismatch.
  * SoftLipPhase parity WARN   - one peer has SoftLipPhase rows and the other has 0
                               (stale desktop binary vs Android).
  * RESIM_STICK_FORK           - cross-peer STICK_SAMPLE disagree near GGPO/PHYSICS_FORK
                               (first-pass sample only; last-write PEER resim is noise).
  * SEAL_LEDGER_STOMP          - REMOTE_PUBLISH_SEAL_OVERRIDE / SEALED_RESIM_LEDGER_SKIP.
  * FIGHTER_LIGHT_ONSET        - first fighter_slot_hash fhash_light DIFF (anim OK).
  * SoftLipX asym              - writer paths only; floor_edge_skip-only demoted to noise.
  * sim_state cadence parity   - WARN when one peer has << sim_state_tick / fighter_slot_hash
                               rows (Android missing INTERVAL=1 hides early forks).

It also surfaces, without failing the run:
  * SYNCTEST_OK count        - proves synctest actually probed (0 == it never ran)
  * SYNCTEST_SKIP histogram  - "fragile" windows synctest refuses to verify; a
                               scope that ALWAYS skips is a blind spot, not a pass
  * soft-continued drift     - drift the engine tolerated instead of stopping.
                               With SSB64_NETPLAY_ROLLBACK_LOAD_HASH_SOFT=0 this
                               should be ~0; any remaining are the >=8-rollback
                               auto-soften threshold (netrollback.c) kicking in.

It reads logs as a pair the same way scripts/netplay-trim-logs.py does
(--label NAME PATH, repeatable) and runs the same session-identity sanity
check so mis-paired host.log/guest.log from different automatch sessions are
obvious before you trust the result.

Recommended capture env (per peer):
  SSB64_NETPLAY_ROLLBACK_SYNCTEST=1 \
  SSB64_NETPLAY_ROLLBACK_LOAD_HASH_SOFT=0 \
  SSB64_NETPLAY_ROLLBACK_VERIFY_EFFECT_HASH=1 \
  SSB64_NETPLAY_GOBJ_LINK_AUDIT=1 \
  SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL=1 \
  SSB64_NETPLAY_FIGHTER_SLOT_HASH_LOG=1 \
  SSB64_NETPLAY_LANDING_BRANCH_DIAG=1 \
  SSB64_NETPLAY_SOFTLIP_X_DIAG=1 \
  SSB64_NETPLAY_SNAPSHOT_FIGHTER_FIELD_DIFF=1
  # Cliff / JumpAerial soft-lip soaks: scripts/netplay-cliff-softlip-soak.env.example

Usage:
  ./scripts/netplay-scan-drift.py --label host host.log --label guest guest.log
  ./scripts/netplay-scan-drift.py host.log guest.log        # positional pair
  ./scripts/netplay-scan-drift.py *.log --strict            # recovered drift -> FAIL

Options:
  --label NAME PATH   Input log (repeatable). Without it, positional paths are
                      auto-labeled (2 paths -> host/guest, else by basename).
  --strict            Treat *recovered* (soft-continued / presentational-only)
                      drift as failure too, not just WARN.
  --show-lines        Echo each matched raw log line under its tick.
  -q / --quiet        Only print the final RESULT line.

Exit code: 0 = PASS, 1 = WARN, 2 = FAIL.
  FAIL  = a fatal/blocked drift, any unsupported/failed respawn, SYNCTEST_FAIL, a
          cross-peer FRAME_COMMIT_STATE_DIVERGE, or an aborted baseline resync storm.
  WARN  = only recovered drift (soft-continue, repair-ok, presentational-only).
  PASS  = none of the above.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

# nWPKind enum (decomp/src/wp/wpdef.h) for readable kind annotation.
WP_KIND_NAMES = {
    0x00: "Fireball", 0x01: "Blaster", 0x02: "ChargeShot", 0x03: "SamusBomb",
    0x04: "Cutter", 0x05: "EggThrow", 0x06: "YoshiStar", 0x07: "Boomerang",
    0x08: "SpinAttack", 0x09: "ThunderJoltAir", 0x0A: "ThunderJoltGround",
    0x0B: "ThunderHead", 0x0C: "ThunderTrail", 0x0D: "PKFire",
    0x0E: "PKThunderHead", 0x0F: "PKThunderTrail", 0x10: "BulletNormal",
    0x11: "BulletHard", 0x12: "ArwingLaser2D", 0x13: "ArwingLaser3D",
    0x14: "LGunAmmo", 0x15: "FFlowerFlame", 0x16: "StarRodStar",
    0x17: "IwarkRock", 0x18: "NyarsCoin", 0x19: "LizardonFlame",
    0x1A: "SpearSwarm", 0x1B: "KamexHydro", 0x1C: "StarmieSwift",
    0x1D: "DogasSmog", 0x1E: "HitokageFlame", 0x1F: "FushigibanaRazor",
}

TICK_RE = re.compile(r"\btick[= ](\d+)\b")
# Partition hash tuples on the primary drift report line: name=0xSNAP/0xLIVE.
PARTITION_RE = re.compile(
    r"\b(figh|world|item|wpn|map|rng|cam|anim|eff)=0x([0-9A-Fa-f]+)/0x([0-9A-Fa-f]+)"
)
DRIFT_TOKEN = "LOAD_HASH_DRIFT"
RESPAWN_RE = re.compile(
    r"(weapon|item) respawn (unsupported|failed)\b.*?\bkind=(-?\d+)"
)
# Synctest probe outcome (SSB64_NETPLAY_ROLLBACK_SYNCTEST=1):
#   SYNCTEST_OK   tick=N
#   SYNCTEST_FAIL tick=N
#   SYNCTEST_SKIP tick=N reason=<token> [probe=N]
SYNCTEST_RE = re.compile(
    r"SYNCTEST_(FAIL|OK|SKIP)\b(?:\s+tick=(\d+))?(?:.*?\breason=(\S+))?"
)

# Cross-peer frame-commit validation. The snapshot SYNCTEST only verifies a peer-LOCAL
# save->reload round trip; FRAME_COMMIT_STATE_DIVERGE is the AUTHORITATIVE cross-peer check
# (the peer's committed hashes arrive over the wire). The line carries every partition for
# both sides: "validation=N local figh=0x.. ... | peer figh=0x.. ... inp_local=0x.. inp_peer=0x..".
# A divergence here with identical inputs is a hard determinism failure even if a deferred
# resim later "recovers" it. This scan previously ignored these lines entirely (it only read
# LOAD_HASH_DRIFT / SYNCTEST), so a run that desynced cross-peer could still print RESULT: WARN.
FC_DIVERGE_RE = re.compile(
    r"FRAME_COMMIT_STATE_DIVERGE\s+validation=(\d+)\s+local\s+(.*?)\s+\|\s+peer\s+(.*)"
)
FC_PARTITION_RE = re.compile(r"\b(figh|world|item|wpn|map|rng|cam|anim|eff)=0x([0-9A-Fa-f]+)")
FC_INPUTS_RE = re.compile(r"inp_local=0x([0-9A-Fa-f]+)\s+inp_peer=0x([0-9A-Fa-f]+)")
# Deferred resim could not reanchor and gave up -> terminal, unrecoverable desync.
RESYNC_STORM_RE = re.compile(
    r"PEER_BASELINE_RESYNC_STORM\b.*?load_tick=(\d+).*?sim=(\d+).*?aborting"
)
# Session teardown markers (terminal): a local stop or a received END.
SESSION_STOP_RE = re.compile(r"VS session stop\b|received VS_SESSION_END\b")
GGPO_QUEUED_CLASS_RE = re.compile(
    r"GGPO input correction queued(?: class=(\w+))?"
)
GGPO_QUEUED_TICK_RE = re.compile(
    r"GGPO input correction queued\b.*?sim_tick=(\d+)"
)
GGPO_CORRECTION_TUPLE_RE = re.compile(
    r"CORRECTION_TUPLE\b.*?mismatch=(\d+)"
)
GGPO_SKIP_MICRO_RE = re.compile(r"GGPO stick replace skipped class=micro_stick")
GGPO_CLASS_SUMMARY_RE = re.compile(
    r"GGPO_CLASS_SUMMARY queued button=(\d+) onset_from_zero=(\d+) "
    r"release=(\d+) real_stick=(\d+) micro_stick=(\d+) \| skipped_micro=(\d+)"
)
PEER_SNAPSHOT_DIVERGE_RE = re.compile(
    r"PEER_SNAPSHOT_DIVERGE\b.*?load_tick=(\d+)"
)

# Peer-only map phase fork (Whispy / ground map) — local synctest is blind to this.
MAP_HASH_SAVE_RE = re.compile(
    r"map_hash_save tick=(\d+) hash_map=0x([0-9A-Fa-f]+)"
)
PUPUPU_GROUND_RE = re.compile(
    r"pupupu_ground tick=(\d+) wind_wait=(-?\d+) wind_dur=(-?\d+) blink=(-?\d+)"
)
POST_RESIM_LIVE_RE = re.compile(
    r"POST_RESIM_LIVE sim=(\d+) target=(\d+)"
)
MPLANDING_RE = re.compile(
    r"SSB64 MpLanding: landing_branch gut=(\d+)"
    r"(?:.*?domain=(\S+)\s+player=(-?\d+)\s+kind=(-?\d+))?"
    r".*?fflags=(0x[0-9A-Fa-f]+).*?"
    r"tr_x=(0x[0-9A-Fa-f]+) tr_y=(0x[0-9A-Fa-f]+)"
)
SOFTLIPX_RE = re.compile(
    r"SSB64 SoftLipX: gut=(\d+) path=(\S+) suppressed=(\d+) .*?"
    r"(?:residual_fflags|fflags)=(0x[0-9A-Fa-f]+) sticky=(0x[0-9A-Fa-f]+) softlip=(\d+)"
)
SOFTLIP_PHASE_RE = re.compile(
    r"SSB64 SoftLipPhase: gut=(\d+) phase=(\S+) (?:domain=(\S+) )?"
    r"player=(-?\d+) status=(-?\d+) "
    r"topn_x=(0x[0-9A-Fa-f]+) vel_x=(0x[0-9A-Fa-f]+) "
    r"ja_vel_x=(0x[0-9A-Fa-f]+) ja_drift=(0x[0-9A-Fa-f]+)"
)
# decomp wpdef.h nWPKindPKThunderHead = 0x0E — Hold-head CLIFF map → jibaku launch.
WP_KIND_PK_THUNDER_HEAD = 0x0E
# Hold gravity delay resurrect / block (soak 128377995 fall-onset +1 tick).
NESS_SANITIZE_GRAVITY_RE = re.compile(
    r"NESS_PKTHUNDER_GATE tick=(\d+) event=sanitize_gravity player=(\d+) status=(\d+) "
    r"was=(-?\d+) now=(-?\d+) expected=(-?\d+)"
)
NESS_HOLD_GRAVITY_RESURRECT_BLOCKED_RE = re.compile(
    r"NESS_PKTHUNDER_GATE tick=(\d+) event=hold_gravity_resurrect_blocked "
    r"player=(\d+) status=(\d+) live=(-?\d+) expected=(-?\d+)"
)
NESS_HOLD_FALL_ONSET_HARDEN_RE = re.compile(
    r"NESS_PKTHUNDER_GATE tick=(\d+) event=hold_fall_onset_harden player=(\d+)"
)
TURN_DASH_WITNESS_RE = re.compile(
    r"TURN_DASH_WITNESS phase=(\S+) tick=(\d+) player=(\d+) .*?"
    r"lr_dash=(-?\d+)(?: entry_lr_dash=(-?\d+))? lr_turn=(-?\d+) .*?"
    r"did_dash=(\d+)"
)
TURN_DASH_HARDEN_RE = re.compile(
    r"TURN_DASH_WITNESS phase=harden_lr_dash tick=(\d+) player=(\d+) "
    r"was=(-?\d+) now=(-?\d+) entry=(-?\d+)"
)
STICK_SAMPLE_RE = re.compile(
    r"SSB64 STICK_SAMPLE mode=\S+ tick=(\d+) player=(\d+) sx=(-?\d+) sy=(-?\d+)"
)
SEAL_OVERRIDE_RE = re.compile(
    r"REMOTE_PUBLISH_SEAL_OVERRIDE player=(\d+) sim_tick=(\d+) .* "
    r"old btn=0x[0-9A-Fa-f]+ sx=(-?\d+) sy=(-?\d+) \| "
    r"sealed btn=0x[0-9A-Fa-f]+ sx=(-?\d+) sy=(-?\d+)"
)
SEALED_RESIM_LEDGER_SKIP_RE = re.compile(
    r"SEALED_RESIM_LEDGER_SKIP player=(\d+) sim_tick=(\d+) "
    r"sealed btn=0x[0-9A-Fa-f]+ sx=(-?\d+) sy=(-?\d+) \| "
    r"ledger btn=0x[0-9A-Fa-f]+ sx=(-?\d+) sy=(-?\d+)"
)
SIM_STATE_TICK_LINE_RE = re.compile(r"SSB64 NetSync: sim_state_tick tick=(\d+)\b")
FIGHTER_SLOT_HASH_LINE_RE = re.compile(r"SSB64 NetSync: fighter_slot_hash tick=(\d+)\b")
FIGHTER_SLOT_HASH_DETAIL_RE = re.compile(
    r"SSB64 NetSync: fighter_slot_hash tick=(\d+) player=(\d+) .* "
    r"fhash_light=(0x[0-9A-Fa-f]+) .* anim_hash=(0x[0-9A-Fa-f]+)"
)
# SoftLipX paths that write / keep TopN.x (not mere floor_edge_skip presence).
SOFTLIPX_WRITER_PATHS = frozenset(
    {
        "lwall_keep",
        "rwall_keep",
        "lwall_suppress",
        "rwall_suppress",
        "lwall_adjnew",
        "rwall_adjnew",
        "lwall_run",
        "rwall_run",
        "ceil_coll_x",
        "ceil_coll_x_skip",
        "ceil_edge_l",
        "ceil_edge_r",
        "ceil_edge_skip",
        "floor_edge_l",
        "floor_edge_r",
        "landing_floor_x",
        "collide_floor_x",
        "floor_new_wall_edge",
    }
)
SOFTLIPX_NOISE_ONLY_PATHS = frozenset({"floor_edge_skip"})
FC_SEED_HINT_RE = re.compile(
    r"FRAME_COMMIT_SEED_HINT\s+validation=(\d+)\s+onset=(\d+)\s+last_agreed=(\d+)\s+"
    r"recovery_seed=(\d+)\s+physics_seed=(\d+)"
)

# Pair-session identity (mirror of netplay-trim-logs.py regexes).
MM_POLL_MATCHED_RE = re.compile(r"MM_POLL_MATCHED .+ session=(\d+) host=(\d+)")
BOOTSTRAP_APPLIED_RE = re.compile(r"bootstrap metadata applied host=\d+ stage=\d+ seed=(\d+)")
HOST_START_RE = re.compile(r"bootstrap host sent START stage=\d+ seed=(\d+)")

# Drift resolution severity. Higher = worse. Order of substring checks matters.
SEV_PASS, SEV_WARN, SEV_FAIL = 0, 1, 2

# (substring, label, severity) — first match wins; bare report line falls through.
RESOLUTION_RULES = [
    ("restoring live world and stopping", "STOP (session ended)", SEV_FAIL),
    ("deferring session stop", "defer-stop (fidelity)", SEV_FAIL),
    ("soft-continue blocked", "blocked", SEV_FAIL),
    ("soft-ignored", "soft-ignored (baseline storm)", SEV_WARN),
    ("repair ok", "repair-ok", SEV_WARN),
    ("resim-sim-core-ok", "sim-core-ok (continued)", SEV_WARN),
    ("presentational-only", "presentational-only", SEV_WARN),
    ("fc_recovery", "fc-recovery", SEV_WARN),
    ("soft-continue", "soft-continue", SEV_WARN),
]


@dataclass
class TickDrift:
    tick: int
    diverged: set[str] = field(default_factory=set)
    resolutions: list[tuple[str, int]] = field(default_factory=list)  # (label, sev)
    has_report: bool = False
    lines: list[str] = field(default_factory=list)

    def severity(self) -> int:
        if self.resolutions:
            return max(sev for _, sev in self.resolutions)
        # Bare report with no follow-up resolution: unresolved -> treat as FAIL.
        return SEV_FAIL if self.has_report else SEV_WARN

    def resolution_text(self) -> str:
        if self.resolutions:
            return ", ".join(dict.fromkeys(lbl for lbl, _ in self.resolutions))
        return "UNRESOLVED (no follow-up)" if self.has_report else "(report only)"


@dataclass
class FCDiverge:
    """A cross-peer FRAME_COMMIT_STATE_DIVERGE occurrence."""
    tick: int
    diverged: set[str]
    inputs_match: bool | None  # True/False if inp_local/inp_peer present, else None
    raw: str


@dataclass
class Respawn:
    domain: str  # weapon | item
    status: str  # unsupported | failed
    kind: int
    tick: int | None
    raw: str


@dataclass
class FileScan:
    label: str
    path: str
    exists: bool = True
    ticks: dict[int, TickDrift] = field(default_factory=dict)
    respawns: list[Respawn] = field(default_factory=list)
    synctest_ok: int = 0
    synctest_fail: list[int] = field(default_factory=list)
    synctest_skip: dict[str, int] = field(default_factory=dict)
    fc_diverges: list[FCDiverge] = field(default_factory=list)
    resync_storms: list[tuple[int, int]] = field(default_factory=list)  # (load_tick, sim)
    session_stops: int = 0
    session_id: str | None = None
    seed: str | None = None
    # Input-contract GGPO class telemetry (informational; does not affect RESULT).
    ggpo_queued: int = 0
    ggpo_by_class: dict[str, int] = field(default_factory=dict)
    ggpo_skipped_micro: int = 0
    # tick -> last map_hash_save / pupupu_ground observed (forward+resim; last write wins).
    map_hash_by_tick: dict[int, str] = field(default_factory=dict)
    pupupu_by_tick: dict[int, tuple[int, int]] = field(default_factory=dict)  # blink, wind_wait
    post_resim_live: list[tuple[int, int]] = field(default_factory=list)  # (sim, target)
    # gut -> actors (domain, player, kind, fflags, tr_x, tr_y); multi-actor per gut.
    mplanding_by_gut: dict[int, list[tuple[str, int, int, str, str, str]]] = field(
        default_factory=dict
    )
    # SoftLipX rows: (gut, path, suppressed, residual_fflags, sticky, softlip)
    softlipx_rows: list[tuple[int, str, int, str, str, int]] = field(default_factory=list)
    # SoftLipPhase: (gut, phase, domain, player, status, topn_x, vel_x, ja_vel_x, ja_drift)
    softlip_phase_rows: list[
        tuple[int, str, str, int, int, str, str, str, str]
    ] = field(default_factory=list)
    # fighter_slot_hash detail: tick -> player -> (fhash_light, anim_hash)
    fighter_slot_by_tick: dict[int, dict[int, tuple[str, str]]] = field(default_factory=dict)
    sim_state_tick_count: int = 0
    fighter_slot_hash_count: int = 0
    fc_seed_hints: list[tuple[int, int, int, int, int]] = field(default_factory=list)
    ggpo_mismatch_ticks: set[int] = field(default_factory=set)
    peer_snapshot_load_ticks: list[int] = field(default_factory=list)
    # Hold gravity: (tick, player, was, now, expected) when sanitize raised delay.
    hold_gravity_resurrect: list[tuple[int, int, int, int, int]] = field(
        default_factory=list
    )
    # hold_gravity_resurrect_blocked rows: (tick, player, live, expected)
    hold_gravity_resurrect_blocked: list[tuple[int, int, int, int]] = field(
        default_factory=list
    )
    hold_fall_onset_harden_ticks: list[int] = field(default_factory=list)
    # TURN_DASH_WITNESS: (tick, phase, player, lr_dash, entry_lr_dash|None, lr_turn, did_dash)
    turn_dash_rows: list[tuple[int, str, int, int, int | None, int, int]] = field(
        default_factory=list
    )
    turn_dash_harden: list[tuple[int, int, int, int, int]] = field(default_factory=list)
    # fighter_slot_hash status trail: tick -> player -> status_id
    fighter_status_by_tick: dict[int, dict[int, int]] = field(default_factory=dict)
    # STICK_SAMPLE: tick -> player -> (sx, sy) first write only (live pass before PEER resim).
    stick_sample_by_tick: dict[int, dict[int, tuple[int, int]]] = field(
        default_factory=dict
    )
    # REMOTE_PUBLISH_SEAL_OVERRIDE: (sim_tick, player, old_sx, old_sy, sealed_sx, sealed_sy)
    seal_overrides: list[tuple[int, int, int, int, int, int]] = field(
        default_factory=list
    )
    # SEALED_RESIM_LEDGER_SKIP: (sim_tick, player, sealed_sx, sealed_sy, ledger_sx, ledger_sy)
    sealed_resim_ledger_skips: list[tuple[int, int, int, int, int, int]] = field(
        default_factory=list
    )

    def severity(self) -> int:
        sev = SEV_PASS
        for td in self.ticks.values():
            sev = max(sev, td.severity())
        if self.respawns:
            sev = max(sev, SEV_FAIL)
        if self.synctest_fail:
            sev = max(sev, SEV_FAIL)
        # A cross-peer frame-commit divergence (with a real partition diff) is a hard
        # determinism failure regardless of recovery; an aborted resync is terminal.
        if any(fc.diverged for fc in self.fc_diverges):
            sev = max(sev, SEV_FAIL)
        if self.resync_storms:
            sev = max(sev, SEV_FAIL)
        return sev

    def soft_continue_ticks(self) -> list[int]:
        out = []
        for t, td in self.ticks.items():
            if any(lbl == "soft-continue" for lbl, _ in td.resolutions):
                out.append(t)
        return sorted(out)


def classify_resolution(line: str) -> tuple[str, int] | None:
    low = line.lower()
    for sub, label, sev in RESOLUTION_RULES:
        if sub in low:
            return (label, sev)
    return None


def _tick_for(scan: FileScan, tick: int) -> TickDrift:
    td = scan.ticks.get(tick)
    if td is None:
        td = TickDrift(tick=tick)
        scan.ticks[tick] = td
    return td


def scan_file(label: str, path: str) -> FileScan:
    scan = FileScan(label=label, path=path)
    p = Path(path)
    if not p.is_file():
        scan.exists = False
        return scan

    with p.open("r", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")

            if scan.session_id is None:
                m = MM_POLL_MATCHED_RE.search(line)
                if m:
                    scan.session_id = m.group(1)
            if scan.seed is None:
                m = BOOTSTRAP_APPLIED_RE.search(line) or HOST_START_RE.search(line)
                if m:
                    scan.seed = m.group(1)

            sm = SYNCTEST_RE.search(line)
            if sm:
                status = sm.group(1)
                tk = int(sm.group(2)) if sm.group(2) else -1
                if status == "OK":
                    scan.synctest_ok += 1
                elif status == "FAIL":
                    scan.synctest_fail.append(tk)
                else:  # SKIP
                    reason = sm.group(3) or "(unknown)"
                    scan.synctest_skip[reason] = scan.synctest_skip.get(reason, 0) + 1
                continue

            fcm = FC_DIVERGE_RE.search(line)
            if fcm:
                tick = int(fcm.group(1))
                local_fields = dict(FC_PARTITION_RE.findall(fcm.group(2)))
                peer_fields = dict(FC_PARTITION_RE.findall(fcm.group(3)))
                diverged = {
                    name
                    for name, lval in local_fields.items()
                    if name in peer_fields and lval.lower() != peer_fields[name].lower()
                }
                im = FC_INPUTS_RE.search(line)
                inputs_match = (
                    im.group(1).lower() == im.group(2).lower() if im else None
                )
                scan.fc_diverges.append(
                    FCDiverge(tick=tick, diverged=diverged, inputs_match=inputs_match, raw=line)
                )
                continue

            stm = RESYNC_STORM_RE.search(line)
            if stm:
                scan.resync_storms.append((int(stm.group(1)), int(stm.group(2))))
                continue

            if SESSION_STOP_RE.search(line):
                scan.session_stops += 1
                continue

            gsum = GGPO_CLASS_SUMMARY_RE.search(line)
            if gsum:
                scan.ggpo_by_class = {
                    "button": int(gsum.group(1)),
                    "onset_from_zero": int(gsum.group(2)),
                    "release": int(gsum.group(3)),
                    "real_stick": int(gsum.group(4)),
                    "micro_stick": int(gsum.group(5)),
                }
                scan.ggpo_skipped_micro = int(gsum.group(6))
                scan.ggpo_queued = sum(scan.ggpo_by_class.values())
                continue

            gq = GGPO_QUEUED_CLASS_RE.search(line)
            if gq:
                scan.ggpo_queued += 1
                cls = gq.group(1) or "unclassified"
                scan.ggpo_by_class[cls] = scan.ggpo_by_class.get(cls, 0) + 1
                gt = GGPO_QUEUED_TICK_RE.search(line)
                if gt:
                    scan.ggpo_mismatch_ticks.add(int(gt.group(1)))
                continue

            ct = GGPO_CORRECTION_TUPLE_RE.search(line)
            if ct:
                scan.ggpo_mismatch_ticks.add(int(ct.group(1)))
                continue

            if GGPO_SKIP_MICRO_RE.search(line):
                scan.ggpo_skipped_micro += 1
                continue

            ps = PEER_SNAPSHOT_DIVERGE_RE.search(line)
            if ps:
                scan.peer_snapshot_load_ticks.append(int(ps.group(1)))
                continue

            mm = MAP_HASH_SAVE_RE.search(line)
            if mm:
                scan.map_hash_by_tick[int(mm.group(1))] = mm.group(2).lower()
                continue

            pm = PUPUPU_GROUND_RE.search(line)
            if pm:
                scan.pupupu_by_tick[int(pm.group(1))] = (
                    int(pm.group(4)),
                    int(pm.group(2)),
                )
                continue

            pr = POST_RESIM_LIVE_RE.search(line)
            if pr:
                scan.post_resim_live.append((int(pr.group(1)), int(pr.group(2))))
                continue

            ml = MPLANDING_RE.search(line)
            if ml:
                gut = int(ml.group(1))
                domain = ml.group(2) if ml.group(2) else "?"
                player = int(ml.group(3)) if ml.group(3) is not None else -1
                kind = int(ml.group(4)) if ml.group(4) is not None else -1
                fflags = ml.group(5).lower()
                tr_x = ml.group(6).lower()
                tr_y = ml.group(7).lower()
                scan.mplanding_by_gut.setdefault(gut, []).append(
                    (domain, player, kind, fflags, tr_x, tr_y)
                )
                continue

            sx = SOFTLIPX_RE.search(line)
            if sx:
                scan.softlipx_rows.append(
                    (
                        int(sx.group(1)),
                        sx.group(2),
                        int(sx.group(3)),
                        sx.group(4).lower(),
                        sx.group(5).lower(),
                        int(sx.group(6)),
                    )
                )
                continue

            sp = SOFTLIP_PHASE_RE.search(line)
            if sp:
                scan.softlip_phase_rows.append(
                    (
                        int(sp.group(1)),
                        sp.group(2),
                        sp.group(3) if sp.group(3) else "ft",
                        int(sp.group(4)),
                        int(sp.group(5)),
                        sp.group(6).lower(),
                        sp.group(7).lower(),
                        sp.group(8).lower(),
                        sp.group(9).lower(),
                    )
                )
                continue

            if SIM_STATE_TICK_LINE_RE.search(line):
                scan.sim_state_tick_count += 1
                continue

            fs = FIGHTER_SLOT_HASH_DETAIL_RE.search(line)
            if fs:
                tick = int(fs.group(1))
                player = int(fs.group(2))
                scan.fighter_slot_hash_count += 1
                scan.fighter_slot_by_tick.setdefault(tick, {})[player] = (
                    fs.group(3).lower(),
                    fs.group(4).lower(),
                )
                # status= in the same line (before fhash_light)
                sm = re.search(r"\bstatus=(\d+)\b", line)
                if sm:
                    scan.fighter_status_by_tick.setdefault(tick, {})[player] = int(
                        sm.group(1)
                    )
                continue

            if FIGHTER_SLOT_HASH_LINE_RE.search(line):
                scan.fighter_slot_hash_count += 1
                continue

            hint = FC_SEED_HINT_RE.search(line)
            if hint:
                scan.fc_seed_hints.append(
                    (
                        int(hint.group(1)),
                        int(hint.group(2)),
                        int(hint.group(3)),
                        int(hint.group(4)),
                        int(hint.group(5)),
                    )
                )
                continue

            sg = NESS_SANITIZE_GRAVITY_RE.search(line)
            if sg:
                was, now = int(sg.group(4)), int(sg.group(5))
                if was < now:
                    scan.hold_gravity_resurrect.append(
                        (
                            int(sg.group(1)),
                            int(sg.group(2)),
                            was,
                            now,
                            int(sg.group(6)),
                        )
                    )
                continue

            rb = NESS_HOLD_GRAVITY_RESURRECT_BLOCKED_RE.search(line)
            if rb:
                scan.hold_gravity_resurrect_blocked.append(
                    (
                        int(rb.group(1)),
                        int(rb.group(2)),
                        int(rb.group(4)),
                        int(rb.group(5)),
                    )
                )
                continue

            fo = NESS_HOLD_FALL_ONSET_HARDEN_RE.search(line)
            if fo:
                scan.hold_fall_onset_harden_ticks.append(int(fo.group(1)))
                continue

            tdh = TURN_DASH_HARDEN_RE.search(line)
            if tdh:
                scan.turn_dash_harden.append(
                    (
                        int(tdh.group(1)),
                        int(tdh.group(2)),
                        int(tdh.group(3)),
                        int(tdh.group(4)),
                        int(tdh.group(5)),
                    )
                )
                continue

            td = TURN_DASH_WITNESS_RE.search(line)
            if td:
                scan.turn_dash_rows.append(
                    (
                        int(td.group(2)),
                        td.group(1),
                        int(td.group(3)),
                        int(td.group(4)),
                        int(td.group(5)) if td.group(5) is not None else None,
                        int(td.group(6)),
                        int(td.group(7)),
                    )
                )
                continue

            ss = STICK_SAMPLE_RE.search(line)
            if ss:
                tick = int(ss.group(1))
                player = int(ss.group(2))
                # First-pass only: post-kill PEER resim last-write (0,0) was a
                # RESIM_STICK_FORK false positive (soak 2120480047 @517).
                by_player = scan.stick_sample_by_tick.setdefault(tick, {})
                if player not in by_player:
                    by_player[player] = (int(ss.group(3)), int(ss.group(4)))
                continue

            so = SEAL_OVERRIDE_RE.search(line)
            if so:
                scan.seal_overrides.append(
                    (
                        int(so.group(2)),
                        int(so.group(1)),
                        int(so.group(3)),
                        int(so.group(4)),
                        int(so.group(5)),
                        int(so.group(6)),
                    )
                )
                continue

            sls = SEALED_RESIM_LEDGER_SKIP_RE.search(line)
            if sls:
                scan.sealed_resim_ledger_skips.append(
                    (
                        int(sls.group(2)),
                        int(sls.group(1)),
                        int(sls.group(3)),
                        int(sls.group(4)),
                        int(sls.group(5)),
                        int(sls.group(6)),
                    )
                )
                continue

            if DRIFT_TOKEN in line:
                tm = TICK_RE.search(line)
                tick = int(tm.group(1)) if tm else -1
                td = _tick_for(scan, tick)
                td.lines.append(line)
                parts = PARTITION_RE.findall(line)
                if parts:
                    td.has_report = True
                    for name, snap, live in parts:
                        if snap.lower() != live.lower():
                            td.diverged.add(name)
                res = classify_resolution(line)
                if res:
                    td.resolutions.append(res)
                continue

            rm = RESPAWN_RE.search(line)
            if rm:
                tm = TICK_RE.search(line)
                scan.respawns.append(
                    Respawn(
                        domain=rm.group(1),
                        status=rm.group(2),
                        kind=int(rm.group(3)),
                        tick=int(tm.group(1)) if tm else None,
                        raw=line,
                    )
                )
    return scan


def kind_label(domain: str, kind: int) -> str:
    if domain == "weapon" and kind in WP_KIND_NAMES:
        return f"{kind} (0x{kind:02X} {WP_KIND_NAMES[kind]})"
    return f"{kind} (0x{kind:02X})"


def fmt_pair_session(scans: list[FileScan]) -> list[str]:
    out: list[str] = []
    sids = {s.session_id for s in scans if s.session_id}
    seeds = {s.seed for s in scans if s.seed}
    detail = " ".join(
        f"[{s.label}] session={s.session_id or '?'} seed={s.seed or '?'}" for s in scans
    )
    if len(scans) < 2:
        out.append(f"pair session: single log ({detail})")
    elif not sids:
        out.append(f"pair session: UNKNOWN (no MM_POLL_MATCHED found) {detail}")
    elif len(sids) > 1 or (len(seeds) > 1):
        out.append(f"pair session: !! MIS-PAIRED — different session/seed: {detail}")
    else:
        out.append(f"pair session: OK session={sids.pop()} seed={(seeds or {'?'}).copy().pop()}")
    return out


def _mplanding_by_actor(
    rows: list[tuple[str, int, int, str, str, str]],
) -> dict[tuple[str, int, int], tuple[str, str, str]]:
    """Last write per (domain, player, kind) within one gut."""
    out: dict[tuple[str, int, int], tuple[str, str, str]] = {}
    for domain, player, kind, fflags, tr_x, tr_y in rows:
        out[(domain, player, kind)] = (fflags, tr_x, tr_y)
    return out


def _physics_fork_actor_priority(domain: str, kind: int) -> int:
    """Prefer weapon forks (esp. PK Thunder head) over fighter Hold last-write."""
    if domain == "wp" and kind == WP_KIND_PK_THUNDER_HEAD:
        return 0
    if domain == "wp":
        return 1
    if domain == "ft":
        return 3
    return 2


def detect_physics_fork_onset(
    a: FileScan, b: FileScan
) -> tuple[str, int] | None:
    """First overlapping MpLanding gut where any actor's tr_x/tr_y bits diverge."""
    common = sorted(set(a.mplanding_by_gut) & set(b.mplanding_by_gut))
    if not common:
        return None
    for gut in common:
        by_a = _mplanding_by_actor(a.mplanding_by_gut[gut])
        by_b = _mplanding_by_actor(b.mplanding_by_gut[gut])
        forks: list[tuple] = []
        for key in sorted(
            set(by_a) & set(by_b),
            key=lambda k: (_physics_fork_actor_priority(k[0], k[2]), k),
        ):
            fa, txa, tya = by_a[key]
            fb, txb, tyb = by_b[key]
            fields: list[str] = []
            if txa != txb:
                fields.append("topn_tx")
            if tya != tyb:
                fields.append("topn_ty")
            if not fields:
                continue
            domain, player, kind = key
            forks.append(
                (
                    _physics_fork_actor_priority(domain, kind),
                    domain,
                    player,
                    kind,
                    fields,
                    fa,
                    fb,
                    txa,
                    tya,
                    txb,
                    tyb,
                )
            )
        if not forks:
            # Actors only on one peer at this gut — still a fork if translates differ
            # on a shared last-write "?" legacy row vs tagged.
            continue
        forks.sort(key=lambda t: (t[0], t[2], t[1], t[3]))
        _prio, domain, player, kind, fields, fa, fb, txa, tya, txb, tyb = forks[0]
        fflag_note = (
            f" fflags={fa}" if fa == fb else f" fflags={a.label}:{fa}/{b.label}:{fb}"
        )
        actor_note = f" domain={domain} player={player} kind={kind}"
        if domain == "wp" and kind == WP_KIND_PK_THUNDER_HEAD:
            actor_note += " (PKThunderHead)"
        class_note = ""
        if domain == "wp" and kind == WP_KIND_PK_THUNDER_HEAD:
            class_note = " hold_head→jibaku_risk"
        elif (
            domain == "ft"
            and fields == ["topn_ty"]
            and txa == txb
        ):
            # Soak 128377995: Hold fall-onset Y ladder; SoftLipX CLIFF is red herring.
            class_note = " hold_gravity_risk (ty-only; not SoftLipX cliff X)"
        detail = (
            f"PHYSICS_FORK_ONSET gut={gut}{actor_note} fields={','.join(fields)}"
            f"{fflag_note}{class_note} "
            f"({a.label} tr_x={txa} tr_y={tya} | {b.label} tr_x={txb} tr_y={tyb}) "
            f"— prefer over FRAME_COMMIT onset/seed when FC fires later"
        )
        if len(forks) > 1:
            others = ",".join(f"{d}/{p}/{k}" for _, d, p, k, *_ in forks[1:3])
            detail += f" (+{len(forks) - 1} other actor fork(s): {others})"
        return detail, gut
    return None


def physics_fork_compound_note(scans: list[FileScan], gut: int) -> str | None:
    """When PHYSICS_FORK coincides with GGPO / PEER_SNAPSHOT, tag compound failure."""
    ggpo_ticks: set[int] = set()
    peer_loads: set[int] = set()
    stick_disagree = False
    existing = [s for s in scans if s.exists]
    for s in scans:
        if not s.exists:
            continue
        ggpo_ticks |= s.ggpo_mismatch_ticks
        peer_loads.update(s.peer_snapshot_load_ticks)
    if len(existing) >= 2:
        a, b = existing[0], existing[1]
        for tick in sorted(set(a.stick_sample_by_tick) & set(b.stick_sample_by_tick)):
            if abs(tick - gut) > 4:
                continue
            sa, sb = a.stick_sample_by_tick[tick], b.stick_sample_by_tick[tick]
            for player in set(sa) & set(sb):
                if sa[player] != sb[player]:
                    stick_disagree = True
                    break
            if stick_disagree:
                break
    near_ggpo = sorted(t for t in ggpo_ticks if abs(t - gut) <= 1)
    near_peer = sorted(t for t in peer_loads if abs(t - gut) <= 2)
    ja_vel_fork = _softlip_ja_vel_fork_at_gut(existing, gut)
    if not near_ggpo and not near_peer and not stick_disagree and not ja_vel_fork:
        return None
    bits: list[str] = []
    if near_ggpo:
        bits.append(f"ggpo_mismatch={','.join(str(t) for t in near_ggpo)}")
    if near_peer:
        bits.append(f"peer_snapshot_load={','.join(str(t) for t in near_peer)}")
    if stick_disagree:
        bits.append("resim_stick_fork")
        return (
            f"    compound={'+'.join(bits)} — prefer RESIM_STICK_FORK / seal-ledger "
            f"over SoftLipX CLIFF (ja_vel is consequence of asymmetric stick apply)"
        )
    if ja_vel_fork:
        bits.append("softlip_ja_vel_fork")
        return (
            f"    compound={'+'.join(bits)} — prefer SoftLipPhase ja_vel / "
            f"JA_VEL_WITNESS (sticks match; not seal-ledger)"
        )
    return (
        f"    compound={'+'.join(bits)} — treat soft-lip/TopN as primary when "
        f"fflags=CLIFF; stick GGPO at same tick is concurrent, not the X writer"
    )


def _softlipx_paths(rows: list[tuple[str, int, str, str, int]]) -> set[str]:
    return {p for p, *_ in rows}


def detect_softlipx_asymmetry(
    a: FileScan, b: FileScan, phys_gut: int | None = None
) -> list[str]:
    """SoftLipX path/suppress/sticky disagree — demote floor_edge_skip-only noise."""
    by_a: dict[int, list[tuple[str, int, str, str, int]]] = {}
    by_b: dict[int, list[tuple[str, int, str, str, int]]] = {}
    for gut, path, sup, fflags, sticky, softlip in a.softlipx_rows:
        by_a.setdefault(gut, []).append((path, sup, fflags, sticky, softlip))
    for gut, path, sup, fflags, sticky, softlip in b.softlipx_rows:
        by_b.setdefault(gut, []).append((path, sup, fflags, sticky, softlip))
    out: list[str] = []
    for gut in sorted(set(by_a) | set(by_b)):
        ra, rb = by_a.get(gut, []), by_b.get(gut, [])
        paths = _softlipx_paths(ra) | _softlipx_paths(rb)
        writers = paths - SOFTLIPX_NOISE_ONLY_PATHS
        near_phys = phys_gut is not None and abs(gut - phys_gut) <= 2
        if not ra and rb:
            detail = (
                f"SOFTLIPX_ASYM gut={gut}: {a.label} missing SoftLipX "
                f"({b.label} paths={[r[0] for r in rb]})"
            )
        elif ra and not rb:
            detail = (
                f"SOFTLIPX_ASYM gut={gut}: {b.label} missing SoftLipX "
                f"({a.label} paths={[r[0] for r in ra]})"
            )
        else:
            sa = {(p, s, f, st, sl) for p, s, f, st, sl in ra}
            sb = {(p, s, f, st, sl) for p, s, f, st, sl in rb}
            if sa == sb:
                continue
            detail = (
                f"SOFTLIPX_ASYM gut={gut}: "
                f"{a.label}={sorted(sa)[:4]} | {b.label}={sorted(sb)[:4]}"
            )
        if not writers and not near_phys:
            out.append(
                f"  ~~ {detail} — noise (floor_edge_skip-only; ignore unless "
                f"near PHYSICS_FORK / SoftLipPhase)"
            )
            if len(out) >= 1:
                # One noise sample is enough; keep scanning for a writer hit.
                continue
        else:
            out.insert(0, f"  !! {detail}")
            return out
    return out[:1]


def detect_softlip_phase_fork(a: FileScan, b: FileScan) -> str | None:
    """First SoftLipPhase gut+phase+domain+player where topn_x or vel_x bits diverge."""
    by_a: dict[tuple[int, str, str, int], tuple[int, str, str, str, str]] = {}
    by_b: dict[tuple[int, str, str, int], tuple[int, str, str, str, str]] = {}
    for gut, phase, domain, player, status, topn, vel, ja_vel, ja_drift in a.softlip_phase_rows:
        by_a[(gut, phase, domain, player)] = (status, topn, vel, ja_vel, ja_drift)
    for gut, phase, domain, player, status, topn, vel, ja_vel, ja_drift in b.softlip_phase_rows:
        by_b[(gut, phase, domain, player)] = (status, topn, vel, ja_vel, ja_drift)
    for key in sorted(set(by_a) & set(by_b), key=lambda k: (k[0], k[3], k[2], k[1])):
        sa, ta, va, jaa, jda = by_a[key]
        sb, tb, vb, jab, jdb = by_b[key]
        fields: list[str] = []
        if ta != tb:
            fields.append("topn_x")
        if va != vb:
            fields.append("vel_x")
        if jaa != jab:
            fields.append("ja_vel_x")
        if jda != jdb:
            fields.append("ja_drift")
        if not fields:
            continue
        gut, phase, domain, player = key
        stage = "wpMap" if phase.startswith("wp_") else "SpecialCollisions"
        return (
            f"SOFTLIP_PHASE_FORK gut={gut} phase={phase} domain={domain} "
            f"player={player} fields={','.join(fields)} status={sa} "
            f"({a.label} topn={ta} vel={va} | {b.label} topn={tb} vel={vb}) "
            f"— first asymmetric {stage} stage"
        )
    return None


def detect_softlip_phase_parity(a: FileScan, b: FileScan) -> str | None:
    """WARN when one peer has SoftLipPhase coverage and the other has none."""
    ca, cb = len(a.softlip_phase_rows), len(b.softlip_phase_rows)
    if ca == 0 and cb == 0:
        return None
    if ca > 0 and cb > 0:
        return None
    rich, poor, n = (a, b, ca) if ca > 0 else (b, a, cb)
    return (
        f"SOFTLIP_PHASE_PARITY [{poor.label}]=0 [{rich.label}]={n} — "
        f"rebuild/redeploy SoftLipPhase binary on {poor.label} before trusting "
        f"cross-peer phase diffs (soak 128512323 Linux=0)"
    )


def detect_fighter_light_onset(
    a: FileScan, b: FileScan, phys_gut: int | None = None
) -> str | None:
    """fhash_light DIFF onset — prefer re-fork near PHYSICS_FORK, not early episodes."""
    common = sorted(set(a.fighter_slot_by_tick) & set(b.fighter_slot_by_tick))

    def _diff_at(tick: int) -> tuple[int, str, str, str, str] | None:
        if tick not in a.fighter_slot_by_tick or tick not in b.fighter_slot_by_tick:
            return None
        players = sorted(
            set(a.fighter_slot_by_tick[tick]) & set(b.fighter_slot_by_tick[tick])
        )
        for player in players:
            la, aa = a.fighter_slot_by_tick[tick][player]
            lb, ab = b.fighter_slot_by_tick[tick][player]
            if la == lb:
                continue
            return (player, la, lb, aa, ab)
        return None

    def _matched_at(tick: int) -> bool:
        if tick not in a.fighter_slot_by_tick or tick not in b.fighter_slot_by_tick:
            return False
        players = set(a.fighter_slot_by_tick[tick]) & set(b.fighter_slot_by_tick[tick])
        if not players:
            return False
        for player in players:
            if (
                a.fighter_slot_by_tick[tick][player][0]
                != b.fighter_slot_by_tick[tick][player][0]
            ):
                return False
        return True

    pick_tick: int | None = None
    pick: tuple[int, str, str, str, str] | None = None
    if phys_gut is not None:
        # Re-agreement then first diverge in the fork window (soak 1747311082: 1473).
        for tick in range(max(0, phys_gut - 8), phys_gut + 3):
            diff = _diff_at(tick)
            if diff is None:
                continue
            if tick > 0 and _matched_at(tick - 1):
                pick_tick, pick = tick, diff
                break
            if pick is None:
                pick_tick, pick = tick, diff
    if pick is None:
        for tick in common:
            diff = _diff_at(tick)
            if diff is not None:
                pick_tick, pick = tick, diff
                break
    if pick is None or pick_tick is None:
        return None
    player, la, lb, aa, ab = pick
    anim_note = "anim=OK" if aa == ab else f"anim={a.label}:{aa}/{b.label}:{ab}"
    return (
        f"FIGHTER_LIGHT_ONSET tick={pick_tick} player={player} {anim_note} "
        f"({a.label} light={la} | {b.label} light={lb}) "
        f"— may precede MpLanding PHYSICS_FORK by 1 tick (vel/pos_prev)"
    )


def detect_hold_gravity_resurrect(scans: list[FileScan]) -> list[str]:
    """Flag asymmetric Hold gravity-delay resurrect; demote peer-matched noise."""
    out: list[str] = []
    existing = [s for s in scans if s.exists]
    if not existing:
        return out
    # key -> set of labels that saw the same resurrect
    by_key: dict[tuple[int, int, int, int, int], set[str]] = {}
    for s in existing:
        for tick, player, was, now, expected in s.hold_gravity_resurrect:
            by_key.setdefault((tick, player, was, now, expected), set()).add(s.label)
    for (tick, player, was, now, expected), labels in sorted(by_key.items()):
        if len(labels) >= 2:
            out.append(
                f"  ~~ HOLD_GRAVITY_RESURRECT tick={tick} player={player} "
                f"was={was}→now={now} expected={expected} "
                f"— symmetric on {','.join(sorted(labels))} (noise; not fall-onset kill)"
            )
        else:
            label = next(iter(labels))
            out.append(
                f"  !! [{label}] HOLD_GRAVITY_RESURRECT tick={tick} player={player} "
                f"was={was}→now={now} expected={expected} "
                f"— asymmetric mid-Hold sanitize raise (fall-onset +1 tick risk)"
            )
        if len(out) >= 8:
            break
    blocked_keys: dict[tuple[int, int, int, int], set[str]] = {}
    for s in existing:
        for tick, player, live, expected in s.hold_gravity_resurrect_blocked:
            blocked_keys.setdefault((tick, player, live, expected), set()).add(s.label)
    for (tick, player, live, expected), labels in sorted(blocked_keys.items())[:4]:
        out.append(
            f"  ~~ HOLD_GRAVITY_RESURRECT_BLOCKED tick={tick} player={player} "
            f"live={live} expected={expected} peers={','.join(sorted(labels))}"
        )
    return out


def detect_turn_dash_lr_dash_fork(a: FileScan, b: FileScan) -> str | None:
    """First TURN_DASH_WITNESS tick where lr_dash or did_dash disagree (first pass wins)."""
    def by_key(rows):
        out: dict[tuple[int, str, int], tuple[int, int]] = {}
        for tick, phase, player, lr_dash, _entry, _lr_turn, did_dash in rows:
            if phase == "harden_lr_dash":
                continue
            key = (tick, phase, player)
            # Keep first chronological pass — resim may rewrite matching later.
            if key not in out:
                out[key] = (lr_dash, did_dash)
        return out

    ka, kb = by_key(a.turn_dash_rows), by_key(b.turn_dash_rows)
    for key in sorted(set(ka) & set(kb)):
        if ka[key] != kb[key]:
            tick, phase, player = key
            lda, dda = ka[key]
            ldb, ddb = kb[key]
            return (
                f"TURN_DASH_LR_DASH_FORK tick={tick} phase={phase} player={player} "
                f"({a.label} lr_dash={lda} did_dash={dda} | "
                f"{b.label} lr_dash={ldb} did_dash={ddb}) "
                f"— InvertLR lr_dash stomp → Turn vs Dash (not SoftLipX / Hold gravity)"
            )
    return None


def _status_fork_recovered(
    a: FileScan, b: FileScan, tick: int, player: int, look: int = 16
) -> bool:
    """True when both peers re-agree on status_id within lookforward (GGPO noise)."""
    for t in range(tick + 1, tick + look + 1):
        sa = a.fighter_status_by_tick.get(t, {}).get(player)
        sb = b.fighter_status_by_tick.get(t, {}).get(player)
        if sa is None or sb is None:
            continue
        if sa == sb:
            return True
    return False


def detect_status_fork_onset(
    a: FileScan, b: FileScan, fc_tick: int | None = None
) -> tuple[str, int] | None:
    """Status_id cross-peer diverge; prefer re-fork near FC; demote GGPO-recovered noise."""
    common = sorted(set(a.fighter_status_by_tick) & set(b.fighter_status_by_tick))
    forks: list[tuple[str, int]] = []
    recovered: list[tuple[str, int]] = []
    for tick in common:
        sa, sb = a.fighter_status_by_tick[tick], b.fighter_status_by_tick[tick]
        for player in sorted(set(sa) & set(sb)):
            if sa[player] != sb[player]:
                detail = (
                    f"STATUS_FORK_ONSET tick={tick} player={player} "
                    f"({a.label} status={sa[player]} | {b.label} status={sb[player]}) "
                    f"— prefer over FRAME_COMMIT when locomotion/grab cascade follows"
                )
                if _status_fork_recovered(a, b, tick, player):
                    recovered.append(
                        (
                            f"STATUS_FORK_RECOVERED tick={tick} player={player} "
                            f"({a.label} status={sa[player]} | {b.label} status={sb[player]}) "
                            f"— re-agreed within lookforward (jump-button GGPO noise)",
                            tick,
                        )
                    )
                else:
                    forks.append((detail, tick))
                break
    if not forks:
        if recovered:
            # Non-kill noise when every STATUS_FORK re-agreed (prefix ~~).
            return (f"~~ {recovered[0][0]}", recovered[0][1])
        return None
    if fc_tick is not None:
        near = [f for f in forks if 0 <= fc_tick - f[1] <= 64]
        if near:
            return near[-1]
    return forks[0]


def _softlip_ja_vel_fork_at_gut(scans: list[FileScan], gut: int) -> bool:
    """True when SoftLipPhase peers disagree on ja_vel_x / ja_drift near gut."""
    existing = [s for s in scans if s.exists]
    if len(existing) < 2:
        return False
    a, b = existing[0], existing[1]
    by_a = {
        (g, phase, domain, player): (ja_vel, ja_drift)
        for g, phase, domain, player, _st, _t, _v, ja_vel, ja_drift in a.softlip_phase_rows
        if abs(g - gut) <= 1
    }
    by_b = {
        (g, phase, domain, player): (ja_vel, ja_drift)
        for g, phase, domain, player, _st, _t, _v, ja_vel, ja_drift in b.softlip_phase_rows
        if abs(g - gut) <= 1
    }
    for key in set(by_a) & set(by_b):
        if by_a[key] != by_b[key]:
            return True
    return False


def detect_earliest_fork_vs_fc(
    scans: list[FileScan],
    phys_gut: int | None,
    status_tick: int | None = None,
    turn_dash_tick: int | None = None,
) -> str | None:
    """Surface earliest physics/status/FC/PEER seed so late FC recovery does not hide it."""
    existing = [s for s in scans if s.exists]
    if len(existing) < 2:
        return None
    fc_ticks = sorted(
        {fc.tick for s in existing for fc in s.fc_diverges if fc.diverged}
    )
    peer_ticks = sorted(
        {t for s in existing for t in s.peer_snapshot_load_ticks}
    )
    candidates: list[tuple[int, str]] = []
    if phys_gut is not None:
        candidates.append((phys_gut, "PHYSICS_FORK"))
    if status_tick is not None:
        candidates.append((status_tick, "STATUS_FORK"))
    if turn_dash_tick is not None:
        candidates.append((turn_dash_tick, "TURN_DASH_FORK"))
    if fc_ticks:
        candidates.append((fc_ticks[0], "FRAME_COMMIT"))
    if peer_ticks:
        candidates.append((peer_ticks[0], "PEER_SNAPSHOT"))
    if len(candidates) < 2:
        return None
    candidates.sort(key=lambda t: t[0])
    earliest_tick, earliest_kind = candidates[0]
    later = [f"{k}@{t}" for t, k in candidates[1:]]
    note = (
        f"EARLIEST_FORK {earliest_kind}@{earliest_tick} before {', '.join(later)}"
    )
    if earliest_kind in ("STATUS_FORK", "TURN_DASH_FORK"):
        note += " — locomotion/Turn-Dash seed (not SoftLipX / Hold gravity)"
    elif earliest_kind == "PHYSICS_FORK" and fc_ticks and phys_gut is not None:
        if phys_gut + 2 < fc_ticks[0]:
            note += f" (physics precedes FC by {fc_ticks[0] - phys_gut} tick(s))"
    return note


def detect_resim_stick_fork(
    a: FileScan, b: FileScan, phys_gut: int | None
) -> list[str]:
    """Flag cross-peer STICK_SAMPLE disagree near GGPO / PHYSICS_FORK (seal vs ledger)."""
    out: list[str] = []
    anchors = set(a.ggpo_mismatch_ticks) | set(b.ggpo_mismatch_ticks)
    if phys_gut is not None:
        anchors.add(phys_gut)
    if not anchors:
        return out
    common_ticks = sorted(
        set(a.stick_sample_by_tick) & set(b.stick_sample_by_tick)
    )
    for tick in common_ticks:
        if not any(abs(tick - anc) <= 4 for anc in anchors):
            continue
        sa, sb = a.stick_sample_by_tick[tick], b.stick_sample_by_tick[tick]
        for player in sorted(set(sa) & set(sb)):
            if sa[player] != sb[player]:
                out.append(
                    f"  !! RESIM_STICK_FORK tick={tick} player={player} "
                    f"({a.label} sx={sa[player][0]} sy={sa[player][1]} | "
                    f"{b.label} sx={sb[player][0]} sy={sb[player][1]}) "
                    f"— seal/ledger stick apply disagree (not SoftLipX ja_vel)"
                )
    return out


def detect_seal_ledger_stomp(scans: list[FileScan]) -> list[str]:
    """Surface SEAL_OVERRIDE / SEALED_RESIM_LEDGER_SKIP near fail windows."""
    out: list[str] = []
    for s in scans:
        if not s.exists:
            continue
        for tick, player, ox, oy, sx, sy in s.seal_overrides[:6]:
            out.append(
                f"  !! [{s.label}] SEAL_LEDGER_STOMP tick={tick} player={player} "
                f"confirmed=({ox},{oy})→sealed=({sx},{sy}) "
                f"(REMOTE_PUBLISH_SEAL_OVERRIDE; reseal after resim)"
            )
        for tick, player, sx, sy, lx, ly in s.sealed_resim_ledger_skips[:6]:
            out.append(
                f"  ~~ [{s.label}] SEALED_RESIM_LEDGER_SKIP tick={tick} player={player} "
                f"sealed=({sx},{sy}) ledger=({lx},{ly}) "
                f"(publish kept seal during resim; good if peers agree)"
            )
    return out


def detect_sim_state_cadence_gap(scans: list[FileScan]) -> list[str]:
    """WARN lines when one peer lacks sim_state_tick / fighter_slot_hash coverage."""
    out: list[str] = []
    existing = [s for s in scans if s.exists]
    if len(existing) < 2:
        return out
    counts = [(s.label, s.sim_state_tick_count, s.fighter_slot_hash_count) for s in existing]
    max_sim = max(c[1] for c in counts)
    max_slot = max(c[2] for c in counts)
    for label, sim_n, slot_n in counts:
        if max_sim >= 32 and sim_n * 10 < max_sim:
            out.append(
                f"  !! [{label}] SIM_STATE cadence gap: sim_state_tick={sim_n} "
                f"(peer max={max_sim}) — enable SSB64_NETPLAY_SIM_STATE_TICK_INTERVAL=1 "
                f"on BOTH peers (Android debug.env)"
            )
        if max_slot >= 32 and slot_n * 10 < max_slot:
            out.append(
                f"  !! [{label}] FIGHTER_SLOT_HASH cadence gap: count={slot_n} "
                f"(peer max={max_slot}) — enable SSB64_NETPLAY_FIGHTER_SLOT_HASH_LOG=1 "
                f"+ SIM_STATE_TICK_INTERVAL=1 on BOTH peers"
            )
        if max_sim >= 32 and sim_n == 0:
            out.append(
                f"  !! [{label}] no sim_state_tick rows — pair hash diff / early fork "
                f"attribution is blind"
            )
    # SoftLipX / MpLanding coverage hints when figh/physics failure present.
    need_softlip = False
    for s in existing:
        if any(fc.diverged & {"figh"} for fc in s.fc_diverges) or s.peer_snapshot_load_ticks:
            need_softlip = True
            if not s.mplanding_by_gut:
                out.append(
                    f"  !! [{s.label}] figh/PEER diverge without MpLanding rows — "
                    f"enable SSB64_NETPLAY_LANDING_BRANCH_DIAG=1"
                )
            if not s.softlipx_rows:
                out.append(
                    f"  !! [{s.label}] figh/PEER diverge without SoftLipX rows — "
                    f"enable SSB64_NETPLAY_SOFTLIP_X_DIAG=1"
                )
    if need_softlip and all(not s.softlipx_rows for s in existing if s.exists):
        out.append(
            "  !! SoftLipX missing on all peers — cliff soak needs "
            "SSB64_NETPLAY_SOFTLIP_X_DIAG=1 (see netplay-cliff-softlip-soak.env.example)"
        )
    return out


def detect_peer_map_phase_fork(
    a: FileScan, b: FileScan, min_span: int = 8
) -> tuple[str, int, int] | None:
    """Detect sustained +1 map phase skew between two peers.

    Returns (detail, first_tick, span) or None. A fork is when for >= min_span
    consecutive overlapping ticks, a[t] matches b[t-1] (or b[t+1]) on map_hash
    while a[t] != b[t]. Prefer map_hash_save; fall back to pupupu blink/wind_wait.
    """
    common = sorted(set(a.map_hash_by_tick) & set(b.map_hash_by_tick))
    if len(common) >= min_span:
        # Scan for longest run of a[t]==b[t-1] with a[t]!=b[t].
        best: tuple[int, int] | None = None  # start, span
        run_start: int | None = None
        run_span = 0
        for t in common:
            ha, hb = a.map_hash_by_tick[t], b.map_hash_by_tick[t]
            if ha == hb:
                run_start, run_span = None, 0
                continue
            hb_prev = b.map_hash_by_tick.get(t - 1)
            ha_prev = a.map_hash_by_tick.get(t - 1)
            phase = (hb_prev is not None and ha == hb_prev) or (
                ha_prev is not None and hb == ha_prev
            )
            if phase:
                if run_start is None:
                    run_start = t
                    run_span = 1
                else:
                    run_span += 1
                if best is None or run_span > best[1]:
                    best = (run_start, run_span)
            else:
                run_start, run_span = None, 0
        if best is not None and best[1] >= min_span:
            # Tie to nearest prior POST_RESIM_LIVE when available.
            post_ticks = [sim for sim, _ in (a.post_resim_live + b.post_resim_live)]
            near = max((p for p in post_ticks if p <= best[0]), default=None)
            detail = (
                f"map_hash +1 phase skew from tick {best[0]} span={best[1]}"
                + (f" after POST_RESIM_LIVE sim={near}" if near is not None else "")
            )
            return detail, best[0], best[1]

    # pupupu blink/wind_wait fallback (same phase-skew pattern).
    common_p = sorted(set(a.pupupu_by_tick) & set(b.pupupu_by_tick))
    if len(common_p) < min_span:
        return None
    best = None
    run_start = None
    run_span = 0
    for t in common_p:
        pa, pb = a.pupupu_by_tick[t], b.pupupu_by_tick[t]
        if pa == pb:
            run_start, run_span = None, 0
            continue
        pb_prev = b.pupupu_by_tick.get(t - 1)
        pa_prev = a.pupupu_by_tick.get(t - 1)
        phase = (pb_prev is not None and pa == pb_prev) or (
            pa_prev is not None and pb == pa_prev
        )
        if phase:
            if run_start is None:
                run_start = t
                run_span = 1
            else:
                run_span += 1
            if best is None or run_span > best[1]:
                best = (run_start, run_span)
        else:
            run_start, run_span = None, 0
    if best is not None and best[1] >= min_span:
        detail = (
            f"pupupu blink/wind_wait +1 phase skew from tick {best[0]} span={best[1]}"
        )
        return detail, best[0], best[1]
    return None


SEV_NAME = {SEV_PASS: "PASS", SEV_WARN: "WARN", SEV_FAIL: "FAIL"}


def report(scans: list[FileScan], strict: bool, show_lines: bool, quiet: bool) -> int:
    overall = SEV_PASS
    body: list[str] = []

    body.append("=== netplay rollback drift scan ===")
    body.extend("  " + l for l in fmt_pair_session(scans))
    body.append("")

    for s in scans:
        if not s.exists:
            body.append(f"[{s.label}] {s.path}")
            body.append("  !! file not found")
            body.append("")
            overall = max(overall, SEV_FAIL)
            continue

        drift_ticks = sorted(t for t in s.ticks if t >= 0) + (
            [-1] if -1 in s.ticks else []
        )
        st_fail_set = set(s.synctest_fail)
        n_lines = sum(len(s.ticks[t].lines) for t in s.ticks)
        body.append(f"[{s.label}] {s.path}")
        body.append(
            f"  LOAD_HASH_DRIFT: {n_lines} line(s) across {len(s.ticks)} tick(s)"
        )
        for t in drift_ticks:
            td = s.ticks[t]
            sev = td.severity()
            # Recovered drift is WARN by default; --strict elevates it to FAIL.
            eff_sev = SEV_FAIL if (strict and sev == SEV_WARN) else sev
            overall = max(overall, eff_sev)
            tick_s = f"tick {t}" if t >= 0 else "tick ?"
            div = ",".join(sorted(td.diverged)) if td.diverged else "-"
            probe = " [synctest probe]" if t in st_fail_set else ""
            body.append(
                f"    [{SEV_NAME[sev]}] {tick_s}: diverged={div} -> {td.resolution_text()}{probe}"
            )
            if show_lines:
                for ln in td.lines:
                    body.append(f"        | {ln}")

        soft_ticks = s.soft_continue_ticks()
        if soft_ticks:
            body.append(
                f"  soft-continued drift: {len(soft_ticks)} tick(s) "
                f"(tolerated despite SOFT=0 -> >=8-rollback auto-soften): "
                + ", ".join(str(t) for t in soft_ticks[:20])
                + (" ..." if len(soft_ticks) > 20 else "")
            )

        if s.respawns:
            overall = max(overall, SEV_FAIL)
            # Aggregate by (domain,status,kind).
            agg: dict[tuple[str, str, int], int] = {}
            for r in s.respawns:
                key = (r.domain, r.status, r.kind)
                agg[key] = agg.get(key, 0) + 1
            body.append(f"  respawn gaps: {len(s.respawns)} line(s)")
            for (domain, status, kind), count in sorted(agg.items()):
                body.append(
                    f"    [FAIL] {domain} respawn {status} kind={kind_label(domain, kind)} (x{count})"
                )
            if show_lines:
                for r in s.respawns:
                    body.append(f"        | {r.raw}")
        else:
            body.append("  respawn gaps: none")

        total_skip = sum(s.synctest_skip.values())
        if s.synctest_ok or s.synctest_fail or s.synctest_skip:
            overall = max(overall, SEV_FAIL if s.synctest_fail else SEV_PASS)
            body.append(
                f"  synctest: {s.synctest_ok} OK, {len(s.synctest_fail)} FAIL, "
                f"{total_skip} skipped"
            )
            for t in sorted(s.synctest_fail):
                tick_s = f"tick {t}" if t >= 0 else "tick ?"
                body.append(f"    [FAIL] SYNCTEST_FAIL {tick_s}")
            for reason, count in sorted(
                s.synctest_skip.items(), key=lambda kv: (-kv[1], kv[0])
            ):
                body.append(f"    [skip] {reason} (x{count})")
        else:
            body.append(
                "  synctest: no probes seen "
                "(SSB64_NETPLAY_ROLLBACK_SYNCTEST not set, or never reached a probe tick)"
            )

        if s.ggpo_queued or s.ggpo_skipped_micro:
            class_bits = ", ".join(
                f"{k}={v}" for k, v in sorted(s.ggpo_by_class.items()) if v
            )
            body.append(
                f"  ggpo: queued={s.ggpo_queued}"
                + (f" ({class_bits})" if class_bits else "")
                + f" skipped_micro={s.ggpo_skipped_micro}"
            )

        # Cross-peer frame-commit validation (the authoritative inter-peer check that the
        # peer-local SYNCTEST round trip cannot see). Any real partition delta is a desync.
        real_fc = [fc for fc in s.fc_diverges if fc.diverged]
        if real_fc:
            overall = max(overall, SEV_FAIL)
            body.append(f"  frame-commit cross-peer diverge: {len(real_fc)} line(s)")
            for fc in real_fc[:20]:
                fields = ",".join(sorted(fc.diverged))
                if fc.inputs_match is True:
                    note = (
                        "inputs=MATCH (genuine cross-ISA determinism failure) "
                        "bucket=REPLAY_DETERMINISM"
                    )
                elif fc.inputs_match is False:
                    note = "inputs=DIFFER (input/pairing skew) bucket=PROTOCOL"
                else:
                    note = "inputs=?"
                body.append(f"    [FAIL] tick {fc.tick}: diverged={fields} {note}")
                if show_lines:
                    body.append(f"        | {fc.raw}")
            if len(real_fc) > 20:
                body.append(f"    ... (+{len(real_fc) - 20} more)")
        elif s.fc_diverges:
            body.append(
                f"  frame-commit cross-peer diverge: {len(s.fc_diverges)} line(s) "
                "(no field delta parsed)"
            )

        if s.resync_storms:
            overall = max(overall, SEV_FAIL)
            body.append(f"  baseline resync storm (recovery ABORTED): {len(s.resync_storms)}")
            for load_tick, sim in s.resync_storms[:10]:
                body.append(f"    [FAIL] load_tick={load_tick} sim={sim}")

        if s.session_stops:
            body.append(f"  session stop / VS_SESSION_END: {s.session_stops}")

        if (
            s.mplanding_by_gut
            or s.softlipx_rows
            or s.softlip_phase_rows
            or s.sim_state_tick_count
        ):
            body.append(
                f"  physics diag: MpLanding guts={len(s.mplanding_by_gut)} "
                f"SoftLipX={len(s.softlipx_rows)} "
                f"SoftLipPhase={len(s.softlip_phase_rows)} "
                f"sim_state_tick={s.sim_state_tick_count} "
                f"fighter_slot_hash={s.fighter_slot_hash_count}"
            )
        for hint in s.fc_seed_hints[:3]:
            v, onset, agreed, recovery, physics = hint
            body.append(
                f"  FC seed hint: validation={v} onset={onset} last_agreed={agreed} "
                f"recovery_seed={recovery} physics_seed={physics} "
                f"(onset may predate physics fork — prefer MpLanding)"
            )

        body.append("")

    # Cross-peer view: ticks where both peers report drift.
    if len([s for s in scans if s.exists]) >= 2:
        per_label = {s.label: set(t for t in s.ticks if t >= 0) for s in scans if s.exists}
        common = set.intersection(*per_label.values()) if per_label else set()
        body.append("=== combined ===")
        if common:
            body.append(
                "  drift ticks on BOTH peers: " + ", ".join(str(t) for t in sorted(common))
            )
        else:
            body.append("  drift ticks on both peers: none")

        # Frame-commit divergences are inherently cross-peer; surface any tick where either
        # peer logged a real partition delta so it cannot be lost in the per-file noise.
        fc_per_label = {
            s.label: sorted({fc.tick for fc in s.fc_diverges if fc.diverged})
            for s in scans
            if s.exists
        }
        if any(fc_per_label.values()):
            for label, ticks in fc_per_label.items():
                if ticks:
                    body.append(
                        f"  !! [{label}] FRAME_COMMIT cross-peer diverge ticks: "
                        + ", ".join(str(t) for t in ticks)
                    )
        if any(s.resync_storms for s in scans if s.exists):
            for s in scans:
                if s.exists and s.resync_storms:
                    body.append(
                        f"  !! [{s.label}] recovery ABORTED (baseline resync storm) x{len(s.resync_storms)}"
                    )
        # Peer-only map phase fork: local SYNCTEST/LOAD_HASH can PASS while Whispy is +1 skewed.
        existing = [s for s in scans if s.exists]
        if len(existing) >= 2:
            fork = detect_peer_map_phase_fork(existing[0], existing[1])
            if fork is not None:
                detail, first_tick, span = fork
                overall = max(overall, SEV_FAIL)
                body.append(
                    f"  !! PEER_MAP_PHASE_FORK [{existing[0].label}/{existing[1].label}]: "
                    f"{detail}"
                )
                body.append(
                    f"    first_skew_tick={first_tick} span={span} "
                    "(local synctest may still PASS — map mislabeled after post-resim save)"
                )
            phys = detect_physics_fork_onset(existing[0], existing[1])
            phys_gut: int | None = None
            if phys is not None:
                detail, gut = phys
                phys_gut = gut
                overall = max(overall, SEV_FAIL)
                body.append(f"  !! {detail}")
                fc_ticks = sorted(
                    {
                        fc.tick
                        for s in existing
                        for fc in s.fc_diverges
                        if fc.diverged
                    }
                )
                if fc_ticks and gut + 2 < min(fc_ticks):
                    body.append(
                        f"    physics fork precedes FRAME_COMMIT by "
                        f"{min(fc_ticks) - gut} tick(s) (FC first={min(fc_ticks)})"
                    )
                compound = physics_fork_compound_note(existing, gut)
                if compound is not None:
                    body.append(compound)
            fc_first = min(
                (
                    fc.tick
                    for s in existing
                    for fc in s.fc_diverges
                    if fc.diverged
                ),
                default=None,
            )
            status_fork = detect_status_fork_onset(
                existing[0], existing[1], fc_tick=fc_first
            )
            status_tick: int | None = None
            if status_fork is not None:
                status_detail, status_tick = status_fork
                if status_detail.startswith("~~ "):
                    body.append(f"  {status_detail}")
                    status_tick = None
                else:
                    overall = max(overall, SEV_FAIL)
                    body.append(f"  !! {status_detail}")
            turn_dash = detect_turn_dash_lr_dash_fork(existing[0], existing[1])
            turn_dash_tick: int | None = None
            if turn_dash is not None:
                overall = max(overall, SEV_FAIL)
                body.append(f"  !! {turn_dash}")
                m = re.search(r"tick=(\d+)", turn_dash)
                if m is not None:
                    turn_dash_tick = int(m.group(1))
            earliest = detect_earliest_fork_vs_fc(
                existing,
                phys_gut,
                status_tick=status_tick,
                turn_dash_tick=turn_dash_tick,
            )
            if earliest is not None:
                body.append(f"  !! {earliest}")
            for grav_line in detect_hold_gravity_resurrect(existing):
                if grav_line.startswith("  !!"):
                    overall = max(overall, SEV_FAIL)
                body.append(grav_line)
            light = detect_fighter_light_onset(
                existing[0], existing[1], phys_gut=phys_gut
            )
            if light is not None:
                body.append(f"  !! {light}")
                if phys_gut is not None:
                    m = re.search(r"tick=(\d+)", light)
                    if m is not None:
                        onset = int(m.group(1))
                        if 0 < phys_gut - onset <= 8:
                            body.append(
                                f"    light onset precedes PHYSICS_FORK by "
                                f"{phys_gut - onset} tick(s) — inspect vel/ja_* "
                                f"via SoftLipPhase post_phys"
                            )
            phase = detect_softlip_phase_fork(existing[0], existing[1])
            if phase is not None:
                overall = max(overall, SEV_FAIL)
                body.append(f"  !! {phase}")
            for stick_line in detect_resim_stick_fork(
                existing[0], existing[1], phys_gut=phys_gut
            ):
                overall = max(overall, SEV_FAIL)
                body.append(stick_line)
            for seal_line in detect_seal_ledger_stomp(existing):
                if seal_line.startswith("  !!"):
                    overall = max(overall, SEV_FAIL)
                body.append(seal_line)
            parity = detect_softlip_phase_parity(existing[0], existing[1])
            if parity is not None:
                overall = max(overall, SEV_WARN)
                body.append(f"  ~~ {parity}")
            for soft_line in detect_softlipx_asymmetry(
                existing[0], existing[1], phys_gut=phys_gut
            ):
                # ty-only Hold gravity forks: SoftLipX CLIFF is usually noise.
                if (
                    phys_gut is not None
                    and "hold_gravity_risk" in (phys[0] if phys else "")
                    and soft_line.startswith("  !!")
                ):
                    body.append(
                        soft_line.replace(
                            "  !!",
                            "  ~~",
                            1,
                        )
                        + " — demoted (PHYSICS_FORK is ty-only hold_gravity_risk)"
                    )
                else:
                    body.append(soft_line)
        body.extend(detect_sim_state_cadence_gap(scans))
        for s in scans:
            if not s.exists:
                continue
            if s.synctest_ok == 0 and not s.synctest_fail:
                body.append(
                    f"  !! [{s.label}] synctest produced no OK/FAIL probes — "
                    "coverage unverified (enable SSB64_NETPLAY_ROLLBACK_SYNCTEST=1)"
                )
        body.append("")

    body.append(f"RESULT: {SEV_NAME[overall]}")

    if quiet:
        print(f"RESULT: {SEV_NAME[overall]}")
    else:
        print("\n".join(body))
    return overall


def build_inputs(args) -> list[tuple[str, str]]:
    inputs: list[tuple[str, str]] = []
    for name, path in args.label or []:
        inputs.append((name, path))
    pos = args.paths or []
    if pos and not inputs:
        if len(pos) == 2:
            for name, path in zip(("host", "guest"), pos):
                inputs.append((name, path))
        else:
            for path in pos:
                inputs.append((Path(path).stem, path))
    elif pos:
        for path in pos:
            inputs.append((Path(path).stem, path))
    return inputs


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(
        description="Scan a host/guest netplay log pair for LOAD_HASH_DRIFT / respawn-unsupported.",
    )
    ap.add_argument(
        "--label", nargs=2, action="append", metavar=("NAME", "PATH"),
        help="Labeled input log (repeatable).",
    )
    ap.add_argument("paths", nargs="*", help="Positional log paths (auto-labeled).")
    ap.add_argument("--strict", action="store_true",
                    help="Treat recovered drift as failure too.")
    ap.add_argument("--show-lines", action="store_true",
                    help="Echo matched raw log lines.")
    ap.add_argument("-q", "--quiet", action="store_true",
                    help="Print only the final RESULT line.")
    args = ap.parse_args(argv)

    inputs = build_inputs(args)
    if not inputs:
        ap.error("no input logs (use --label NAME PATH or positional paths)")

    scans = [scan_file(name, path) for name, path in inputs]
    return report(scans, args.strict, args.show_lines, args.quiet)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
