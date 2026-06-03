#!/usr/bin/env python3
"""
Merge and filter large SSB64 netplay debug logs for paired host/guest triage.

Usage:
  ./scripts/netplay-trim-logs.py --label host host.log --label guest guest.log \\
    -o netplay-trimmed.log --tick-min 100 --tick-max 250 --diff-ticks

Options:
  --label NAME PATH   Input log (repeatable)
  -o PATH             Output file (default: stdout)
  --tick-min N        Keep lines with tick= only when N <= tick <= --tick-max
  --tick-max N
  --include REGEX     Extra keep pattern (repeatable; rarely needed — defaults use subsystem prefixes)
  --exclude REGEX     Extra drop pattern (repeatable)
  --no-default-filters  Do not apply built-in include/exclude lists
  --keep-yoster-cloud-fighters  Keep yoster_cloud_fighter rows (excluded by default)
  --dedupe-effect-save  Collapse consecutive identical effect save lines (default on)
  --no-dedupe-snapshots Disable snapshot save dedupe (item/weapon/effect)
  --no-collapse-yamabuki-gate  Keep every yamabuki_gate / yamabuki_gate_node row (default collapses identical state)
  --no-collapse-yamabuki-hitokage  Keep every yamabuki_hitokage row (default collapses identical flame state)
  --keep-gate-anim-trace  Keep SSB64: gcPlayDObjAnimJoint TraI lines (gate mesh apply probe; capped in-game)
  --collapse-r-stall    Collapse repeated path=R frame_commit_diag and frozen sim_state_tick (default on)
  --no-collapse-r-stall Disable R-stall collapse
  --summary-only      Print summary block only (no merged body)
  --diff-ticks        Report first sim_state_tick field mismatch between first two inputs
  --diff-death-rebirth  Report death/rebirth sim + gate diag mismatches (implies --diff-ticks extras)
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

# Broad netplay subsystem prefixes — keep ENV-gated diag without per-var patterns.
DEFAULT_INCLUDE = [
    r"debug\.env",
    r"SSB64 NetPeer:",
    r"SSB64 NetInput:",
    r"SSB64 NetPlay:",
    r"SSB64 NetSession:",
    r"SSB64 NetSync:",
    # Throw-window item snapshots (synctest skip during fighter/item throw); always keep for cross-ISA bisect.
    r"SSB64 NetSync: item_throw_window ",
    r"SSB64 NetSync: item_hold_coupling ",
    # Yoshi's Island cloud lifecycle / stand-detection (SSB64_NETPLAY_YOSTER_CLOUD_DIAG=1).
    r"SSB64 NetSync: yoster_cloud ",
    # Saffron City (Yamabuki) tower door + rooftop Pokémon (SSB64_NETPLAY_YAMABUKI_GATE_DIAG=1).
    r"SSB64 NetSync: yamabuki_gate ",
    # Per-node gate DObj tree dump (which node carries the DL/material and whether it animates).
    r"SSB64 NetSync: yamabuki_gate_node ",
    r"SSB64 NetSync: yamabuki_hitokage ",
    # Hyrule Castle tornado / twister rollback diagnostics.
    r"SSB64 NetRbSnapshot: hyrule_twister",
    r"SSB64 NetSync: hyrule_twister",
    # Sector Z Arwing snapshot / restore diagnostics.
    r"SSB64 NetRbSnapshot: sector_arwing",
    # Post-apply GObj link census (item/weapon/effect counts after snapshot load).
    r"SSB64 NetRbSnapshot: gobj_link_audit ",
    r"SSB64 NetRbSnapshot:",
    r"SSB64 NetRollback:",
    # Ness PK Thunder throw / jibaku timing (SSB64_NETPLAY_NESS_PKTHUNDER_GATE_DIAG=1).
    r"SSB64 Netplay: NESS_PKTHUNDER_GATE ",
    # FTStatusVars overlay witness (SSB64_NETPLAY_STATUSVARS_WITNESS=1).
    r"SSB64 NetStatusVars:",
    r"SSB64 Netplay:",
    r"SSB64 Automatch:",
    r"SSB64 ICE:",
    r"SSB64 Matchmaking:",
    r"SSB64 DESYNC",
    r"SSB64 FRAME COMMIT REPORT",
    r"returning to character select",
]

# High-volume lines that rarely help hash-drift / rollback triage.
DEFAULT_EXCLUDE = [
    r"osSpTaskStartGo",
    r"Thread5",
    r"syTaskmanLoadScene",
    r"mnStartup",
    r"watchdog",
    r"RelocFile",
    r"GBI",
    r"game coroutine",
    r"debug session start",
    r"Matchmaking: GET https://.*/v1/match/",
    r"Matchmaking: POST https://.*/v1/match/.*/ice -> HTTP 204",
    r"Matchmaking: heartbeat OK",
    r"Matchmaking: still queued",
    # Per-tick spam (enable with --keep-noisy-traces)
    r"SSB64 NetSync: joint_translate ",
    r"SSB64 NetSync: ft_phase ",
    r"SSB64 NetSync: fighter_slot_hash ",
    r"SSB64 NetSync: validation_dual_hash ",
    r"SSB64 NetSync: role=client ",
    r"SSB64 NetSync: role=host ",
    r"SSB64 NetSync: tick_diag ",
    r"SSB64 NetSync: pub_vs_remote_summary ",
    r"SSB64 NetPeer: cross_os_pacing ",
    r"SSB64 NetPeer: phase_lock_commit ",
    r"SSB64 NetPeer: INPUT send ",
    r"SSB64 NetPeer: INPUT recv ",
    r"SSB64 NetInput: frame_commit_diag tick=\d+ path=P ",
    # Per-tick fighter floor-line rows when YOSTER_CLOUD_DIAG_FIGHTERS=1 (enable with --keep-yoster-cloud-fighters).
    r"SSB64 NetSync: yoster_cloud_fighter ",
]

TICK_RE = re.compile(r"\btick=(\d+)\b")
SIM_STATE_TICK_RE = re.compile(r"SSB64 NetSync: sim_state_tick tick=(\d+)\b")
FIELD_RE = re.compile(r"\b([a-zA-Z_][a-zA-Z0-9_]*)=([^\s]+)")
EFFECT_SAVE_RE = re.compile(
    r"SSB64 NetRbSnapshot: effect save tick=(\d+) effect_count=(\d+)"
)
ITEM_SAVE_RE = re.compile(
    r"SSB64 NetRbSnapshot: item save tick=(\d+) item_count=(\d+)"
)
LOAD_HASH_DRIFT_RE = re.compile(
    r"SSB64 NetRollback: LOAD_HASH_DRIFT tick=(\d+) .+ item=0x([0-9A-Fa-f]+)/0x([0-9A-Fa-f]+)"
)
SYNCTEST_FAIL_RE = re.compile(r"SSB64 NetRollback: SYNCTEST_FAIL tick=(\d+)")
FRAME_COMMIT_DIVERGE_RE = re.compile(
    r"SSB64 NetRollback: FRAME_COMMIT_STATE_DIVERGE validation=(\d+) .+ item=0x([0-9A-Fa-f]+) .+ item=0x([0-9A-Fa-f]+)"
)
RESIM_SIM_CORE_RE = re.compile(
    r"SSB64 NetRollback: LOAD_HASH_DRIFT resim-sim-core-ok"
)
VS_STOP_RE = re.compile(r"SSB64 NetPeer: VS session stop|LOAD_HASH_DRIFT — restoring live world")
ITEM_THROW_WINDOW_RE = re.compile(
    r"SSB64 NetSync: item_throw_window tick=(\d+) reason=(\S+) .+ item=(0x[0-9A-Fa-f]+)"
)
YOSTER_CLOUD_RE = re.compile(
    r"SSB64 NetSync: yoster_cloud tick=(\d+) cloud=(\d+) .+ stand=(\d+)"
)
YOSTER_CLOUD_FIGHTER_RE = re.compile(
    r"SSB64 NetSync: yoster_cloud_fighter tick=(\d+) cloud=(\d+) .+ match=(\d+)"
)
ITEM_HASH_WALK_BEGIN_RE = re.compile(
    r"SSB64 NetSync: item_hash_walk begin sim_tick=(\d+) .+ reason=(\S+)"
)
DEATH_REBIRTH_SIM_RE = re.compile(r"SSB64 Netplay: death_rebirth_sim tick=(\d+) player=(\d+) (.+)")
REBIRTH_GATE_RE = re.compile(
    r"SSB64 Netplay: REBIRTH_GATE tick=(\d+) event=(\S+) player=(\d+) (.+)"
)
NETSTATUSVARS_RE = re.compile(r"SSB64 NetStatusVars: (\S+(?: \S+)?)")
NETSTATUSVARS_TICK_PLAYER_RE = re.compile(r"\btick=(\d+) player=(\d+)")
FIGHTER_FIELD_DIFF_RE = re.compile(
    r"SSB64 NetRbSnapshot: fighter_field_diff tag=(\S+) tick=(\d+) player=(\d+) (.+)"
)
RING_SAVE_DIAG_RE = re.compile(
    r"SSB64 NetRbSnapshot: ring_save_diag tick=(\d+) .+ ring_figh=0x([0-9A-Fa-f]+) live_figh_full=0x([0-9A-Fa-f]+) "
    r".+ figh_ok=(\d+) .+ anim_ok=(\d+) blob_figh=0x([0-9A-Fa-f]+) .+ blob_ok=(\d+)"
)
RING_SAVE_PLAYER_RE = re.compile(
    r"SSB64 NetRbSnapshot: ring_save_player tick=(\d+) player=(\d+) fkind=(\d+) status=(\d+) .+ "
    r"full_ok=(\d+) .+ anim_ok=(\d+)"
)
FRAME_COMMIT_DIAG_RE = re.compile(
    r"SSB64 NetInput: frame_commit_diag tick=(\d+) path=(\S+)"
)
RESIM_COMPLETE_RE = re.compile(r"SSB64 NetRollback: resim complete")
POST_RESIM_LIVE_RE = re.compile(
    r"SSB64 NetRollback: POST_RESIM_LIVE sim=(\d+) target=(\d+) hr=(\d+) next_wire=(\d+) wire_gap=(\d+)"
)
STRICT_STALL_RE = re.compile(r"SSB64 NetInput: strict_stall_diag")
# Saffron City (Yamabuki) tower-door diag (SSB64_NETPLAY_YAMABUKI_GATE_DIAG=1). root_af/root_aw is the
# (non-animated) root DObj; child_af/child_aw is the animated door panel; child_t is TraI translate.
YAMABUKI_GATE_RE = re.compile(
    r"SSB64 NetSync: yamabuki_gate tag=(\S+) tick=(\d+) status=(\d+).*?"
    r"child_af=([-0-9.eE+]+) child_aw=([-0-9.eE+]+)"
)
YAMABUKI_GATE_CHILD_T_RE = re.compile(
    r"child_t=\(([-0-9.eE+]+),([-0-9.eE+]+),([-0-9.eE+]+)\)"
)
# Per-node gate DObj tree dump: idx, translate, rotate.z, anim cursor, has-DL, has-material, flags, anim_root.
YAMABUKI_GATE_NODE_RE = re.compile(
    r"SSB64 NetSync: yamabuki_gate_node tag=(\S+) tick=(\d+) idx=(\d+) dobj=(\S+) "
    r"t=\(([-0-9.eE+]+),([-0-9.eE+]+),([-0-9.eE+]+)\) rz=([-0-9.eE+]+) "
    r"af=([-0-9.eE+]+) aw=([-0-9.eE+]+) dl=(\d+) mobj=(\d+) flags=0x([0-9A-Fa-f]+) anim_root=(\d+)"
)
YAMABUKI_HITOKAGE_RE = re.compile(
    r"SSB64 NetSync: yamabuki_hitokage tick=(\d+) item_gobj=(\S+) anim_frame=([-0-9.eE+]+) "
    r"anim_wait=([-0-9.eE+]+) texture_id=(-?\d+) flags=(\d+) flame_wait=(\d+) "
    r"offset=\(([-0-9.eE+]+),([-0-9.eE+]+),([-0-9.eE+]+)\) flame_weapons=(\d+)"
)
SYNCTEST_YAMABUKI_SKIP_RE = re.compile(
    r"SSB64 NetRollback: SYNCTEST_SKIP tick=(\d+) reason=(yamabuki[^\s]*)(?: probe=(\d+))?"
)
HYRULE_TWISTER_RE = re.compile(r"SSB64 NetRbSnapshot: hyrule_twister")
GOBJ_LINK_AUDIT_RE = re.compile(
    r"SSB64 NetRbSnapshot: gobj_link_audit tick=(\d+) f=(\d+) i=(\d+) w=(\d+) ef6=(\d+) ef8=(\d+)"
)
GATE_ANIM_TRA_I_RE = re.compile(r"SSB64: gcPlayDObjAnimJoint - TraI ")
# Ness PK Thunder / jibaku gate (SSB64_NETPLAY_NESS_PKTHUNDER_GATE_DIAG=1).
NESS_PKTHUNDER_GATE_RE = re.compile(
    r"SSB64 Netplay: NESS_PKTHUNDER_GATE tick=(\d+) event=(\S+)"
)
SNAPSHOT_SAVE_DEDUPE_PREFIXES = (
    "SSB64 NetRbSnapshot: effect save tick=",
    "SSB64 NetRbSnapshot: item save tick=",
    "SSB64 NetRbSnapshot: weapon save tick=",
)


def frame_commit_r_signature(line: str) -> str | None:
    m = FRAME_COMMIT_DIAG_RE.search(line)
    if m is None or m.group(2) != "R":
        return None
    fields = dict(FIELD_RE.findall(line))
    keys = ("tick", "path", "publish", "sup", "hr", "wire", "remote_sim_frontier", "D", "commit_gen")
    return "fc_r:" + ",".join(f"{k}={fields.get(k, '?')}" for k in keys)


def sim_state_signature(line: str) -> str | None:
    fields = parse_sim_state_fields(line)
    if fields is None or "tick" not in fields:
        return None
    keys = ("tick", "figh", "item", "eff", "rb_applied", "path")
    return "sim:" + ",".join(f"{k}={fields.get(k, '?')}" for k in keys)


def collapse_r_stall_lines(lines: list[str]) -> tuple[list[str], dict[str, int]]:
    """Collapse repeated path=R frame_commit_diag and frozen sim_state_tick rows (interleaved-safe)."""
    fc_counts: dict[str, int] = {}
    sim_counts: dict[str, int] = {}
    for ln in lines:
        fc_sig = frame_commit_r_signature(ln)
        if fc_sig is not None:
            fc_counts[fc_sig] = fc_counts.get(fc_sig, 0) + 1
            continue
        sim_sig = sim_state_signature(ln)
        if sim_sig is not None:
            sim_counts[sim_sig] = sim_counts.get(sim_sig, 0) + 1

    out: list[str] = []
    stats = {"r_stall_collapsed": 0, "sim_frozen_collapsed": 0}
    fc_seen: set[str] = set()
    sim_seen: set[str] = set()

    for ln in lines:
        fc_sig = frame_commit_r_signature(ln)
        if fc_sig is not None:
            if fc_sig in fc_seen:
                continue
            fc_seen.add(fc_sig)
            n = fc_counts.get(fc_sig, 1)
            if n > 1:
                out.append(f"{ln}  (... repeated {n - 1} more times)")
                stats["r_stall_collapsed"] += n - 1
            else:
                out.append(ln)
            continue
        sim_sig = sim_state_signature(ln)
        if sim_sig is not None:
            if sim_sig in sim_seen:
                continue
            sim_seen.add(sim_sig)
            n = sim_counts.get(sim_sig, 1)
            if n > 1:
                out.append(f"{ln}  (... repeated {n - 1} more times)")
                stats["sim_frozen_collapsed"] += n - 1
            else:
                out.append(ln)
            continue
        out.append(ln)
    return out, stats


def collect_ring_save_summary(lines: list[str]) -> list[str]:
    """Summarize ring_save_diag save-time hash parity (SSB64_NETPLAY_SNAPSHOT_RING_SAVE_DIAG=1)."""
    rows: list[tuple[str, str, str, str]] = []
    player_rows: list[tuple[str, str, str, str, str]] = []
    for ln in lines:
        m = RING_SAVE_DIAG_RE.search(ln)
        if m is not None:
            rows.append((m.group(1), m.group(4), m.group(5), m.group(7)))
            continue
        m = RING_SAVE_PLAYER_RE.search(ln)
        if m is not None:
            player_rows.append((m.group(1), m.group(2), m.group(4), m.group(5), m.group(6)))
    if not rows and not player_rows:
        return []
    out: list[str] = []
    out.append(f"    ring_save_diag lines: {len(rows)}")
    if rows:
        figh_bad = [r for r in rows if r[1] == "0"]
        anim_bad = [r for r in rows if r[2] == "0"]
        blob_bad = [r for r in rows if r[3] == "0"]
        if figh_bad:
            ticks = sorted({r[0] for r in figh_bad}, key=int)
            out.append(
                f"    ring_save figh_ok=0: {len(figh_bad)} first_ticks={','.join(ticks[:8])}"
                + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
            )
        if anim_bad:
            ticks = sorted({r[0] for r in anim_bad}, key=int)
            out.append(
                f"    ring_save anim_ok=0: {len(anim_bad)} first_ticks={','.join(ticks[:8])}"
                + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
            )
        if blob_bad:
            ticks = sorted({r[0] for r in blob_bad}, key=int)
            out.append(
                f"    ring_save blob_ok=0 (ring!=blob rehash): {len(blob_bad)} first_ticks={','.join(ticks[:8])}"
                + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
            )
    if player_rows:
        full_bad = [r for r in player_rows if r[3] == "0"]
        anim_bad_p = [r for r in player_rows if r[4] == "0"]
        dair = [r for r in player_rows if r[2] == "213"]
        out.append(f"    ring_save_player lines: {len(player_rows)}")
        if full_bad:
            ticks = sorted({r[0] for r in full_bad}, key=int)
            out.append(
                f"    ring_save_player full_ok=0: {len(full_bad)} first_ticks={','.join(ticks[:8])}"
                + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
            )
        if anim_bad_p:
            ticks = sorted({r[0] for r in anim_bad_p}, key=int)
            out.append(
                f"    ring_save_player anim_ok=0: {len(anim_bad_p)} first_ticks={','.join(ticks[:8])}"
                + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
            )
        if dair:
            ticks = sorted({r[0] for r in dair}, key=int)
            out.append(
                f"    ring_save_player status=213 (AttackAirLw): {len(dair)} ticks={','.join(ticks[:8])}"
                + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
            )
    return out


def collapse_ring_save_diag_lines(lines: list[str]) -> tuple[list[str], int]:
    """Collapse consecutive ring_save_diag rows when parity flags match (tick still shown on first)."""
    counts: dict[str, int] = {}
    for ln in lines:
        m = RING_SAVE_DIAG_RE.search(ln)
        if m is None:
            continue
        sig = f"ring:{m.group(4)}:{m.group(5)}:{m.group(7)}"
        counts[sig] = counts.get(sig, 0) + 1
    out: list[str] = []
    seen: set[str] = set()
    collapsed = 0
    for ln in lines:
        m = RING_SAVE_DIAG_RE.search(ln)
        if m is None:
            out.append(ln)
            continue
        sig = f"ring:{m.group(4)}:{m.group(5)}:{m.group(7)}"
        if sig in seen:
            continue
        seen.add(sig)
        n = counts.get(sig, 1)
        if n > 1:
            out.append(f"{ln}  (... repeated {n - 1} more times with same figh_ok/anim_ok/blob_ok)")
            collapsed += n - 1
        else:
            out.append(ln)
    return out, collapsed


def yamabuki_gate_child_t(line: str) -> tuple[str, str, str] | None:
    m = YAMABUKI_GATE_CHILD_T_RE.search(line)
    if m is None:
        return None
    return m.group(1), m.group(2), m.group(3)


def yamabuki_gate_signature(line: str) -> str | None:
    """Signature for collapsing per-tick yamabuki_gate spam: identical gate/anim state, only tick moving."""
    if YAMABUKI_GATE_RE.search(line) is None:
        return None
    fields = dict(FIELD_RE.findall(line))
    keys = (
        "tag",
        "status",
        "phase",
        "gate_pos",
        "gate_noentry",
        "root_af",
        "root_aw",
        "child_af",
        "child_aw",
        "child_rz",
        "yaku3",
        "yaku_st",
        "monster_gobj",
    )
    sig = "yam:" + ",".join(f"{k}={fields.get(k, '?')}" for k in keys)
    child_t = yamabuki_gate_child_t(line)
    if child_t is not None:
        sig += f",child_t={','.join(child_t)}"
    return sig


def collapse_yamabuki_gate_lines(lines: list[str]) -> tuple[list[str], int]:
    """Collapse consecutive yamabuki_gate rows whose gate/anim state is identical (only tick/monster_wait move)."""
    counts: dict[str, int] = {}
    for ln in lines:
        sig = yamabuki_gate_signature(ln)
        if sig is not None:
            counts[sig] = counts.get(sig, 0) + 1
    out: list[str] = []
    seen: set[str] = set()
    collapsed = 0
    for ln in lines:
        sig = yamabuki_gate_signature(ln)
        if sig is None:
            out.append(ln)
            continue
        if sig in seen:
            continue
        seen.add(sig)
        n = counts.get(sig, 1)
        if n > 1:
            out.append(f"{ln}  (... repeated {n - 1} more times with same gate/anim state)")
            collapsed += n - 1
        else:
            out.append(ln)
    return out, collapsed


def yamabuki_gate_node_signature(line: str) -> str | None:
    """Signature for collapsing per-tick yamabuki_gate_node spam: identical per-node render state."""
    m = YAMABUKI_GATE_NODE_RE.search(line)
    if m is None:
        return None
    # tag(1), idx(3), t(5,6,7), rz(8), af(9), aw(10), dl(11), mobj(12), flags(13), anim_root(14)
    return (
        f"gnode:tag={m.group(1)},idx={m.group(3)},t={m.group(5)},{m.group(6)},{m.group(7)},"
        f"rz={m.group(8)},af={m.group(9)},aw={m.group(10)},dl={m.group(11)},mobj={m.group(12)},"
        f"flags={m.group(13)},anim_root={m.group(14)}"
    )


def collapse_yamabuki_gate_node_lines(lines: list[str]) -> tuple[list[str], int]:
    """Collapse consecutive yamabuki_gate_node rows whose per-node render state is identical."""
    counts: dict[str, int] = {}
    for ln in lines:
        sig = yamabuki_gate_node_signature(ln)
        if sig is not None:
            counts[sig] = counts.get(sig, 0) + 1
    out: list[str] = []
    seen: set[str] = set()
    collapsed = 0
    for ln in lines:
        sig = yamabuki_gate_node_signature(ln)
        if sig is None:
            out.append(ln)
            continue
        if sig in seen:
            continue
        seen.add(sig)
        n = counts.get(sig, 1)
        if n > 1:
            out.append(f"{ln}  (... repeated {n - 1} more times with same node render state)")
            collapsed += n - 1
        else:
            out.append(ln)
    return out, collapsed


def collect_yamabuki_gate_node_summary(lines: list[str]) -> list[str]:
    """Pinpoint which gate DObj node carries the visible mesh and whether that node animates.

    The single-node `yamabuki_gate child_*` sampling reads root->child only. This walks the full
    per-node dump to answer: (a) which idx has the display list (dl=1) / material (mobj=1), (b) whether
    that drawn node's translate/anim_frame ever moves, and (c) whether any drawn node is HIDDEN. A door
    that "won't visually open" with a correct sim state usually means the animating node (translate
    moving) is NOT the node carrying the DL, or the DL node is HIDDEN."""
    rows: list[tuple[str, ...]] = []
    for ln in lines:
        m = YAMABUKI_GATE_NODE_RE.search(ln)
        if m is not None:
            rows.append(m.groups())
    if not rows:
        return []
    out: list[str] = []
    # group by idx: collect translate-z spread, anim_frame spread, dl/mobj/hidden status.
    by_idx: dict[str, list[tuple[str, ...]]] = {}
    for r in rows:
        by_idx.setdefault(r[2], []).append(r)
    node_ticks = sorted({r[1] for r in rows}, key=int)
    out.append(
        f"    yamabuki_gate_node rows: {len(rows)} nodes={len(by_idx)} "
        f"tick_span={node_ticks[0]}..{node_ticks[-1]}"
    )

    def to_float(s: str) -> float | None:
        try:
            return float(s)
        except ValueError:
            return None

    drawn_idxs: list[str] = []
    for idx in sorted(by_idx.keys(), key=int):
        node_rows = by_idx[idx]
        dl_any = any(r[10] == "1" for r in node_rows)
        mobj_any = any(r[11] == "1" for r in node_rows)
        hidden_any = any((int(r[12], 16) & 0x1) != 0 for r in node_rows)
        afs = [v for v in (to_float(r[8]) for r in node_rows) if v is not None]
        tzs = [v for v in (to_float(r[6]) for r in node_rows) if v is not None]
        tys = [v for v in (to_float(r[5]) for r in node_rows) if v is not None]
        af_moves = bool(afs) and (max(afs) - min(afs) > 0.01)
        t_moves = (bool(tzs) and (max(tzs) - min(tzs) > 0.01)) or (
            bool(tys) and (max(tys) - min(tys) > 0.01)
        )
        if dl_any:
            drawn_idxs.append(idx)
        if dl_any or mobj_any:
            af_s = f"{min(afs):.2f}..{max(afs):.2f}" if afs else "n/a"
            out.append(
                f"    node idx={idx} dl={int(dl_any)} mobj={int(mobj_any)} hidden={int(hidden_any)} "
                f"af={af_s} af_moves={int(af_moves)} t_moves={int(t_moves)}"
            )
            if dl_any and hidden_any:
                out.append(f"    WARNING node idx={idx} carries DL but is HIDDEN on some ticks")
            if dl_any and not af_moves and not t_moves:
                out.append(
                    f"    WARNING drawn node idx={idx} never moved (anim_frame & translate static) — "
                    f"mesh visually frozen even if sim state opened"
                )
    if not drawn_idxs:
        out.append("    WARNING no gate node reported a display list (dl=1) — mesh source node not found")
    return out


def yamabuki_hitokage_signature(line: str) -> str | None:
    m = YAMABUKI_HITOKAGE_RE.search(line)
    if m is None:
        return None
    return (
        f"hitokage:flags={m.group(6)} texture={m.group(5)} flame_w={m.group(11)} "
        f"anim_f={m.group(3)} anim_w={m.group(4)} offset={m.group(8)},{m.group(9)},{m.group(10)}"
    )


def collapse_yamabuki_hitokage_lines(lines: list[str]) -> tuple[list[str], int]:
    """Collapse consecutive yamabuki_hitokage rows with identical flame/presentation state."""
    counts: dict[str, int] = {}
    for ln in lines:
        sig = yamabuki_hitokage_signature(ln)
        if sig is not None:
            counts[sig] = counts.get(sig, 0) + 1
    out: list[str] = []
    seen: set[str] = set()
    collapsed = 0
    for ln in lines:
        sig = yamabuki_hitokage_signature(ln)
        if sig is None:
            out.append(ln)
            continue
        if sig in seen:
            continue
        seen.add(sig)
        n = counts.get(sig, 1)
        if n > 1:
            out.append(f"{ln}  (... repeated {n - 1} more times with same hitokage/flame state)")
            collapsed += n - 1
        else:
            out.append(ln)
    return out, collapsed


def collect_yamabuki_hitokage_summary(lines: list[str]) -> list[str]:
    """Summarize Charmander walk-out / flame diagnostics (yamabuki_hitokage)."""
    rows: list[tuple[str, ...]] = []
    for ln in lines:
        m = YAMABUKI_HITOKAGE_RE.search(ln)
        if m is not None:
            rows.append(m.groups())
    if not rows:
        return []
    flag_names = {
        "0": "NONE",
        "1": "WAIT",
        "2": "INSTANT",
        "3": "ALL",
    }
    out: list[str] = []
    out.append(f"    yamabuki_hitokage rows: {len(rows)}")
    flags_at_spawn = rows[0][5]
    out.append(
        f"    hitokage spawn flags={flags_at_spawn} ({flag_names.get(flags_at_spawn, '?')}) "
        f"tick={rows[0][0]}"
    )
    flame_rows = [r for r in rows if int(r[10]) > 0]
    if flame_rows:
        out.append(
            f"    hitokage flame_weapons>0: {len(flame_rows)} rows, first tick={flame_rows[0][0]} "
            f"max_weapons={max(int(r[10]) for r in flame_rows)}"
        )
    else:
        out.append(
            "    hitokage flame_weapons: never >0 "
            f"(flags={flags_at_spawn} — NONE means no flamethrower this spawn)"
        )
    texture_flame = [r for r in rows if r[4] == "1"]
    if texture_flame:
        out.append(f"    hitokage texture_id=1 (mouth open): {len(texture_flame)} rows")
    hollow = [r for r in rows if r[2] == "-1.00" or r[4] == "-1"]
    if hollow:
        ticks = sorted({r[0] for r in hollow}, key=int)
        out.append(
            f"    WARNING hitokage hollow item (anim/texture -1): ticks={','.join(ticks[:8])}"
            + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
        )
    return out


def collect_yamabuki_synctest_skip_summary(lines: list[str]) -> list[str]:
    """Summarize Yamabuki-related synctest probe skip reasons."""
    by_reason: dict[str, list[str]] = {}
    for ln in lines:
        m = SYNCTEST_YAMABUKI_SKIP_RE.search(ln)
        if m is None:
            continue
        reason = m.group(2)
        by_reason.setdefault(reason, []).append(m.group(1))
    if not by_reason:
        return []
    out: list[str] = []
    out.append(f"    yamabuki synctest_skip rows: {sum(len(v) for v in by_reason.values())}")
    for reason in sorted(by_reason.keys()):
        ticks = sorted(set(by_reason[reason]), key=int)
        out.append(
            f"    synctest_skip reason={reason}: {len(by_reason[reason])} "
            f"first_ticks={','.join(ticks[:6])}"
            + (f" (+{len(ticks) - 6} more)" if len(ticks) > 6 else "")
        )
    return out


def collect_hyrule_twister_summary(lines: list[str]) -> list[str]:
    rows = [ln for ln in lines if HYRULE_TWISTER_RE.search(ln)]
    if not rows:
        return []
    tags: dict[str, int] = {}
    for ln in rows:
        if "hyrule_twister_repair" in ln:
            tags["repair"] = tags.get("repair", 0) + 1
        elif "hyrule_twister_capture" in ln:
            tags["capture"] = tags.get("capture", 0) + 1
        elif "hyrule_twister_apply_drift" in ln:
            tags["apply_drift"] = tags.get("apply_drift", 0) + 1
        elif "hyrule_twister_obstacle_fail" in ln:
            tags["obstacle_fail"] = tags.get("obstacle_fail", 0) + 1
        elif "hyrule_twister_rider" in ln:
            tags["rider"] = tags.get("rider", 0) + 1
        else:
            tags["other"] = tags.get("other", 0) + 1
    return [f"    hyrule_twister rows: {len(rows)} by_kind={tags}"]


def parse_netstatusvars_line(line: str) -> tuple[str, str, dict[str, str]] | None:
    """Parse SSB64 NetStatusVars witness / corruption diagnostics."""
    m = NETSTATUSVARS_RE.search(line)
    if m is None:
        return None
    event = m.group(1)
    fields = parse_kv_tail(line[m.end() :])
    tp = NETSTATUSVARS_TICK_PLAYER_RE.search(line)
    if tp is not None:
        fields["tick"] = tp.group(1)
        fields["player"] = tp.group(2)
    if event.startswith("corrupt "):
        fields["kind"] = event.split(" ", 1)[1]
        event = "corrupt"
    fields["event"] = event
    return fields.get("tick", "?"), event, fields


def netstatusvars_collapse_signature(line: str) -> str | None:
    """Collapse repeated witness rows that differ only by tick."""
    if "SSB64 NetStatusVars:" not in line:
        return None
    if "witness armed" in line:
        return None
    return re.sub(r"\btick=\d+", "tick=*", line).strip()


def collapse_netstatusvars_lines(lines: list[str]) -> tuple[list[str], int]:
    counts: dict[str, int] = {}
    for ln in lines:
        sig = netstatusvars_collapse_signature(ln)
        if sig is not None:
            counts[sig] = counts.get(sig, 0) + 1
    out: list[str] = []
    seen: set[str] = set()
    collapsed = 0
    for ln in lines:
        sig = netstatusvars_collapse_signature(ln)
        if sig is None:
            out.append(ln)
            continue
        if sig in seen:
            continue
        seen.add(sig)
        n = counts.get(sig, 1)
        if n > 1:
            out.append(f"{ln}  (... repeated {n - 1} more times)")
            collapsed += n - 1
        else:
            out.append(ln)
    return out, collapsed


def collect_netstatusvars_summary(lines: list[str]) -> list[str]:
    """Summarize FTStatusVars witness stomps and in-overlay corruption rows."""
    events: list[tuple[str, str, dict[str, str]]] = []
    for ln in lines:
        parsed = parse_netstatusvars_line(ln)
        if parsed is not None:
            events.append(parsed)
    if not events:
        return []

    by_event: dict[str, int] = {}
    armed = False
    stomps: list[tuple[str, str, dict[str, str]]] = []
    corrupt: list[tuple[str, str, dict[str, str]]] = []

    for tick_s, event, fields in events:
        by_event[event] = by_event.get(event, 0) + 1
        if event == "witness armed":
            armed = True
        elif event == "witness stomp":
            stomps.append((tick_s, event, fields))
        elif event == "corrupt":
            corrupt.append((tick_s, event, fields))

    out: list[str] = []
    total = len(events)
    out.append(f"    NetStatusVars rows: {total} by_event={by_event}")
    if armed:
        out.append("    NetStatusVars witness armed (SSB64_NETPLAY_STATUSVARS_WITNESS=1)")
    if stomps:
        first = stomps[0]
        out.append(
            f"    NetStatusVars stomps: {len(stomps)} first tick={first[0]} "
            f"player={first[2].get('player', '?')} accessed={first[2].get('accessed', '?')} "
            f"expected={first[2].get('expected', '?')}"
        )
    if corrupt:
        kinds: dict[str, int] = {}
        for _tick_s, _event, fields in corrupt:
            kind = fields.get("kind", fields.get("event", "?"))
            kinds[kind] = kinds.get(kind, 0) + 1
        first = corrupt[0]
        out.append(
            f"    NetStatusVars corrupt: {len(corrupt)} by_kind={kinds} "
            f"first tick={first[0]} player={first[2].get('player', '?')}"
        )
    return out


def parse_ness_pkthunder_gate_line(line: str) -> tuple[str, str, dict[str, str]] | None:
    m = NESS_PKTHUNDER_GATE_RE.search(line)
    if m is None:
        return None
    tick_s, event = m.group(1), m.group(2)
    fields = parse_kv_tail(line[m.end() :])
    fields["tick"] = tick_s
    fields["event"] = event
    return tick_s, event, fields


def ness_pkthunder_stall_signature(line: str) -> str | None:
    parsed = parse_ness_pkthunder_gate_line(line)
    if parsed is None:
        return None
    _tick_s, event, fields = parsed
    if event != "jibaku_stall_tick":
        return None
    return (
        f"ness_stall:{fields.get('player', '?')}:{fields.get('status', '?')}:"
        f"{fields.get('anim_length', '?')}"
    )


def collapse_ness_pkthunder_stall_lines(lines: list[str]) -> tuple[list[str], int]:
    """Collapse repeated jibaku_stall_tick rows (only tick moves)."""
    counts: dict[str, int] = {}
    for ln in lines:
        sig = ness_pkthunder_stall_signature(ln)
        if sig is not None:
            counts[sig] = counts.get(sig, 0) + 1
    out: list[str] = []
    seen: set[str] = set()
    collapsed = 0
    for ln in lines:
        sig = ness_pkthunder_stall_signature(ln)
        if sig is None:
            out.append(ln)
            continue
        if sig in seen:
            continue
        seen.add(sig)
        n = counts.get(sig, 1)
        if n > 1:
            out.append(f"{ln}  (... repeated {n - 1} more times)")
            collapsed += n - 1
        else:
            out.append(ln)
    return out, collapsed


def collect_ness_pkthunder_summary(lines: list[str]) -> list[str]:
    """Summarize Ness PK Thunder hold/jibaku gate diagnostics."""
    events: list[tuple[str, str, dict[str, str]]] = []
    for ln in lines:
        parsed = parse_ness_pkthunder_gate_line(ln)
        if parsed is not None:
            events.append(parsed)
    if not events:
        return []

    by_event: dict[str, int] = {}
    holds: list[tuple[str, str, dict[str, str]]] = []
    jibakus: list[tuple[str, str, dict[str, str]]] = []
    jibaku_phases: list[tuple[str, str, dict[str, str]]] = []
    post_culls: list[tuple[str, str, dict[str, str]]] = []
    post_finishes: list[tuple[str, str, dict[str, str]]] = []
    weapon_states: list[tuple[str, str, dict[str, str]]] = []
    sanitize_gravity_rows: list[tuple[str, str, dict[str, str]]] = []
    sanitize_delay_rows: list[tuple[str, str, dict[str, str]]] = []
    suspects: list[str] = []

    for tick_s, event, fields in events:
        by_event[event] = by_event.get(event, 0) + 1
        resim = fields.get("resim", "0") == "1"
        if event == "hold_enter":
            holds.append((tick_s, event, fields))
            delay_s = fields.get("delay", "?")
            gravity_delay_s = fields.get("gravity_delay", "?")
            try:
                if int(delay_s) <= 0:
                    suspects.append(
                        f"tick={tick_s} player={fields.get('player', '?')} hold_enter delay={delay_s}"
                    )
            except ValueError:
                pass
            try:
                if int(gravity_delay_s) <= 0:
                    suspects.append(
                        f"tick={tick_s} player={fields.get('player', '?')} hold_enter "
                        f"gravity_delay={gravity_delay_s} (no anti-gravity float in Hold)"
                    )
            except ValueError:
                pass
        elif event == "sanitize_gravity":
            sanitize_gravity_rows.append((tick_s, event, fields))
        elif event == "sanitize_delay":
            sanitize_delay_rows.append((tick_s, event, fields))
        elif event == "jibaku_phase":
            jibaku_phases.append((tick_s, event, fields))
        elif event == "jibaku_post_cull":
            post_culls.append((tick_s, event, fields))
        elif event == "jibaku_post_finish":
            post_finishes.append((tick_s, event, fields))
            if fields.get("defer_teardown", "1") == "0":
                suspects.append(
                    f"tick={tick_s} player={fields.get('player', '?')} jibaku_post_finish "
                    f"defer_teardown=0 status={fields.get('status', '?')} mask_curr={fields.get('mask_curr', '?')}"
                )
        elif event == "jibaku_weapon_state":
            weapon_states.append((tick_s, event, fields))
            try:
                if int(fields.get("weapons", "0")) > 0:
                    suspects.append(
                        f"tick={tick_s} player={fields.get('player', '?')} jibaku_weapon_state "
                        f"weapons={fields.get('weapons', '?')} head_pkstatus={fields.get('head_pkstatus', '?')}"
                    )
            except ValueError:
                pass
        elif event == "jibaku_trigger":
            jibakus.append((tick_s, event, fields))
            if resim:
                continue
            hold_frames_s = fields.get("hold_frames", "?")
            delay_before_s = fields.get("delay_before", "?")
            try:
                hold_frames = int(hold_frames_s)
                delay_before = int(delay_before_s)
            except ValueError:
                hold_frames = -1
                delay_before = 0
            if hold_frames >= 0 and hold_frames <= 5:
                suspects.append(
                    f"tick={tick_s} player={fields.get('player', '?')} jibaku hold_frames={hold_frames_s} "
                    f"from_status={fields.get('from_status', '?')} delay_before={delay_before_s}"
                )
            elif delay_before != 0:
                suspects.append(
                    f"tick={tick_s} player={fields.get('player', '?')} jibaku delay_before={delay_before_s} "
                    f"(expected 0) hold_frames={hold_frames_s}"
                )

    out: list[str] = []
    out.append(f"    NESS_PKTHUNDER_GATE rows: {len(events)} events={by_event}")
    out.append(f"    hold_enter: {len(holds)}  jibaku_trigger: {len(jibakus)}  jibaku_phase: {len(jibaku_phases)}")
    if sanitize_gravity_rows or sanitize_delay_rows:
        out.append(
            f"    sanitize: delay={len(sanitize_delay_rows)} gravity={len(sanitize_gravity_rows)} "
            f"(rollback scrub recovery)"
        )
    if sanitize_gravity_rows:
        sg_samples: list[str] = []
        for tick_s, _event, fields in sanitize_gravity_rows[:6]:
            sg_samples.append(
                f"tick={tick_s} p={fields.get('player', '?')} status={fields.get('status', '?')} "
                f"was={fields.get('was', '?')} now={fields.get('now', '?')} expected={fields.get('expected', '?')}"
            )
        out.append(f"    sanitize_gravity samples: {'; '.join(sg_samples)}")
        if len(sanitize_gravity_rows) > 6:
            out.append(f"    ... +{len(sanitize_gravity_rows) - 6} more sanitize_gravity rows")
    if holds:
        zero_gravity_holds = 0
        for _tick_s, _event, fields in holds:
            try:
                if int(fields.get("gravity_delay", "1")) <= 0:
                    zero_gravity_holds += 1
            except ValueError:
                pass
        if zero_gravity_holds:
            out.append(
                f"    hold_enter gravity_delay=0: {zero_gravity_holds}/{len(holds)} "
                f"(premature gravity — expect sanitize_gravity on prior Start rollback)"
            )
    if weapon_states:
        out.append(f"    jibaku_weapon_state: {len(weapon_states)}")
        ws_samples: list[str] = []
        for tick_s, _event, fields in weapon_states[:6]:
            ws_samples.append(
                f"tick={tick_s} p={fields.get('player', '?')} weapons={fields.get('weapons', '?')} "
                f"head_pkstatus={fields.get('head_pkstatus', '?')} fighter_status={fields.get('fighter_status', '?')}"
            )
        out.append(f"    jibaku_weapon_state samples: {'; '.join(ws_samples)}")
    if post_finishes:
        out.append(f"    jibaku_post_finish: {len(post_finishes)}")
        pf_samples: list[str] = []
        for tick_s, _event, fields in post_finishes[:6]:
            pf_samples.append(
                f"tick={tick_s} p={fields.get('player', '?')} status={fields.get('status', '?')} "
                f"defer={fields.get('defer_teardown', '?')} mask=0x{fields.get('mask_curr', '?')}"
            )
        out.append(f"    jibaku_post_finish samples: {'; '.join(pf_samples)}")
    if post_culls:
        deferred_n = sum(1 for _t, _e, f in post_culls if f.get("action") == "deferred")
        cull_n = sum(1 for _t, _e, f in post_culls if f.get("action") == "cull")
        out.append(f"    jibaku_post_cull: deferred={deferred_n} cull={cull_n}")
        cull_samples: list[str] = []
        for tick_s, _event, fields in post_culls:
            if fields.get("action") != "cull":
                continue
            cull_samples.append(
                f"tick={tick_s} before={fields.get('weapons_before', '?')} "
                f"after={fields.get('weapons_after', '?')} p={fields.get('player', '?')}"
            )
            if len(cull_samples) >= 6:
                break
        if cull_samples:
            out.append(f"    jibaku_post_cull cull samples: {'; '.join(cull_samples)}")
        defer_samples: list[str] = []
        for tick_s, _event, fields in post_culls:
            if fields.get("action") != "deferred":
                continue
            defer_samples.append(
                f"tick={tick_s} weapons={fields.get('weapons', '?')} "
                f"cull_at={fields.get('cull_at_tick', '?')} p={fields.get('player', '?')}"
            )
            if len(defer_samples) >= 4:
                break
        if defer_samples:
            out.append(f"    jibaku_post_cull defer samples: {'; '.join(defer_samples)}")
        crash_pairs: list[str] = []
        for tick_s, _event, fields in post_culls:
            if fields.get("action") != "deferred":
                continue
            try:
                defer_tick = int(tick_s)
                cull_at = int(fields.get("cull_at_tick", "0"))
            except ValueError:
                continue
            if cull_at == defer_tick + 1:
                has_cull = any(
                    t == str(cull_at) and f.get("action") == "cull"
                    for t, _e, f in post_culls
                )
                if not has_cull:
                    crash_pairs.append(f"jibaku@{defer_tick} deferred but no cull@{cull_at}")
        if crash_pairs:
            out.append(f"    SUSPECT missing post-jibaku cull: {len(crash_pairs)}")
            for s in crash_pairs[:6]:
                out.append(f"      {s}")
    if jibakus:
        samples: list[str] = []
        for tick_s, _event, fields in jibakus[:6]:
            samples.append(
                f"tick={tick_s} p={fields.get('player', '?')} hold_frames={fields.get('hold_frames', '?')} "
                f"from={fields.get('from_status', '?')} delay_before={fields.get('delay_before', '?')}"
            )
        out.append(f"    jibaku samples: {'; '.join(samples)}")
        if len(jibakus) > 6:
            out.append(f"    ... +{len(jibakus) - 6} more jibaku_trigger rows")
    if suspects:
        out.append(f"    SUSPECT instant/early jibaku: {len(suspects)}")
        for s in suspects[:8]:
            out.append(f"      {s}")
        if len(suspects) > 8:
            out.append(f"      ... +{len(suspects) - 8} more")
    return out


def collect_gobj_link_audit_summary(lines: list[str]) -> list[str]:
    """Summarize post-apply link counts (useful when item/weapon/effect restore ejects stage actors)."""
    rows: list[tuple[str, str, str, str]] = []
    for ln in lines:
        m = GOBJ_LINK_AUDIT_RE.search(ln)
        if m is not None:
            rows.append((m.group(1), m.group(3), m.group(4), m.group(5)))
    if not rows:
        return []
    out: list[str] = []
    out.append(f"    gobj_link_audit rows: {len(rows)}")
    prev: tuple[str, str, str] | None = None
    for tick, items, weapons, ef6 in rows:
        if tick == "4294967295":
            continue
        cur = (items, weapons, ef6)
        if prev is not None and cur != prev:
            out.append(
                f"    gobj_link_audit transition tick={tick}: "
                f"items {prev[0]}->{items} weapons {prev[1]}->{weapons} ef6 {prev[2]}->{ef6}"
            )
            break
        prev = cur
    return out


def collect_yamabuki_gate_summary(lines: list[str]) -> list[str]:
    """Summarize Saffron tower-door lifecycle + whether the door mesh ever animated open."""
    rows: list[tuple[str, str, str, str, str]] = []
    for ln in lines:
        m = YAMABUKI_GATE_RE.search(ln)
        if m is not None:
            rows.append((m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)))
    if not rows:
        return []
    out: list[str] = []
    out.append(f"    yamabuki_gate rows: {len(rows)}")

    def first_tick_for(tag: str) -> str | None:
        for r in rows:
            if r[0] == tag:
                return r[1]
        return None

    for tag in ("open_entry", "spawn_open", "spawn_done"):
        tick = first_tick_for(tag)
        if tick is not None:
            out.append(f"    first {tag}: tick={tick}")

    def to_float(s: str) -> float | None:
        try:
            return float(s)
        except ValueError:
            return None

    child_afs = [v for v in (to_float(r[3]) for r in rows) if v is not None]
    if child_afs:
        out.append(f"    door child_af range: min={min(child_afs):.2f} max={max(child_afs):.2f}")
        if max(child_afs) <= 0.0:
            out.append("    WARNING: door child_af never advanced past 0 — mesh stayed closed (anim frozen)")

    proc_rows = [r for r in rows if r[0] == "proc_anim"]
    if proc_rows:
        proc_afs = [v for v in (to_float(r[3]) for r in proc_rows) if v is not None]
        if proc_afs:
            out.append(
                f"    proc_anim live-advance samples: {len(proc_rows)} "
                f"child_af {min(proc_afs):.2f}..{max(proc_afs):.2f}"
            )
    else:
        out.append("    proc_anim samples: 0 (no live door-anim advance observed)")

    corrupt_tz: list[tuple[str, str]] = []
    for ln in lines:
        m = YAMABUKI_GATE_RE.search(ln)
        if m is None:
            continue
        child_t = yamabuki_gate_child_t(ln)
        if child_t is None:
            continue
        try:
            tz = abs(float(child_t[2]))
        except ValueError:
            continue
        if tz > 10000.0:
            corrupt_tz.append((m.group(2), child_t[2]))
    if corrupt_tz:
        ticks = sorted({t for t, _ in corrupt_tz}, key=int)
        out.append(
            f"    WARNING door child_tz corruption (|tz|>10000): {len(corrupt_tz)} rows "
            f"first_ticks={','.join(ticks[:6])}"
            + (f" (+{len(ticks) - 6} more)" if len(ticks) > 6 else "")
        )
    return out


def collect_hash_drift_summary(lines: list[str]) -> list[str]:
    """Summarize LOAD_HASH / synctest / frame-commit item diverge episodes."""
    out: list[str] = []
    load_drifts: list[tuple[str, str, str, str]] = []
    item_walks: list[tuple[str, str]] = []
    for ln in lines:
        m = LOAD_HASH_DRIFT_RE.search(ln)
        if m is not None:
            tick, snap, live = m.group(1), m.group(2), m.group(3)
            load_drifts.append((tick, snap, live, "item_mismatch" if snap.lower() != live.lower() else "other"))
            continue
        m = SYNCTEST_FAIL_RE.search(ln)
        if m is not None:
            load_drifts.append((m.group(1), "", "", "synctest_fail"))
            continue
        m = FRAME_COMMIT_DIVERGE_RE.search(ln)
        if m is not None:
            out.append(
                f"    frame_commit_item_diverge: validation={m.group(1)} "
                f"local_item=0x{m.group(2)} peer_item=0x{m.group(3)}"
            )
            continue
        m = ITEM_HASH_WALK_BEGIN_RE.search(ln)
        if m is not None:
            item_walks.append((m.group(1), m.group(2)))
    if load_drifts:
        item_mm = [d for d in load_drifts if d[3] == "item_mismatch"]
        synctest = [d for d in load_drifts if d[3] == "synctest_fail"]
        out.append(f"    LOAD_HASH_DRIFT lines: {len(load_drifts)} item_mismatch={len(item_mm)}")
        if item_mm:
            ticks = sorted({d[0] for d in item_mm}, key=int)
            out.append(f"    first item LOAD_HASH_DRIFT ticks: {','.join(ticks[:8])}" + (
                f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else ""
            ))
        if synctest:
            ticks = sorted({d[0] for d in synctest}, key=int)
            out.append(f"    SYNCTEST_FAIL ticks: {','.join(ticks[:8])}" + (
                f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else ""
            ))
    if item_walks:
        reasons: dict[str, int] = {}
        for _, reason in item_walks:
            reasons[reason] = reasons.get(reason, 0) + 1
        out.append(f"    item_hash_walk episodes: {len(item_walks)} by_reason={reasons}")
    throw_windows: list[tuple[str, str, str]] = []
    for ln in lines:
        m = ITEM_THROW_WINDOW_RE.search(ln)
        if m is not None:
            throw_windows.append((m.group(1), m.group(2), m.group(3)))
    if throw_windows:
        ticks = sorted({d[0] for d in throw_windows}, key=int)
        out.append(f"    item_throw_window episodes: {len(throw_windows)} first_ticks={','.join(ticks[:8])}" + (
            f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else ""
        ))
    yoster_clouds: list[tuple[str, str, str]] = []
    yoster_fighters: list[tuple[str, str, str]] = []
    for ln in lines:
        m = YOSTER_CLOUD_RE.search(ln)
        if m is not None:
            yoster_clouds.append((m.group(1), m.group(2), m.group(3)))
            continue
        m = YOSTER_CLOUD_FIGHTER_RE.search(ln)
        if m is not None:
            yoster_fighters.append((m.group(1), m.group(2), m.group(3)))
    if yoster_clouds:
        stand_rows = [row for row in yoster_clouds if row[2] == "1"]
        ticks = sorted({row[0] for row in yoster_clouds}, key=int)
        out.append(
            f"    yoster_cloud rows: {len(yoster_clouds)} stand=1 rows={len(stand_rows)} "
            f"first_ticks={','.join(ticks[:8])}" + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
        )
    if yoster_fighters:
        mismatch = [row for row in yoster_fighters if row[2] == "0"]
        ticks = sorted({row[0] for row in mismatch}, key=int)
        out.append(
            f"    yoster_cloud_fighter rows: {len(yoster_fighters)} match=0 rows={len(mismatch)}"
            + (
                f" first_mismatch_ticks={','.join(ticks[:8])}"
                + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
                if ticks
                else ""
            )
        )
    resim_core = sum(1 for ln in lines if RESIM_SIM_CORE_RE.search(ln))
    if resim_core:
        out.append(f"    resim-sim-core-ok (recovery continued): {resim_core}")
    vs_stop = sum(1 for ln in lines if VS_STOP_RE.search(ln))
    if vs_stop:
        out.append(f"    session stop / hard LOAD_HASH abort lines: {vs_stop}")
    return out


def collect_rollback_stall_summary(lines: list[str]) -> list[str]:
    out: list[str] = []
    resim_lines = [ln for ln in lines if RESIM_COMPLETE_RE.search(ln)]
    post_live = [ln for ln in lines if POST_RESIM_LIVE_RE.search(ln)]
    stall_lines = [ln for ln in lines if STRICT_STALL_RE.search(ln)]
    r_fc: dict[str, int] = {}
    for ln in lines:
        m = FRAME_COMMIT_DIAG_RE.search(ln)
        if m is not None and m.group(2) == "R":
            tick_s = m.group(1)
            r_fc[tick_s] = r_fc.get(tick_s, 0) + 1
    if resim_lines:
        out.append(f"    resim episodes: {len(resim_lines)} (last: {resim_lines[-1].strip()[:120]})")
    if post_live:
        m = POST_RESIM_LIVE_RE.search(post_live[-1])
        if m is not None:
            out.append(
                f"    last POST_RESIM_LIVE: sim={m.group(1)} target={m.group(2)} "
                f"hr={m.group(3)} next_wire={m.group(4)} wire_gap={m.group(5)}"
            )
    if stall_lines:
        out.append(f"    strict_stall_diag lines: {len(stall_lines)}")
    if r_fc:
        worst_tick, worst_n = max(r_fc.items(), key=lambda kv: kv[1])
        out.append(f"    max R-stall frame_commit_diag: tick={worst_tick} count={worst_n}")
    out.extend(collect_hash_drift_summary(lines))
    return out


@dataclass
class LabeledLog:
    label: str
    path: Path
    lines_read: int = 0
    lines_kept: int = 0
    kept: list[str] = field(default_factory=list)
    collapse_stats: dict[str, int] = field(default_factory=dict)
    stall_summary_lines: list[str] = field(default_factory=list)


def compile_patterns(patterns: list[str]) -> list[re.Pattern[str]]:
    return [re.compile(p) for p in patterns]


def line_tick(line: str) -> int | None:
    m = TICK_RE.search(line)
    if m is None:
        return None
    try:
        return int(m.group(1))
    except ValueError:
        return None


def should_keep(
    line: str,
    include: list[re.Pattern[str]],
    exclude: list[re.Pattern[str]],
    tick_min: int | None,
    tick_max: int | None,
) -> bool:
    for pat in exclude:
        if pat.search(line):
            return False
    if not any(pat.search(line) for pat in include):
        return False
    tick = line_tick(line)
    if tick is not None and (tick_min is not None or tick_max is not None):
        if tick_min is not None and tick < tick_min:
            return False
        if tick_max is not None and tick > tick_max:
            return False
    return True


def parse_sim_state_fields(line: str) -> dict[str, str] | None:
    if "sim_state_tick" not in line:
        return None
    return dict(FIELD_RE.findall(line))


def parse_kv_tail(tail: str) -> dict[str, str]:
    return dict(FIELD_RE.findall(tail))


def collect_death_rebirth_sim(lines: list[str]) -> dict[tuple[str, str], dict[str, str]]:
    out: dict[tuple[str, str], dict[str, str]] = {}
    for ln in lines:
        m = DEATH_REBIRTH_SIM_RE.search(ln)
        if m is None:
            continue
        tick_s, player_s, tail = m.group(1), m.group(2), m.group(3)
        fields = parse_kv_tail(tail)
        fields["tick"] = tick_s
        fields["player"] = player_s
        out[(tick_s, player_s)] = fields
    return out


def collect_rebirth_gate_events(lines: list[str]) -> list[tuple[str, str, str, dict[str, str]]]:
    out: list[tuple[str, str, str, dict[str, str]]] = []
    for ln in lines:
        m = REBIRTH_GATE_RE.search(ln)
        if m is None:
            continue
        tick_s, event, player_s, tail = m.group(1), m.group(2), m.group(3), m.group(4)
        fields = parse_kv_tail(tail)
        out.append((tick_s, event, player_s, fields))
    return out


def collect_fighter_field_diff(lines: list[str]) -> dict[tuple[str, str, str], list[dict[str, str]]]:
    """Group load_drift field diffs by (tag, tick, player)."""
    out: dict[tuple[str, str, str], list[dict[str, str]]] = {}
    for ln in lines:
        m = FIGHTER_FIELD_DIFF_RE.search(ln)
        if m is None:
            continue
        tag, tick_s, player_s, tail = m.group(1), m.group(2), m.group(3), m.group(4)
        key = (tag, tick_s, player_s)
        fields = parse_kv_tail(tail)
        out.setdefault(key, []).append(fields)
    return out


def diff_death_rebirth_sim(log_a: LabeledLog, log_b: LabeledLog) -> list[str]:
    by_a = collect_death_rebirth_sim(log_a.kept)
    by_b = collect_death_rebirth_sim(log_b.kept)
    out: list[str] = []
    out.append(
        f"=== death_rebirth_sim diff ({log_a.label} vs {log_b.label}) ==="
    )
    out.append(f"  [{log_a.label}] rows={len(by_a)}  [{log_b.label}] rows={len(by_b)}")
    common_keys = sorted(set(by_a.keys()) & set(by_b.keys()), key=lambda k: (int(k[0]), int(k[1])))
    skip_keys = {"tick", "player"}
    for tick_s, player_s in common_keys:
        fa, fb = by_a[(tick_s, player_s)], by_b[(tick_s, player_s)]
        all_keys = sorted(set(fa.keys()) | set(fb.keys()))
        diffs = [k for k in all_keys if k not in skip_keys and fa.get(k) != fb.get(k)]
        if diffs:
            out.append(
                f"  first mismatch tick={tick_s} player={player_s} fields={','.join(diffs)}"
            )
            for k in diffs:
                out.append(f"    {k}: {log_a.label}={fa.get(k)} {log_b.label}={fb.get(k)}")
            break
    else:
        only_a = sorted(set(by_a.keys()) - set(by_b.keys()), key=lambda k: (int(k[0]), int(k[1])))
        only_b = sorted(set(by_b.keys()) - set(by_a.keys()), key=lambda k: (int(k[0]), int(k[1])))
        if only_a:
            t, p = only_a[0]
            out.append(f"  no field mismatch; first row only in {log_a.label}: tick={t} player={p}")
        elif only_b:
            t, p = only_b[0]
            out.append(f"  no field mismatch; first row only in {log_b.label}: tick={t} player={p}")
        elif not common_keys:
            out.append("  no death_rebirth_sim rows in either log (enable SSB64_NETPLAY_DEATH_REBIRTH_SIM_DIAG=1)")
        else:
            out.append("  no field mismatch on overlapping death_rebirth_sim rows")
    out.append("")
    return out


def diff_rebirth_gate(log_a: LabeledLog, log_b: LabeledLog) -> list[str]:
    ev_a = collect_rebirth_gate_events(log_a.kept)
    ev_b = collect_rebirth_gate_events(log_b.kept)
    out: list[str] = []
    out.append(f"=== REBIRTH_GATE diff ({log_a.label} vs {log_b.label}) ===")
    out.append(f"  [{log_a.label}] events={len(ev_a)}  [{log_b.label}] events={len(ev_b)}")
    key_a = {(t, e, p): f for t, e, p, f in ev_a}
    key_b = {(t, e, p): f for t, e, p, f in ev_b}
    common = sorted(set(key_a.keys()) & set(key_b.keys()), key=lambda k: (int(k[0]), k[1], int(k[2])))
    for tick_s, event, player_s in common:
        fa, fb = key_a[(tick_s, event, player_s)], key_b[(tick_s, event, player_s)]
        all_keys = sorted(set(fa.keys()) | set(fb.keys()))
        diffs = [k for k in all_keys if fa.get(k) != fb.get(k)]
        if diffs:
            out.append(
                f"  first mismatch tick={tick_s} event={event} player={player_s} fields={','.join(diffs)}"
            )
            for k in diffs:
                out.append(f"    {k}: {log_a.label}={fa.get(k)} {log_b.label}={fb.get(k)}")
            break
    else:
        only_a = sorted(set(key_a.keys()) - set(key_b.keys()), key=lambda k: (int(k[0]), k[1], int(k[2])))
        only_b = sorted(set(key_b.keys()) - set(key_a.keys()), key=lambda k: (int(k[0]), k[1], int(k[2])))
        if only_a:
            t, e, p = only_a[0]
            out.append(f"  no field mismatch; first event only in {log_a.label}: tick={t} event={e} player={p}")
        elif only_b:
            t, e, p = only_b[0]
            out.append(f"  no field mismatch; first event only in {log_b.label}: tick={t} event={e} player={p}")
        elif not common:
            out.append("  no REBIRTH_GATE rows in either log (enable SSB64_NETPLAY_REBIRTH_GATE_DIAG=1)")
        else:
            out.append("  no field mismatch on overlapping REBIRTH_GATE events")
    out.append("")
    return out


def build_summary(logs: list[LabeledLog]) -> list[str]:
    out: list[str] = []
    out.append("=== netplay-trim-logs summary ===")
    for lg in logs:
        out.append(
            f"  [{lg.label}] {lg.path}: read={lg.lines_read} kept={lg.lines_kept}"
        )
        banners = [ln for ln in lg.kept if "cross_isa_session" in ln]
        for ln in banners[:2]:
            out.append(f"    banner: {ln.strip()}")
        prev_eff: str | None = None
        for ln in lg.kept:
            fields = parse_sim_state_fields(ln)
            if fields is None:
                continue
            eff = fields.get("eff")
            tick = fields.get("tick")
            if eff is not None and prev_eff is not None and eff != prev_eff:
                out.append(f"    first eff change: tick={tick} eff {prev_eff} -> {eff}")
                break
            if eff is not None:
                prev_eff = eff
        prev_count: str | None = None
        for ln in lg.kept:
            m = EFFECT_SAVE_RE.search(ln)
            if m is None:
                continue
            tick_s, count = m.group(1), m.group(2)
            if prev_count is not None and count != prev_count:
                out.append(
                    f"    effect_count transition: tick={tick_s} "
                    f"{prev_count} -> {count}"
                )
                break
            prev_count = count
        prev_item: str | None = None
        for ln in lg.kept:
            m = ITEM_SAVE_RE.search(ln)
            if m is None:
                continue
            tick_s, count = m.group(1), m.group(2)
            if tick_s == "4294967295":
                continue
            if prev_item is not None and count != prev_item:
                out.append(
                    f"    item_count transition: tick={tick_s} "
                    f"{prev_item} -> {count}"
                )
                break
            prev_item = count
        dr_count = sum(1 for ln in lg.kept if "death_rebirth_sim" in ln)
        if dr_count:
            out.append(f"    death_rebirth_sim rows: {dr_count}")
        rg_count = sum(1 for ln in lg.kept if "REBIRTH_GATE" in ln)
        if rg_count:
            out.append(f"    REBIRTH_GATE events: {rg_count}")
        out.extend(collect_netstatusvars_summary(lg.kept))
        stall_summary = lg.stall_summary_lines or collect_rollback_stall_summary(lg.kept)
        out.extend(stall_summary)
        ring_summary = collect_ring_save_summary(lg.kept)
        out.extend(ring_summary)
        gate_summary = collect_yamabuki_gate_summary(lg.kept)
        out.extend(gate_summary)
        out.extend(collect_yamabuki_gate_node_summary(lg.kept))
        hitokage_summary = collect_yamabuki_hitokage_summary(lg.kept)
        out.extend(hitokage_summary)
        out.extend(collect_yamabuki_synctest_skip_summary(lg.kept))
        out.extend(collect_hyrule_twister_summary(lg.kept))
        out.extend(collect_ness_pkthunder_summary(lg.kept))
        out.extend(collect_gobj_link_audit_summary(lg.kept))
        tra_i_count = sum(1 for ln in lg.kept if GATE_ANIM_TRA_I_RE.search(ln))
        if tra_i_count:
            out.append(f"    gate TraI apply trace rows: {tra_i_count} (SSB64: gcPlayDObjAnimJoint)")
        if (
            lg.collapse_stats.get("r_stall_collapsed", 0)
            or lg.collapse_stats.get("sim_frozen_collapsed", 0)
            or lg.collapse_stats.get("ring_save_collapsed", 0)
            or lg.collapse_stats.get("yamabuki_gate_collapsed", 0)
            or lg.collapse_stats.get("yamabuki_gate_node_collapsed", 0)
            or lg.collapse_stats.get("yamabuki_hitokage_collapsed", 0)
            or lg.collapse_stats.get("ness_pkthunder_stall_collapsed", 0)
            or lg.collapse_stats.get("netstatusvars_collapsed", 0)
        ):
            out.append(
                f"    collapsed repeats: r_stall={lg.collapse_stats.get('r_stall_collapsed', 0)} "
                f"sim_frozen={lg.collapse_stats.get('sim_frozen_collapsed', 0)} "
                f"ring_save={lg.collapse_stats.get('ring_save_collapsed', 0)} "
                f"yamabuki_gate={lg.collapse_stats.get('yamabuki_gate_collapsed', 0)} "
                f"yamabuki_gate_node={lg.collapse_stats.get('yamabuki_gate_node_collapsed', 0)} "
                f"yamabuki_hitokage={lg.collapse_stats.get('yamabuki_hitokage_collapsed', 0)} "
                f"ness_pkthunder_stall={lg.collapse_stats.get('ness_pkthunder_stall_collapsed', 0)} "
                f"netstatusvars={lg.collapse_stats.get('netstatusvars_collapsed', 0)}"
            )
    out.append("")
    return out


def diff_sim_state_ticks(log_a: LabeledLog, log_b: LabeledLog) -> list[str]:
    by_tick_a: dict[str, dict[str, str]] = {}
    by_tick_b: dict[str, dict[str, str]] = {}

    for ln in log_a.kept:
        fields = parse_sim_state_fields(ln)
        if fields and "tick" in fields:
            by_tick_a[fields["tick"]] = fields
    for ln in log_b.kept:
        fields = parse_sim_state_fields(ln)
        if fields and "tick" in fields:
            by_tick_b[fields["tick"]] = fields

    out: list[str] = []
    out.append(
        f"=== sim_state_tick diff ({log_a.label} vs {log_b.label}) ==="
    )
    common = sorted(set(by_tick_a.keys()) & set(by_tick_b.keys()), key=int)
    compare_keys = [
        "figh",
        "eff",
        "anim",
        "cam",
        "item",
        "ahead",
        "hr",
        "commit_gen",
    ]
    item_first: tuple[str, str, str] | None = None
    pacing_first: tuple[str, list[str]] | None = None
    for tick in common:
        fa, fb = by_tick_a[tick], by_tick_b[tick]
        if item_first is None and fa.get("item") != fb.get("item") and "item" in fa and "item" in fb:
            item_first = (tick, fa.get("item", "?"), fb.get("item", "?"))
        if pacing_first is None:
            pacing_diffs = [
                k for k in ("ahead", "hr", "commit_gen") if fa.get(k) != fb.get(k) and k in fa and k in fb
            ]
            if pacing_diffs:
                pacing_first = (tick, pacing_diffs)
        diffs = [
            k for k in compare_keys if fa.get(k) != fb.get(k) and k in fa and k in fb
        ]
        if diffs and item_first is None and pacing_first is None:
            out.append(f"  first mismatch tick={tick} fields={','.join(diffs)}")
            for k in diffs:
                out.append(f"    {k}: {log_a.label}={fa.get(k)} {log_b.label}={fb.get(k)}")
            break
    else:
        if item_first is not None:
            tick, ia, ib = item_first
            out.append(f"  first item mismatch tick={tick}")
            out.append(f"    item: {log_a.label}={ia} {log_b.label}={ib}")
        if pacing_first is not None:
            tick, fields = pacing_first
            fa, fb = by_tick_a[tick], by_tick_b[tick]
            out.append(f"  first pacing skew tick={tick} fields={','.join(fields)}")
            for k in fields:
                out.append(f"    {k}: {log_a.label}={fa.get(k)} {log_b.label}={fb.get(k)}")
        if item_first is None and pacing_first is None:
            out.append("  no field mismatch on overlapping sim_state_tick rows")
    out.append("")
    return out


def process_file(
    label: str,
    path: Path,
    include: list[re.Pattern[str]],
    exclude: list[re.Pattern[str]],
    tick_min: int | None,
    tick_max: int | None,
    dedupe_snapshot_save: bool,
) -> LabeledLog:
    lg = LabeledLog(label=label, path=path)
    prev_snapshot_save: dict[str, str | None] = {p: None for p in SNAPSHOT_SAVE_DEDUPE_PREFIXES}
    with path.open(encoding="utf-8", errors="replace") as f:
        for line in f:
            lg.lines_read += 1
            raw = line.rstrip("\n")
            if not should_keep(raw, include, exclude, tick_min, tick_max):
                continue
            if dedupe_snapshot_save:
                skipped = False
                for prefix in SNAPSHOT_SAVE_DEDUPE_PREFIXES:
                    if raw.startswith(prefix):
                        if raw == prev_snapshot_save[prefix]:
                            skipped = True
                        else:
                            prev_snapshot_save[prefix] = raw
                        break
                if skipped:
                    continue
            lg.kept.append(raw)
            lg.lines_kept += 1
    return lg


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--label",
        nargs=2,
        action="append",
        metavar=("NAME", "PATH"),
        required=True,
        help="Label and path for an input log (repeatable)",
    )
    parser.add_argument("-o", "--output", type=Path, help="Output file path")
    parser.add_argument("--tick-min", type=int, default=None)
    parser.add_argument("--tick-max", type=int, default=None)
    parser.add_argument("--include", action="append", default=[], help="Extra keep regex")
    parser.add_argument("--exclude", action="append", default=[], help="Extra drop regex")
    parser.add_argument("--no-default-filters", action="store_true")
    parser.add_argument(
        "--keep-noisy-traces",
        action="store_true",
        help="Do not exclude joint_translate, ft_phase, fighter_slot_hash, yoster_cloud_fighter, etc.",
    )
    parser.add_argument(
        "--no-collapse-ring-save",
        action="store_true",
        help="Keep every ring_save_diag line (default collapses runs with identical figh_ok/anim_ok/blob_ok)",
    )
    parser.add_argument(
        "--no-collapse-yamabuki-gate",
        action="store_true",
        help="Keep every yamabuki_gate / yamabuki_gate_node line (default collapses runs with identical state)",
    )
    parser.add_argument(
        "--keep-yoster-cloud-fighters",
        action="store_true",
        help="Keep per-tick yoster_cloud_fighter rows (excluded by default when noisy)",
    )
    parser.add_argument(
        "--no-collapse-yamabuki-hitokage",
        action="store_true",
        help="Keep every yamabuki_hitokage line (default collapses runs with identical flame state)",
    )
    parser.add_argument(
        "--keep-gate-anim-trace",
        action="store_true",
        help="Keep SSB64: gcPlayDObjAnimJoint TraI lines (gate mesh apply probe; capped in-game)",
    )
    parser.add_argument("--dedupe-effect-save", action="store_true", help="Deprecated alias for default snapshot dedupe")
    parser.add_argument("--no-dedupe-snapshots", action="store_true", help="Keep consecutive identical snapshot save lines")
    parser.add_argument("--collapse-r-stall", dest="collapse_r_stall", action="store_true", default=True)
    parser.add_argument("--no-collapse-r-stall", dest="collapse_r_stall", action="store_false")
    parser.add_argument("--summary-only", action="store_true")
    parser.add_argument("--diff-ticks", action="store_true")
    parser.add_argument(
        "--diff-death-rebirth",
        action="store_true",
        help="Include death_rebirth_sim + REBIRTH_GATE diffs (default when --diff-ticks)",
    )
    args = parser.parse_args()
    diff_death_rebirth = args.diff_death_rebirth or args.diff_ticks

    include_src = ([] if args.no_default_filters else DEFAULT_INCLUDE) + args.include
    exclude_src = ([] if args.no_default_filters else DEFAULT_EXCLUDE) + args.exclude
    if args.keep_gate_anim_trace:
        include_src.append(r"SSB64: gcPlayDObjAnimJoint - TraI ")
    if args.keep_noisy_traces or args.keep_yoster_cloud_fighters:
        noisy = [
            r"SSB64 NetSync: joint_translate ",
            r"SSB64 NetSync: ft_phase ",
            r"SSB64 NetSync: fighter_slot_hash ",
            r"SSB64 NetSync: validation_dual_hash ",
            r"SSB64 NetSync: role=client ",
            r"SSB64 NetSync: role=host ",
            r"SSB64 NetSync: tick_diag ",
            r"SSB64 NetSync: pub_vs_remote_summary ",
            r"SSB64 NetPeer: cross_os_pacing ",
            r"SSB64 NetPeer: phase_lock_commit ",
            r"SSB64 NetPeer: INPUT send ",
            r"SSB64 NetPeer: INPUT recv ",
            r"SSB64 NetInput: frame_commit_diag tick=\d+ path=P ",
        ]
        if args.keep_yoster_cloud_fighters or args.keep_noisy_traces:
            noisy.append(r"SSB64 NetSync: yoster_cloud_fighter ")
        exclude_src = [p for p in exclude_src if p not in noisy]
    include = compile_patterns(include_src)
    exclude = compile_patterns(exclude_src)

    dedupe_snapshots = not args.no_dedupe_snapshots

    logs: list[LabeledLog] = []
    for label, path_s in args.label:
        path = Path(path_s)
        if not path.is_file():
            print(f"error: not a file: {path}", file=sys.stderr)
            return 2
        logs.append(
            process_file(
                label,
                path,
                include,
                exclude,
                args.tick_min,
                args.tick_max,
                dedupe_snapshots,
            )
        )

    for lg in logs:
        lg.stall_summary_lines = collect_rollback_stall_summary(lg.kept)
        if args.collapse_r_stall:
            lg.kept, lg.collapse_stats = collapse_r_stall_lines(lg.kept)
        if not args.no_collapse_ring_save:
            ring_collapsed: int
            lg.kept, ring_collapsed = collapse_ring_save_diag_lines(lg.kept)
            lg.collapse_stats["ring_save_collapsed"] = ring_collapsed
        if not args.no_collapse_yamabuki_gate:
            gate_collapsed: int
            lg.kept, gate_collapsed = collapse_yamabuki_gate_lines(lg.kept)
            lg.collapse_stats["yamabuki_gate_collapsed"] = gate_collapsed
            node_collapsed: int
            lg.kept, node_collapsed = collapse_yamabuki_gate_node_lines(lg.kept)
            lg.collapse_stats["yamabuki_gate_node_collapsed"] = node_collapsed
        if not args.no_collapse_yamabuki_hitokage:
            hitokage_collapsed: int
            lg.kept, hitokage_collapsed = collapse_yamabuki_hitokage_lines(lg.kept)
            lg.collapse_stats["yamabuki_hitokage_collapsed"] = hitokage_collapsed
        stall_collapsed: int
        lg.kept, stall_collapsed = collapse_ness_pkthunder_stall_lines(lg.kept)
        lg.collapse_stats["ness_pkthunder_stall_collapsed"] = stall_collapsed
        netstatusvars_collapsed: int
        lg.kept, netstatusvars_collapsed = collapse_netstatusvars_lines(lg.kept)
        lg.collapse_stats["netstatusvars_collapsed"] = netstatusvars_collapsed
        lg.lines_kept = len(lg.kept)

    summary = build_summary(logs)
    if args.diff_ticks and len(logs) >= 2:
        summary.extend(diff_sim_state_ticks(logs[0], logs[1]))
    if diff_death_rebirth and len(logs) >= 2:
        summary.extend(diff_death_rebirth_sim(logs[0], logs[1]))
        summary.extend(diff_rebirth_gate(logs[0], logs[1]))

    body: list[str] = []
    if not args.summary_only:
        for lg in logs:
            body.append(f"=== {lg.label}: {lg.path} ===")
            body.extend(lg.kept)
            body.append("")

    out_text = "\n".join(summary + body)
    if args.output:
        args.output.write_text(out_text + "\n", encoding="utf-8")
        print(f"wrote {args.output} ({len(summary)} summary lines, {sum(lg.lines_kept for lg in logs)} kept)")
    else:
        sys.stdout.write(out_text)
        if not out_text.endswith("\n"):
            sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
