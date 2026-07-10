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
  SSB64_NETPLAY_GOBJ_LINK_AUDIT=1

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

        # Cross-peer frame-commit validation (the authoritative inter-peer check that the
        # peer-local SYNCTEST round trip cannot see). Any real partition delta is a desync.
        real_fc = [fc for fc in s.fc_diverges if fc.diverged]
        if real_fc:
            overall = max(overall, SEV_FAIL)
            body.append(f"  frame-commit cross-peer diverge: {len(real_fc)} line(s)")
            for fc in real_fc[:20]:
                fields = ",".join(sorted(fc.diverged))
                if fc.inputs_match is True:
                    note = "inputs=MATCH (genuine cross-ISA determinism failure)"
                elif fc.inputs_match is False:
                    note = "inputs=DIFFER (input/pairing skew)"
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
