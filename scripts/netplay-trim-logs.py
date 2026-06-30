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
  --sync-report       Brief stability report to stdout (hash drift, synctest, resim, pair diff)
  --diff-ticks        Report first sim_state_tick field mismatch between first two inputs

Summary and --sync-report include a pair session check (session_id + bootstrap rng_seed) so
mis-paired host.log/guest.log files from different automatch sessions are obvious before submit.
  --diff-death-rebirth  Report death/rebirth sim + gate diag mismatches (implies --diff-ticks extras)

Keeps effect_xf_stale rows (always-on rate-limited + SSB64_NETPLAY_SNAPSHOT_EFFECT_DIAG=1 verbose), fireball_spawn paths/skips
(WEAPON_DIAG=1), Dream Land Whispy presentation repair rows (SSB64_NETPLAY_WHISPY_REPAIR_DIAG=1),
SIGSEGV/crash backtraces, and GFX stale-DL diag from the crash handler.
Summarizes stale-xf ejects, fireball spawn/latch/retry, orphan guard_shield no_fighter prune spam,
Whispy particle/texture repair (post_verify, forward_texture, xf_alias), guard_shield_load_drift
bisect rows, and LOAD_HASH_DRIFT partition mismatches (figh/anim/map/rng).
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
    # Dream Land Whispy particle + flower presentation repair (SSB64_NETPLAY_WHISPY_REPAIR_DIAG=1).
    r"SSB64 WhispyRepair:",
    # Hyrule Castle tornado / twister rollback diagnostics.
    r"SSB64 NetRbSnapshot: hyrule_twister",
    r"SSB64 NetSync: hyrule_twister",
    # Sector Z Arwing snapshot / restore diagnostics.
    r"SSB64 NetRbSnapshot: sector_arwing",
    # Map hash round-trip decomposition (SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG=1).
    r"SSB64 NetRbSnapshot: map_hash_",
    # Post-apply GObj link census (item/weapon/effect counts after snapshot load).
    r"SSB64 NetRbSnapshot: gobj_link_audit ",
    r"SSB64 NetRbSnapshot:",
    r"SSB64 NetRollback:",
    # Ness PK Thunder throw / jibaku timing (SSB64_NETPLAY_NESS_PKTHUNDER_GATE_DIAG=1).
    r"SSB64 Netplay: NESS_PKTHUNDER_GATE ",
    # FTStatusVars overlay witness (SSB64_NETPLAY_STATUSVARS_WITNESS=1).
    r"SSB64 NetStatusVars:",
    # Samus screw / FallSpecial soft-platform pass (SSB64_NETPLAY_FALLSPECIAL_PASS_DIAG=1).
    r"SSB64 FallSpecialPassDiag:",
    r"SSB64 Netplay:",
    # Kirby stone armor hits (SSB64_NETPLAY_KIRBY_STONE_DAMAGE_DIAG=1).
    r"SSB64 KirbyStone:",
    r"SSB64 Automatch:",
    r"SSB64 ICE:",
    r"SSB64 Matchmaking:",
    r"SSB64 DESYNC",
    r"SSB64 FRAME COMMIT REPORT",
    r"returning to character select",
    # Crash / stale display-list triage (AppImage SIGSEGV + libultraship stale-DL diag).
    r"SSB64: .*SIGSEGV",
    r"SSB64: ---- main-thread backtrace",
    r"SSB64: ---- registers ----",
    r"SSB64: ---- end backtrace ----",
    r"GFX STALE-DL DIAG",
    r"badCmd host=",
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
MM_POLL_MATCHED_RE = re.compile(
    r"SSB64 Automatch: MM_POLL_MATCHED .+ session=(\d+) host=(\d+) ticket=([^\s]+)"
)
INPUT_BIND_ACK_RE = re.compile(
    r"SSB64 NetPeer: input_bind_ack session=(\d+) .+ role=(\w+)"
)
BOOTSTRAP_APPLIED_RE = re.compile(
    r"SSB64 NetPeer: bootstrap metadata applied host=\d+ stage=\d+ seed=(\d+)"
)
MATCH_CONFIG_STAGED_RE = re.compile(
    r"SSB64 NetPeer: automatch MATCH_CONFIG staged stage=\d+ seed=(\d+)"
)
METADATA_COMPOSED_RE = re.compile(
    r"SSB64 NetPeer: automatch metadata composed stage=\d+ seed=(\d+)"
)
HOST_START_RE = re.compile(
    r"SSB64 NetPeer: bootstrap host sent START stage=\d+ seed=(\d+)"
)
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
FORCE_MISMATCH_INJECT_RE = re.compile(
    r"SSB64 NetRollback: (?:debug inject tamper at remote tick|FORCE_MISMATCH on wire tick) (\d+)"
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
RNG_HASH_WALK_BEGIN_RE = re.compile(
    r"SSB64 NetSync: rng_hash_walk begin sim_tick=(\d+) .+ reason=(\S+)"
)
# Unified FRAME_COMMIT_STATE_DIVERGE parse. The line carries EVERY partition for both
# peers as "local figh=.. world=.. item=.. rng=.. eff=.. | peer figh=.. ... inp_local=..
# inp_peer=..". This replaces the older field-specific regexes (item-only, rng-only): each
# independently matched this same line, whichever fired first did a `continue` and
# short-circuited the rest, AND neither checked figh. Result: a real cross-peer figh/item
# divergence reported fc_item_div=0 fc_rng_div=0 (STABLE) whenever rng happened to match.
# Parse the whole line once and compare all partitions.
FRAME_COMMIT_DIVERGE_LINE_RE = re.compile(
    r"FRAME_COMMIT_STATE_DIVERGE\s+validation=(\d+)\s+local\s+(.*?)\s+\|\s+peer\s+(.*)"
)
_FC_PARTITION_RE = re.compile(r"\b(figh|world|item|wpn|map|rng|cam|anim|eff)=0x([0-9A-Fa-f]+)")
_FC_INPUTS_RE = re.compile(r"inp_local=0x([0-9A-Fa-f]+)\s+inp_peer=0x([0-9A-Fa-f]+)")
# Recovery aborted: the deferred resim could not reanchor and gave up — terminal desync.
PEER_BASELINE_RESYNC_STORM_RE = re.compile(
    r"PEER_BASELINE_RESYNC_STORM\b.*?load_tick=(\d+).*?sim=(\d+).*?aborting"
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
WHISPY_REPAIR_RE = re.compile(r"SSB64 WhispyRepair:")
WHISPY_POST_VERIFY_RE = re.compile(
    r"SSB64 WhispyRepair: post_verify tick=(\d+) reason=(\S+)"
)
WHISPY_FORWARD_TEXTURE_RE = re.compile(r"SSB64 WhispyRepair: forward_texture ")
WHISPY_XF_ALIAS_RE = re.compile(r"SSB64 WhispyRepair: xf_alias tick=(\d+)")
GUARD_SHIELD_LOAD_DRIFT_RE = re.compile(
    r"SSB64 NetRbSnapshot: guard_shield_load_drift tick=(\d+) player=(\d+) fkind=(\d+) status=(\d+)"
)
QUAKE_SANITIZE_RE = re.compile(
    r"SSB64 NetRbSnapshot: quake_sanitize ejected=(\d+) tick=(\d+)"
)
LOAD_HASH_DRIFT_PARTITION_RE = re.compile(
    r"SSB64 NetRollback: LOAD_HASH_DRIFT tick=(\d+) figh=0x([0-9A-Fa-f]+)/0x([0-9A-Fa-f]+) "
    r"world=0x([0-9A-Fa-f]+)/0x([0-9A-Fa-f]+) item=0x([0-9A-Fa-f]+)/0x([0-9A-Fa-f]+) "
    r"wpn=0x([0-9A-Fa-f]+)/0x([0-9A-Fa-f]+) map=0x([0-9A-Fa-f]+)/0x([0-9A-Fa-f]+) "
    r"rng=0x([0-9A-Fa-f]+)/0x([0-9A-Fa-f]+) cam=0x([0-9A-Fa-f]+)/0x([0-9A-Fa-f]+) "
    r"anim=0x([0-9A-Fa-f]+)/0x([0-9A-Fa-f]+) eff=0x([0-9A-Fa-f]+)/0x([0-9A-Fa-f]+)"
)
FIGHTER_LOAD_VERIFY_RE = re.compile(
    r"SSB64 NetRbSnapshot: fighter_load_verify tick=(\d+) figh_live=0x([0-9A-Fa-f]+) figh_slot=0x([0-9A-Fa-f]+) "
    r"anim_live=0x([0-9A-Fa-f]+) anim_slot=0x([0-9A-Fa-f]+)"
)
GOBJ_LINK_AUDIT_RE = re.compile(
    r"SSB64 NetRbSnapshot: gobj_link_audit tick=(\d+) f=(\d+) i=(\d+) w=(\d+) ef6=(\d+) ef8=(\d+)"
)
EFFECT_XF_STALE_RE = re.compile(
    r"SSB64 NetRbSnapshot: effect_xf_stale tick=(\d+) proc=(\S+) reason=(\S+) effect_gobj_id=(\d+) "
    r"xf=(\S+) xf_owner=(\S+) particle=(\S+)"
)
GUARD_SHIELD_NO_FIGHTER_RE = re.compile(
    r"SSB64 NetRbSnapshot: guard_shield_prune tick=0 path=eject reason=no_fighter effect_gobj_id=(\d+)"
)
CRASH_SIGSEGV_RE = re.compile(r"SSB64: .*SIGSEGV fault_addr=")
STALE_DL_BAD_CMD_RE = re.compile(r"badCmd host=(\S+) class=(\S+) frame=(\d+)")
FIREBALL_SPAWN_PATH_RE = re.compile(
    r"SSB64 NetRbSnapshot: fireball_spawn path=(\S+) owner_player=(\d+)"
)
FIREBALL_CULL_STALE_RE = re.compile(
    r"SSB64 NetRbSnapshot: fireball_spawn cull_stale owner_player=\d+ path=(\S+)"
)
FIREBALL_SPAWN_SKIP_RE = re.compile(
    r"SSB64 NetRbSnapshot: fireball_spawn skip=(\S+) owner_player=(\d+) status=(\d+)"
)
MAP_HASH_DRIFT_RE = re.compile(
    r"SSB64 NetRbSnapshot: map_hash_drift tick=(\d+) slot_stored=0x([0-9A-Fa-f]+) live_full=0x([0-9A-Fa-f]+) "
    r"kin=0x([0-9A-Fa-f]+) ground_fold_slot=0x([0-9A-Fa-f]+) ground_fold_scratch=0x([0-9A-Fa-f]+) "
    r"hash_kin_plus_slot_ground=0x([0-9A-Fa-f]+) hash_kin_plus_scratch_ground=0x([0-9A-Fa-f]+)"
)
MAP_HASH_GROUND_PAYLOAD_RE = re.compile(
    r"SSB64 NetRbSnapshot: map_hash_ground_payload tick=(\d+) gkind=(\d+) slot_len=(\d+) "
    r"scratch_len=(\d+) match=(\d+) first_off=(-?\d+)"
)
MAP_HASH_YAKU1_RE = re.compile(
    r"SSB64 NetRbSnapshot: map_hash_yaku1 tick=(\d+) live_tx=([-0-9.eE+]+) ty=([-0-9.eE+]+) tz=([-0-9.eE+]+) "
    r"live_sx=([-0-9.eE+]+) sy=([-0-9.eE+]+) sz=([-0-9.eE+]+) blob_tx=([-0-9.eE+]+) ty=([-0-9.eE+]+) "
    r"tz=([-0-9.eE+]+) blob_sx=([-0-9.eE+]+) sy=([-0-9.eE+]+) sz=([-0-9.eE+]+) user_live=(\d+) "
    r"user_blob=(\d+) mp_tic_live=(\d+) mp_tic_slot=(\d+)"
)
MAP_HASH_ARWING_D0_RE = re.compile(
    r"SSB64 NetRbSnapshot: map_hash_arwing_d0 tick=(\d+) live_frame=([-0-9.eE+]+) wait=([-0-9.eE+]+) "
    r"tx=([-0-9.eE+]+) ty=([-0-9.eE+]+) blob_tx=([-0-9.eE+]+) ty=([-0-9.eE+]+) status=(\d+) "
    r"deck_derived=(\d+)"
)
MAP_HASH_SAVE_SELF_TEST_RE = re.compile(
    r"SSB64 NetRbSnapshot: map_hash_save_self_test FAIL tick=(\d+) stored=0x([0-9A-Fa-f]+) "
    r"immediate=0x([0-9A-Fa-f]+)"
)
MAP_HASH_SECTOR_FIELD_RE = re.compile(
    r"SSB64 NetRbSnapshot: map_hash_sector_field tick=(\d+) field=(\S+) slot=(\S+) live=(\S+)"
)
GATE_ANIM_TRA_I_RE = re.compile(r"SSB64: gcPlayDObjAnimJoint - TraI ")
# Ness PK Thunder / jibaku gate (SSB64_NETPLAY_NESS_PKTHUNDER_GATE_DIAG=1).
NESS_PKTHUNDER_GATE_RE = re.compile(
    r"SSB64 Netplay: NESS_PKTHUNDER_GATE tick=(\d+) event=(\S+)"
)
SYNCTEST_OK_RE = re.compile(r"SSB64 NetRollback: SYNCTEST_OK tick=(\d+)")
SYNCTEST_SKIP_ANY_RE = re.compile(r"SSB64 NetRollback: SYNCTEST_SKIP tick=(\d+)")
LOAD_HASH_DRIFT_ANY_RE = re.compile(r"SSB64 NetRollback: LOAD_HASH_DRIFT tick=(\d+)")
LOAD_HASH_ABORT_RE = re.compile(
    r"SSB64 NetRollback: LOAD_HASH_DRIFT — restoring live world"
)
PEER_SNAPSHOT_DIVERGE_RE = re.compile(
    r"SSB64 NetRollback: PEER_SNAPSHOT_DIVERGE — stopping VS session"
)
RESIM_SIM_CORE_REJECT_RE = re.compile(
    r"SSB64 NetRollback: resim-sim-core-reject tick=(\d+)"
)
RESIM_SIM_CORE_REJECT_NOT_IN_RESIM_RE = re.compile(
    r"SSB64 NetRollback: resim-sim-core-reject tick=\d+ reason=not_in_resim\b"
)
DESYNC_REPORT_RE = re.compile(r"SSB64 DESYNC REPORT")
CSS_RETURN_RE = re.compile(r"returning to character select", re.I)
VS_SESSION_STOP_RE = re.compile(r"SSB64 NetPeer: VS session stop ")
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


def collect_whispy_repair_summary(lines: list[str]) -> list[str]:
    """Summarize Dream Land Whispy rollback presentation repair (SSB64_NETPLAY_WHISPY_REPAIR_DIAG=1)."""
    rows = [ln for ln in lines if WHISPY_REPAIR_RE.search(ln)]
    if not rows:
        return []
    tags: dict[str, int] = {}
    spawned_leaves = 0
    spawned_dust = 0
    zero_drawable = 0
    post_verify_reasons: dict[str, int] = {}
    xf_alias_ticks: list[str] = []
    for ln in rows:
        if "post_verify" in ln:
            tags["post_verify"] = tags.get("post_verify", 0) + 1
            m = WHISPY_POST_VERIFY_RE.search(ln)
            if m is not None:
                post_verify_reasons[m.group(2)] = post_verify_reasons.get(m.group(2), 0) + 1
        elif "forward_texture" in ln:
            tags["forward_texture"] = tags.get("forward_texture", 0) + 1
        elif "xf_alias" in ln:
            tags["xf_alias"] = tags.get("xf_alias", 0) + 1
            m = WHISPY_XF_ALIAS_RE.search(ln)
            if m is not None:
                xf_alias_ticks.append(m.group(1))
        elif "forward_tick" in ln:
            tags["forward_tick"] = tags.get("forward_tick", 0) + 1
        elif "flower_reseed" in ln:
            tags["flower_reseed"] = tags.get("flower_reseed", 0) + 1
        elif "presentation" in ln:
            tags["presentation"] = tags.get("presentation", 0) + 1
        else:
            tags["rollback_repair"] = tags.get("rollback_repair", 0) + 1
        if "spawned_leaves=1" in ln:
            spawned_leaves += 1
        if "spawned_dust=1" in ln:
            spawned_dust += 1
        if "leaves_drawable=0" in ln or "dust_drawable=0" in ln:
            zero_drawable += 1
    out = [
        f"    whispy_repair rows: {len(rows)} by_kind={tags} "
        f"spawned_leaves={spawned_leaves} spawned_dust={spawned_dust}"
    ]
    if post_verify_reasons:
        out.append(f"    whispy_repair post_verify by_reason={post_verify_reasons}")
    if xf_alias_ticks:
        ticks = sorted(set(xf_alias_ticks), key=int)
        out.append(
            f"    whispy_repair xf_alias ticks: {','.join(ticks[:8])}"
            + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
        )
    if zero_drawable:
        out.append(f"    whispy_repair zero_drawable rows: {zero_drawable}")
    return out


def collect_quake_sanitize_summary(lines: list[str]) -> list[str]:
    """Summarize hollow quake ejects after synctest emergency restore."""
    rows = [ln for ln in lines if QUAKE_SANITIZE_RE.search(ln)]
    if not rows:
        return []
    total_ejected = 0
    ticks: list[str] = []
    for ln in rows:
        m = QUAKE_SANITIZE_RE.search(ln)
        if m is not None:
            total_ejected += int(m.group(1))
            ticks.append(m.group(2))
    tick_s = ",".join(sorted(set(ticks), key=int)[:8])
    extra = f" (+{len(set(ticks)) - 8} more ticks)" if len(set(ticks)) > 8 else ""
    return [f"    quake_sanitize rows: {len(rows)} total_ejected={total_ejected} ticks={tick_s}{extra}"]


def collect_guard_shield_load_drift_summary(lines: list[str]) -> list[str]:
    """Summarize guard+shield load-hash drift rows (always logged on figh/anim verify mismatch)."""
    rows: list[tuple[str, str, str, str]] = []
    for ln in lines:
        m = GUARD_SHIELD_LOAD_DRIFT_RE.search(ln)
        if m is not None:
            rows.append((m.group(1), m.group(2), m.group(3), m.group(4)))
    if not rows:
        return []
    ticks = sorted({r[0] for r in rows}, key=int)
    players = sorted({r[1] for r in rows}, key=int)
    statuses = sorted({r[3] for r in rows}, key=int)
    return [
        f"    guard_shield_load_drift rows: {len(rows)} ticks={','.join(ticks[:8])}"
        + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
        + f" players={','.join(players)} statuses={','.join(statuses)}"
    ]


def collect_load_hash_partition_summary(lines: list[str]) -> list[str]:
    """Classify which hash partitions mismatch on each LOAD_HASH_DRIFT line."""
    episodes: list[tuple[str, list[str]]] = []
    for ln in lines:
        m = LOAD_HASH_DRIFT_PARTITION_RE.search(ln)
        if m is None:
            continue
        tick = m.group(1)
        parts = [
            ("figh", m.group(2), m.group(3)),
            ("world", m.group(4), m.group(5)),
            ("item", m.group(6), m.group(7)),
            ("wpn", m.group(8), m.group(9)),
            ("map", m.group(10), m.group(11)),
            ("rng", m.group(12), m.group(13)),
            ("cam", m.group(14), m.group(15)),
            ("anim", m.group(16), m.group(17)),
            ("eff", m.group(18), m.group(19)),
        ]
        mismatches = [name for name, slot, live in parts if slot.lower() != live.lower()]
        if mismatches:
            episodes.append((tick, mismatches))
    if not episodes:
        return []
    by_combo: dict[str, int] = {}
    for _, mismatches in episodes:
        key = "+".join(mismatches)
        by_combo[key] = by_combo.get(key, 0) + 1
    ticks = sorted({t for t, _ in episodes}, key=int)
    out = [
        f"    load_hash_partition mismatches: {len(episodes)} by_combo={by_combo}",
        f"    load_hash_partition first_ticks: {','.join(ticks[:8])}"
        + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else ""),
    ]
    return out


def collect_fighter_anim_drift_summary(lines: list[str]) -> list[str]:
    """Summarize fighter_load_verify rows where anim or figh slot/live diverge."""
    rows: list[tuple[str, bool, bool]] = []
    for ln in lines:
        m = FIGHTER_LOAD_VERIFY_RE.search(ln)
        if m is None:
            continue
        tick = m.group(1)
        figh_mm = m.group(2).lower() != m.group(3).lower()
        anim_mm = m.group(4).lower() != m.group(5).lower()
        if figh_mm or anim_mm:
            rows.append((tick, figh_mm, anim_mm))
    if not rows:
        return []
    figh_only = sum(1 for _, f, a in rows if f and not a)
    anim_only = sum(1 for _, f, a in rows if a and not f)
    both = sum(1 for _, f, a in rows if f and a)
    ticks = sorted({t for t, _, _ in rows}, key=int)
    return [
        f"    fighter_load_verify drift: {len(rows)} figh_only={figh_only} anim_only={anim_only} both={both} "
        f"ticks={','.join(ticks[:8])}" + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
    ]


def parse_netstatusvars_line(line: str) -> tuple[str, str, dict[str, str]] | None:
    """Parse SSB64 NetStatusVars witness / corruption diagnostics."""
    m = NETSTATUSVARS_RE.search(line)
    if m is None:
        return None
    # The 2-token capture can grab a trailing key=value for single-word events
    # (e.g. "airveltransn_nan tick=522"); keep only the leading non-field tokens.
    event = " ".join(tok for tok in m.group(1).split() if "=" not in tok)
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

    # JumpAerial NaN family: SetStatus entry snapshot + first per-tick non-finite overlay/vel.
    entries = [e for e in events if e[1] == "jumpaerial entry"]
    if entries:
        ft, _ev, ff = entries[0]
        out.append(
            f"    NetStatusVars jumpaerial entry: {len(entries)} first tick={ft} "
            f"player={ff.get('player', '?')} fkind={ff.get('fkind', '?')} "
            f"vel_air={ff.get('vel_air', '?')} drift={ff.get('drift', '?')} vel_x={ff.get('vel_x', '?')}"
        )
    jumpaerial_corrupt = [c for c in corrupt if c[2].get("kind") == "jumpaerial"]
    if jumpaerial_corrupt:
        ft, _ev, ff = jumpaerial_corrupt[0]
        out.append(
            f"    NetStatusVars corrupt jumpaerial: {len(jumpaerial_corrupt)} first tick={ft} "
            f"player={ff.get('player', '?')} drift={ff.get('drift', '?')} "
            f"vel_air={ff.get('vel_air', '?')} (bit patterns expose cross-ISA +nan/-nan split)"
        )

    # ftPhysicsGetAirVelTransN input dump — the producing math for the JumpAerial NaN.
    airveltransn = [e for e in events if e[1] == "airveltransn_nan"]
    if airveltransn:
        ft, _ev, ff = airveltransn[0]
        out.append(
            f"    NetStatusVars airveltransn_nan: {len(airveltransn)} first tick={ft} "
            f"player={ff.get('player', '?')} out={ff.get('out', '?')} rot_z={ff.get('rot_z', '?')} "
            f"transn_t={ff.get('transn_t', '?')} topn_s={ff.get('topn_s', '?')} "
            f"anim_vel={ff.get('anim_vel', '?')} cos={ff.get('cos', '?')} sin={ff.get('sin', '?')}"
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


def _ness_pkthunder_parse_xy_pair(raw: str) -> tuple[float, float] | None:
    """Parse dist=(x,y) or dist_root=(x,y) tail values from gate logs."""
    if not raw:
        return None
    s = raw.strip()
    if s.startswith("(") and s.endswith(")"):
        s = s[1:-1]
    parts = s.split(",", 1)
    if len(parts) != 2:
        return None
    try:
        return float(parts[0]), float(parts[1])
    except ValueError:
        return None


def _ness_pkthunder_note_vertical_plunge_suspect(
    suspects: list[str],
    tick_s: str,
    fields: dict[str, str],
    event: str,
    dist_key: str,
) -> None:
    """Flag near-vertical jibaku launch (|dy| at collide box edge, tiny dx)."""
    pair = _ness_pkthunder_parse_xy_pair(fields.get(dist_key, ""))
    if pair is None:
        return
    dx, dy = pair
    if abs(dy) >= 350.0 and abs(dx) < 50.0:
        suspects.append(
            f"vertical_plunge tick={tick_s} player={fields.get('player', '?')} {event} "
            f"{dist_key}=({dx:.1f},{dy:.1f})"
        )


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
    jibaku_collides: list[tuple[str, str, dict[str, str]]] = []
    jibaku_launch_dists: list[tuple[str, str, dict[str, str]]] = []
    jibaku_pre_culls: list[tuple[str, str, dict[str, str]]] = []
    jibaku_coupling_rows: list[tuple[str, str, dict[str, str]]] = []
    pk_trail_culls: list[tuple[str, str, dict[str, str]]] = []
    ground_snap_blocked: list[tuple[str, str, dict[str, str]]] = []
    ground_snaps: list[tuple[str, str, dict[str, str]]] = []
    post_culls: list[tuple[str, str, dict[str, str]]] = []
    post_finishes: list[tuple[str, str, dict[str, str]]] = []
    weapon_states: list[tuple[str, str, dict[str, str]]] = []
    sanitize_gravity_rows: list[tuple[str, str, dict[str, str]]] = []
    sanitize_delay_rows: list[tuple[str, str, dict[str, str]]] = []
    fighter_nans: list[tuple[str, str, dict[str, str]]] = []
    suspects: list[str] = []

    for tick_s, event, fields in events:
        by_event[event] = by_event.get(event, 0) + 1
        resim = fields.get("resim", "0") == "1"
        if event == "fighter_nan":
            fighter_nans.append((tick_s, event, fields))
        elif event == "hold_enter":
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
        elif event == "jibaku_collide":
            jibaku_collides.append((tick_s, event, fields))
            _ness_pkthunder_note_vertical_plunge_suspect(suspects, tick_s, fields, "jibaku_collide", "dist_root")
        elif event == "jibaku_launch_dist":
            jibaku_launch_dists.append((tick_s, event, fields))
            _ness_pkthunder_note_vertical_plunge_suspect(suspects, tick_s, fields, "jibaku_launch_dist", "dist")
        elif event == "jibaku_pre_collide_cull":
            jibaku_pre_culls.append((tick_s, event, fields))
        elif event == "jibaku_coupling":
            jibaku_coupling_rows.append((tick_s, event, fields))
            _ness_pkthunder_note_vertical_plunge_suspect(suspects, tick_s, fields, "jibaku_coupling", "dist")
        elif event == "pk_trail_cull":
            pk_trail_culls.append((tick_s, event, fields))
            try:
                after_n = int(fields.get("weapons_after", "0"))
                if after_n <= 1 and int(fields.get("weapons_before", "0")) > 1:
                    suspects.append(
                        f"head_only_pk tick={tick_s} player={fields.get('player', '?')} site={fields.get('site', '?')} "
                        f"before={fields.get('weapons_before', '?')} after={after_n}"
                    )
            except ValueError:
                pass
        elif event == "air_jibaku_ground_snap_blocked":
            ground_snap_blocked.append((tick_s, event, fields))
        elif event == "air_jibaku_ground_snap":
            ground_snaps.append((tick_s, event, fields))
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
    if fighter_nans:
        ft, _ev, ff = fighter_nans[0]
        out.append(
            f"    SUSPECT fighter_nan: {len(fighter_nans)} first tick={ft} "
            f"player={ff.get('player', '?')} site={ff.get('site', '?')} bad={ff.get('bad', '?')} "
            f"translate={ff.get('translate', '?')} vel_air={ff.get('vel_air', '?')} "
            f"(downstream of the JumpAerial airveltransn_nan; ±nan sign diverges per ISA)"
        )
    hold_ticks = by_event.get("hold_tick", 0)
    jibaku_couplings = by_event.get("jibaku_coupling", 0)
    jibaku_collide_n = by_event.get("jibaku_collide", 0)
    jibaku_launch_n = by_event.get("jibaku_launch_dist", 0)
    procmap_defers = by_event.get("air_jibaku_procmap_defer", 0)
    ground_snap_n = by_event.get("air_jibaku_ground_snap", 0)
    ground_snap_blocked_n = by_event.get("air_jibaku_ground_snap_blocked", 0)
    pk_trail_cull_n = by_event.get("pk_trail_cull", 0)
    out.append(
        f"    hold_enter: {len(holds)}  hold_tick: {hold_ticks}  jibaku_trigger: {len(jibakus)}  "
        f"jibaku_collide: {jibaku_collide_n}  jibaku_launch_dist: {jibaku_launch_n}  "
        f"jibaku_coupling: {jibaku_couplings}  procmap_defer: {procmap_defers}  jibaku_phase: {len(jibaku_phases)}"
    )
    out.append(
        f"    ground_snap: {ground_snap_n}  ground_snap_blocked: {ground_snap_blocked_n}  "
        f"pk_trail_cull: {pk_trail_cull_n}"
    )
    if ground_snaps:
        gs_samples: list[str] = []
        for tick_s, _event, fields in ground_snaps[:6]:
            gs_samples.append(
                f"tick={tick_s} p={fields.get('player', '?')} source={fields.get('source', '?')} "
                f"vel_air=({fields.get('vel_air', '?')}) on_floor={fields.get('on_floor', '?')}"
            )
        out.append(f"    ground_snap samples: {'; '.join(gs_samples)}")
    if ground_snap_blocked:
        blocked_by_reason: dict[str, int] = {}
        for _tick_s, _event, fields in ground_snap_blocked:
            reason = fields.get("reason", "?")
            blocked_by_reason[reason] = blocked_by_reason.get(reason, 0) + 1
        out.append(f"    ground_snap_blocked by_reason: {blocked_by_reason}")
        gb_samples: list[str] = []
        for tick_s, _event, fields in ground_snap_blocked[:6]:
            gb_samples.append(
                f"tick={tick_s} p={fields.get('player', '?')} reason={fields.get('reason', '?')} "
                f"vel_air=({fields.get('vel_air', '?')}) floor_flags=0x{fields.get('floor_flags', '?')}"
            )
        out.append(f"    ground_snap_blocked samples: {'; '.join(gb_samples)}")
    if pk_trail_culls:
        pt_samples: list[str] = []
        for tick_s, _event, fields in pk_trail_culls[:8]:
            pt_samples.append(
                f"tick={tick_s} p={fields.get('player', '?')} site={fields.get('site', '?')} "
                f"before={fields.get('weapons_before', '?')} after={fields.get('weapons_after', '?')}"
            )
        out.append(f"    pk_trail_cull samples: {'; '.join(pt_samples)}")
    if jibaku_pre_culls:
        out.append(f"    jibaku_pre_collide_cull: {len(jibaku_pre_culls)}")
        pc_samples: list[str] = []
        for tick_s, _event, fields in jibaku_pre_culls[:6]:
            pc_samples.append(
                f"tick={tick_s} p={fields.get('player', '?')} before={fields.get('weapons_before', '?')} "
                f"after={fields.get('weapons_after', '?')}"
            )
        out.append(f"    jibaku_pre_collide_cull samples: {'; '.join(pc_samples)}")
    if jibaku_collides:
        jc_samples: list[str] = []
        for tick_s, _event, fields in jibaku_collides[:6]:
            jc_samples.append(
                f"tick={tick_s} p={fields.get('player', '?')} dist_root={fields.get('dist_root', '?')} "
                f"dist_topn={fields.get('dist_topn', '?')} head={fields.get('head', '?')}"
            )
        out.append(f"    jibaku_collide samples: {'; '.join(jc_samples)}")
    if jibaku_launch_dists:
        jl_samples: list[str] = []
        for tick_s, _event, fields in jibaku_launch_dists[:6]:
            jl_samples.append(
                f"tick={tick_s} p={fields.get('player', '?')} dist={fields.get('dist', '?')} "
                f"vel_air=({fields.get('vel_air', '?')}) src={fields.get('launch_src', '?')}"
            )
        out.append(f"    jibaku_launch_dist samples: {'; '.join(jl_samples)}")
    if jibaku_coupling_rows:
        cp_samples: list[str] = []
        for tick_s, _event, fields in jibaku_coupling_rows[:6]:
            cp_samples.append(
                f"tick={tick_s} p={fields.get('player', '?')} site={fields.get('site', '?')} "
                f"dist={fields.get('dist', '?')} vel_air=({fields.get('vel_air', '?')})"
            )
        out.append(f"    jibaku_coupling samples: {'; '.join(cp_samples)}")
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
    plunge = [s for s in suspects if s.startswith("vertical_plunge ")]
    head_only = [s for s in suspects if s.startswith("head_only_pk ")]
    other_suspects = [
        s for s in suspects if not s.startswith("vertical_plunge ") and not s.startswith("head_only_pk ")
    ]
    if head_only:
        out.append(f"    SUSPECT head_only_pk trail cull: {len(head_only)}")
        for s in head_only[:8]:
            out.append(f"      {s}")
        if len(head_only) > 8:
            out.append(f"      ... +{len(head_only) - 8} more")
    if plunge:
        out.append(f"    SUSPECT vertical_plunge jibaku: {len(plunge)}")
        for s in plunge[:8]:
            out.append(f"      {s}")
        if len(plunge) > 8:
            out.append(f"      ... +{len(plunge) - 8} more")
    if other_suspects:
        out.append(f"    SUSPECT instant/early jibaku: {len(other_suspects)}")
        for s in other_suspects[:8]:
            out.append(f"      {s}")
        if len(other_suspects) > 8:
            out.append(f"      ... +{len(other_suspects) - 8} more")
    return out


def collect_effect_xf_stale_summary(lines: list[str]) -> list[str]:
    """Summarize stale LBTransform ejects from particle-backed effect procs (SNAPSHOT_EFFECT_DIAG=1)."""
    rows: list[tuple[str, ...]] = []
    for ln in lines:
        m = EFFECT_XF_STALE_RE.search(ln)
        if m is not None:
            rows.append(m.groups())
    if not rows:
        return []
    out: list[str] = []
    by_reason: dict[str, int] = {}
    by_proc: dict[str, int] = {}
    for r in rows:
        by_proc[r[1]] = by_proc.get(r[1], 0) + 1
        by_reason[r[2]] = by_reason.get(r[2], 0) + 1
    first = rows[0]
    out.append(
        f"    effect_xf_stale rows: {len(rows)} by_proc={by_proc} by_reason={by_reason} "
        f"first tick={first[0]} proc={first[1]} reason={first[2]} gobj_id={first[3]}"
    )
    owner_mismatch = [r for r in rows if r[2] == "owner_mismatch"]
    if owner_mismatch:
        out.append(
            f"    effect_xf_stale owner_mismatch: {len(owner_mismatch)} "
            f"(UAF — xf likely recycled into Gfx DL memory)"
        )
    return out


def collect_fireball_spawn_summary(lines: list[str]) -> list[str]:
    """Summarize Mario/Luigi fireball spawn helper (SSB64_NETPLAY_SNAPSHOT_WEAPON_DIAG=1)."""
    by_path: dict[str, int] = {}
    by_skip: dict[str, int] = {}
    cull_stale = 0
    for ln in lines:
        m = FIREBALL_SPAWN_PATH_RE.search(ln)
        if m is not None:
            by_path[m.group(1)] = by_path.get(m.group(1), 0) + 1
            continue
        if FIREBALL_CULL_STALE_RE.search(ln) is not None:
            cull_stale += 1
            continue
        m = FIREBALL_SPAWN_SKIP_RE.search(ln)
        if m is not None:
            by_skip[m.group(1)] = by_skip.get(m.group(1), 0) + 1
    if not by_path and not by_skip and cull_stale == 0:
        return []
    out: list[str] = []
    out.append(f"    fireball_spawn paths: {sum(by_path.values())} by_path={by_path}")
    if cull_stale:
        out.append(
            f"    fireball cull_stale: {cull_stale} "
            f"(in-flight carry-over removed before anim/retry spawn)"
        )
    if by_skip:
        out.append(f"    fireball_spawn skips: {sum(by_skip.values())} by_skip={by_skip}")
    latch_clear = by_skip.get("latch_clear", 0)
    latched = by_skip.get("latched", 0)
    emergency = by_path.get("emergency", 0)
    retry = by_path.get("retry", 0)
    anim = by_path.get("anim", 0)
    if latch_clear:
        out.append(
            f"    fireball mid-throw latch_clear: {latch_clear} "
            f"(ball lost after latch — retry path should follow as path=retry)"
        )
    if retry:
        out.append(f"    fireball path=retry: {retry} (mid-throw recovery spawns)")
    if emergency and anim:
        out.append(
            f"    SUSPECT fireball emergency+anim mix: emergency={emergency} anim={anim} "
            f"(same session — check for double spawn per B)"
        )
    if latched > 200:
        out.append(
            f"    fireball skip=latched spam: {latched} rows "
            f"(expected after spawn; empty throws if weapon_count=0 persists while latched)"
        )
    return out


def collect_guard_shield_no_fighter_summary(lines: list[str]) -> list[str]:
    """Summarize orphan guard-shield prune spam (effect GObj with no live fighter coupling)."""
    ids: dict[str, int] = {}
    for ln in lines:
        m = GUARD_SHIELD_NO_FIGHTER_RE.search(ln)
        if m is not None:
            ids[m.group(1)] = ids.get(m.group(1), 0) + 1
    if not ids:
        return []
    out: list[str] = []
    out.append(f"    guard_shield no_fighter prune rows: {sum(ids.values())} by_gobj_id={ids}")
    return out


def collect_crash_summary(lines: list[str]) -> list[str]:
    """Summarize SIGSEGV + stale-DL crash-handler lines."""
    sigsegv = [ln for ln in lines if CRASH_SIGSEGV_RE.search(ln)]
    backtrace = [ln for ln in lines if "main-thread backtrace" in ln]
    stale_dl = [ln for ln in lines if STALE_DL_BAD_CMD_RE.search(ln)]
    if not sigsegv and not backtrace and not stale_dl:
        return []
    out: list[str] = []
    if sigsegv:
        out.append(f"    SIGSEGV lines: {len(sigsegv)} last={sigsegv[-1].strip()[:160]}")
    if backtrace:
        out.append(f"    crash backtrace lines: {len(backtrace)} last={backtrace[-1].strip()[:160]}")
    if stale_dl:
        m = STALE_DL_BAD_CMD_RE.search(stale_dl[-1])
        if m is not None:
            out.append(
                f"    stale-DL badCmd: frame={m.group(3)} host={m.group(1)} "
                f"(fault addr often matches xf UAF into DL arena)"
            )
    return out


def effect_xf_stale_signature(line: str) -> str | None:
    m = EFFECT_XF_STALE_RE.search(line)
    if m is None:
        return None
    return f"xfstale:proc={m.group(2)},reason={m.group(3)},gobj={m.group(4)}"


def collapse_effect_xf_stale_lines(lines: list[str]) -> tuple[list[str], int]:
    counts: dict[str, int] = {}
    for ln in lines:
        sig = effect_xf_stale_signature(ln)
        if sig is not None:
            counts[sig] = counts.get(sig, 0) + 1
    out: list[str] = []
    seen: set[str] = set()
    collapsed = 0
    for ln in lines:
        sig = effect_xf_stale_signature(ln)
        if sig is None:
            out.append(ln)
            continue
        if sig in seen:
            continue
        seen.add(sig)
        n = counts.get(sig, 1)
        if n > 1:
            out.append(f"{ln}  (... repeated {n - 1} more times with same proc/reason/gobj_id)")
            collapsed += n - 1
        else:
            out.append(ln)
    return out, collapsed


def guard_shield_no_fighter_signature(line: str) -> str | None:
    m = GUARD_SHIELD_NO_FIGHTER_RE.search(line)
    if m is None:
        return None
    return f"shield_orphan:gobj={m.group(1)}"


def collapse_guard_shield_no_fighter_lines(lines: list[str]) -> tuple[list[str], int]:
    counts: dict[str, int] = {}
    for ln in lines:
        sig = guard_shield_no_fighter_signature(ln)
        if sig is not None:
            counts[sig] = counts.get(sig, 0) + 1
    out: list[str] = []
    seen: set[str] = set()
    collapsed = 0
    for ln in lines:
        sig = guard_shield_no_fighter_signature(ln)
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
    rng_walks: list[tuple[str, str]] = []
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
        fc = parse_frame_commit_diverge(ln)
        if fc is not None:
            fc_tick, fc_fields, fc_inputs_match = fc
            if fc_fields:
                if fc_inputs_match is True:
                    inp = " inputs=MATCH(cross-ISA determinism failure)"
                elif fc_inputs_match is False:
                    inp = " inputs=DIFFER(input/pairing skew)"
                else:
                    inp = ""
                out.append(
                    f"    FRAME_COMMIT_STATE_DIVERGE: validation={fc_tick} "
                    f"diverged={','.join(sorted(fc_fields))}{inp}"
                )
            continue
        m = ITEM_HASH_WALK_BEGIN_RE.search(ln)
        if m is not None:
            item_walks.append((m.group(1), m.group(2)))
            continue
        m = RNG_HASH_WALK_BEGIN_RE.search(ln)
        if m is not None:
            rng_walks.append((m.group(1), m.group(2)))
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
    if rng_walks:
        reasons: dict[str, int] = {}
        for _, reason in rng_walks:
            reasons[reason] = reasons.get(reason, 0) + 1
        out.append(f"    rng_hash_walk episodes: {len(rng_walks)} by_reason={reasons}")
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


def collect_map_hash_drift_summary(lines: list[str]) -> list[str]:
    """Summarize SSB64_NETPLAY_SNAPSHOT_MAP_HASH_DIAG map hash decomposition."""
    out: list[str] = []
    drifts: list[dict[str, str]] = []
    ground_payloads: list[tuple[str, str, str]] = []
    yaku1_rows: list[tuple[str, float, float, float, float, str, str]] = []
    arwing_rows: list[tuple[str, float, float]] = []
    save_self_tests: list[tuple[str, str, str]] = []
    sector_fields: list[tuple[str, str, str, str]] = []

    for ln in lines:
        m = MAP_HASH_DRIFT_RE.search(ln)
        if m is not None:
            drifts.append(
                {
                    "tick": m.group(1),
                    "slot_stored": m.group(2).upper(),
                    "live_full": m.group(3).upper(),
                    "kin": m.group(4).upper(),
                    "ground_fold_slot": m.group(5).upper(),
                    "ground_fold_scratch": m.group(6).upper(),
                    "hash_kin_plus_slot_ground": m.group(7).upper(),
                    "hash_kin_plus_scratch_ground": m.group(8).upper(),
                }
            )
            continue
        m = MAP_HASH_GROUND_PAYLOAD_RE.search(ln)
        if m is not None:
            ground_payloads.append((m.group(1), m.group(5), m.group(6)))
            continue
        m = MAP_HASH_YAKU1_RE.search(ln)
        if m is not None:
            live_tx = float(m.group(2))
            blob_tx = float(m.group(8))
            live_ty = float(m.group(3))
            blob_ty = float(m.group(9))
            yaku1_rows.append(
                (
                    m.group(1),
                    abs(live_tx - blob_tx),
                    abs(live_ty - blob_ty),
                    live_tx,
                    blob_tx,
                    m.group(14),
                    m.group(15),
                )
            )
            continue
        m = MAP_HASH_ARWING_D0_RE.search(ln)
        if m is not None:
            live_tx = float(m.group(4))
            blob_tx = float(m.group(6))
            arwing_rows.append((m.group(1), abs(live_tx - blob_tx), live_tx))
            continue
        m = MAP_HASH_SAVE_SELF_TEST_RE.search(ln)
        if m is not None:
            save_self_tests.append((m.group(1), m.group(2).upper(), m.group(3).upper()))
            continue
        m = MAP_HASH_SECTOR_FIELD_RE.search(ln)
        if m is not None:
            sector_fields.append((m.group(1), m.group(2), m.group(3), m.group(4)))

    if not (
        drifts
        or ground_payloads
        or yaku1_rows
        or arwing_rows
        or save_self_tests
        or sector_fields
    ):
        return out

    if drifts:
        ticks = sorted({d["tick"] for d in drifts}, key=int)
        out.append(f"    map_hash_drift rows: {len(drifts)} unique_ticks={len(ticks)}")
        out.append(
            f"    map_hash_drift ticks: {','.join(ticks[:8])}"
            + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
        )
        slot_live_mismatch = sum(1 for d in drifts if d["slot_stored"] != d["live_full"])
        kin_scratch_live = sum(
            1 for d in drifts if d["hash_kin_plus_scratch_ground"] == d["live_full"]
        )
        kin_slot_live = sum(1 for d in drifts if d["hash_kin_plus_slot_ground"] == d["live_full"])
        ground_fold_match = sum(
            1 for d in drifts if d["ground_fold_slot"] == d["ground_fold_scratch"]
        )
        out.append(
            f"    map_hash_drift attribution: slot_stored!=live_full={slot_live_mismatch}/"
            f"{len(drifts)} kin_plus_scratch==live_full={kin_scratch_live}/{len(drifts)} "
            f"kin_plus_slot==live_full={kin_slot_live}/{len(drifts)} "
            f"ground_fold_slot==scratch={ground_fold_match}/{len(drifts)}"
        )
        sample = drifts[0]
        out.append(
            f"    map_hash_drift sample tick={sample['tick']}: slot_stored=0x{sample['slot_stored']} "
            f"live_full=0x{sample['live_full']} kin=0x{sample['kin']} "
            f"ground_fold=0x{sample['ground_fold_slot']}"
        )

    if ground_payloads:
        mismatch = [row for row in ground_payloads if row[1] == "0"]
        out.append(
            f"    map_hash_ground_payload rows: {len(ground_payloads)} match=0 rows={len(mismatch)}"
        )
        if mismatch:
            ticks = sorted({row[0] for row in mismatch}, key=int)
            out.append(
                f"    map_hash_ground_payload mismatch ticks: {','.join(ticks[:8])}"
                + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
            )

    if sector_fields:
        by_field: dict[str, int] = {}
        for _, field, _, _ in sector_fields:
            by_field[field] = by_field.get(field, 0) + 1
        out.append(f"    map_hash_sector_field rows: {len(sector_fields)} by_field={by_field}")

    if yaku1_rows:
        max_tx = max(yaku1_rows, key=lambda row: row[1])
        user_mismatch = sum(1 for row in yaku1_rows if row[5] != row[6])
        out.append(
            f"    map_hash_yaku1 rows: {len(yaku1_rows)} user_live!=user_blob={user_mismatch} "
            f"max_tx_delta={max_tx[1]:.3f} tick={max_tx[0]} live_tx={max_tx[3]:.3f} "
            f"blob_tx={max_tx[4]:.3f}"
        )

    if arwing_rows:
        max_tx = max(arwing_rows, key=lambda row: row[1])
        out.append(
            f"    map_hash_arwing_d0 rows: {len(arwing_rows)} max_tx_delta={max_tx[1]:.3f} "
            f"tick={max_tx[0]} live_tx={max_tx[2]:.3f}"
        )

    if save_self_tests:
        ticks = sorted({row[0] for row in save_self_tests}, key=int)
        out.append(f"    map_hash_save_self_test FAIL rows: {len(save_self_tests)}")
        out.append(
            f"    map_hash_save_self_test FAIL ticks: {','.join(ticks[:8])}"
            + (f" (+{len(ticks) - 8} more)" if len(ticks) > 8 else "")
        )

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
    out.extend(collect_map_hash_drift_summary(lines))
    return out


@dataclass
class SessionRecord:
    session_id: str
    automatch_ticket: str | None = None
    netplay_role: str | None = None
    match_config_staged_seed: int | None = None
    metadata_composed_seed: int | None = None
    host_start_seed: int | None = None
    bootstrap_applied_seed: int | None = None
    sim_tick1_rng: str | None = None
    sim_tick1_cseed: str | None = None
    max_sim_tick: int | None = None


@dataclass
class NetplaySessionIdentity:
    label: str
    session_id: str | None = None
    automatch_ticket: str | None = None
    netplay_role: str | None = None
    match_config_staged_seed: int | None = None
    metadata_composed_seed: int | None = None
    host_start_seed: int | None = None
    bootstrap_applied_seed: int | None = None
    sim_tick1_rng: str | None = None
    sim_tick1_cseed: str | None = None
    max_sim_tick: int | None = None
    session_count: int = 0
    all_session_ids: list[str] = field(default_factory=list)


@dataclass
class LabeledLog:
    label: str
    path: Path
    lines_read: int = 0
    lines_kept: int = 0
    kept: list[str] = field(default_factory=list)
    collapse_stats: dict[str, int] = field(default_factory=dict)
    stall_summary_lines: list[str] = field(default_factory=list)
    session_identity: NetplaySessionIdentity | None = None


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


@dataclass
class SyncReportMetrics:
    label: str
    path: Path
    lines_total: int = 0
    max_sim_tick: int | None = None
    synctest_ok: int = 0
    synctest_fail: int = 0
    first_synctest_fail_tick: int | None = None
    synctest_skip: int = 0
    load_hash_drift: int = 0
    load_hash_item_mismatch: int = 0
    first_item_drift_tick: int | None = None
    load_hash_abort: int = 0
    resim_complete: int = 0
    resim_sim_core_ok: int = 0
    resim_sim_core_reject: int = 0
    frame_commit_item_diverge: int = 0
    frame_commit_rng_diverge: int = 0
    # Cross-peer FRAME_COMMIT_STATE_DIVERGE (any partition). Counted once per line via
    # the unified parser so figh/world/item/etc. are no longer missed.
    frame_commit_state_diverge: int = 0
    frame_commit_figh_diverge: int = 0
    # Divergences where inp_local == inp_peer: identical inputs, different state = a
    # genuine cross-ISA determinism failure (not packet loss / input skew).
    frame_commit_diverge_inputs_match: int = 0
    frame_commit_first_tick: int | None = None
    frame_commit_diverged_fields: set[str] = field(default_factory=set)
    # Deferred resim could not reanchor and aborted -> terminal, unrecoverable desync.
    peer_baseline_resync_storm: int = 0
    peer_snapshot_diverge: int = 0
    session_stop: int = 0
    css_return: int = 0
    sigsegv: int = 0
    desync_report: int = 0
    ring_save_figh_bad: int = 0
    ring_save_anim_bad: int = 0


def _sync_report_bump_tick(current: int | None, tick_s: str) -> int | None:
    try:
        tick = int(tick_s)
    except ValueError:
        return current
    if current is None or tick > current:
        return tick
    return current


def parse_frame_commit_diverge(line: str) -> tuple[int, set[str], bool | None] | None:
    """Parse a FRAME_COMMIT_STATE_DIVERGE line into (validation_tick, diverged_fields, inputs_match).

    Compares every partition present on both the local and peer side of the line so no
    field is missed. inputs_match is True/False when inp_local/inp_peer are present, else
    None. Returns None if the line is not a frame-commit divergence.
    """
    m = FRAME_COMMIT_DIVERGE_LINE_RE.search(line)
    if m is None:
        return None
    tick = int(m.group(1))
    local_fields = {name: val for name, val in _FC_PARTITION_RE.findall(m.group(2))}
    peer_fields = {name: val for name, val in _FC_PARTITION_RE.findall(m.group(3))}
    diverged: set[str] = set()
    for name, lval in local_fields.items():
        pval = peer_fields.get(name)
        if pval is not None and lval.lower() != pval.lower():
            diverged.add(name)
    im = _FC_INPUTS_RE.search(line)
    inputs_match: bool | None = None
    if im is not None:
        inputs_match = im.group(1).lower() == im.group(2).lower()
    return tick, diverged, inputs_match


def read_raw_log_lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def collect_sync_report_metrics(label: str, path: Path, lines: list[str]) -> SyncReportMetrics:
    """Scan raw log lines for rollback/sync stability signals (unfiltered)."""
    m = SyncReportMetrics(label=label, path=path, lines_total=len(lines))
    for ln in lines:
        sim_m = SIM_STATE_TICK_RE.search(ln)
        if sim_m is not None:
            m.max_sim_tick = _sync_report_bump_tick(m.max_sim_tick, sim_m.group(1))

        ok_m = SYNCTEST_OK_RE.search(ln)
        if ok_m is not None:
            m.synctest_ok += 1
            continue

        fail_m = SYNCTEST_FAIL_RE.search(ln)
        if fail_m is not None:
            m.synctest_fail += 1
            if m.first_synctest_fail_tick is None:
                m.first_synctest_fail_tick = int(fail_m.group(1))
            continue

        skip_m = SYNCTEST_SKIP_ANY_RE.search(ln)
        if skip_m is not None:
            m.synctest_skip += 1
            continue

        drift_m = LOAD_HASH_DRIFT_ANY_RE.search(ln)
        if drift_m is not None:
            m.load_hash_drift += 1
            item_m = LOAD_HASH_DRIFT_RE.search(ln)
            if item_m is not None and item_m.group(2).lower() != item_m.group(3).lower():
                m.load_hash_item_mismatch += 1
                if m.first_item_drift_tick is None:
                    m.first_item_drift_tick = int(drift_m.group(1))
            continue

        if LOAD_HASH_ABORT_RE.search(ln) is not None:
            m.load_hash_abort += 1
            continue

        if RESIM_COMPLETE_RE.search(ln) is not None:
            m.resim_complete += 1
            continue

        if RESIM_SIM_CORE_RE.search(ln) is not None:
            m.resim_sim_core_ok += 1
            continue

        reject_m = RESIM_SIM_CORE_REJECT_RE.search(ln)
        if reject_m is not None:
            if RESIM_SIM_CORE_REJECT_NOT_IN_RESIM_RE.search(ln) is None:
                m.resim_sim_core_reject += 1
            continue

        fc = parse_frame_commit_diverge(ln)
        if fc is not None:
            fc_tick, fc_fields, fc_inputs_match = fc
            if fc_fields:
                m.frame_commit_state_diverge += 1
                m.frame_commit_diverged_fields |= fc_fields
                if m.frame_commit_first_tick is None:
                    m.frame_commit_first_tick = fc_tick
                if "figh" in fc_fields:
                    m.frame_commit_figh_diverge += 1
                if "item" in fc_fields:
                    m.frame_commit_item_diverge += 1
                if "rng" in fc_fields:
                    m.frame_commit_rng_diverge += 1
                # Identical inputs but divergent state = real determinism failure.
                if fc_inputs_match:
                    m.frame_commit_diverge_inputs_match += 1
            continue

        storm_m = PEER_BASELINE_RESYNC_STORM_RE.search(ln)
        if storm_m is not None:
            m.peer_baseline_resync_storm += 1
            continue

        if PEER_SNAPSHOT_DIVERGE_RE.search(ln) is not None:
            m.peer_snapshot_diverge += 1
            continue

        if VS_SESSION_STOP_RE.search(ln) is not None:
            m.session_stop += 1
            continue

        if CSS_RETURN_RE.search(ln) is not None:
            m.css_return += 1
            continue

        if CRASH_SIGSEGV_RE.search(ln) is not None:
            m.sigsegv += 1
            continue

        if DESYNC_REPORT_RE.search(ln) is not None:
            m.desync_report += 1
            continue

        ring_m = RING_SAVE_DIAG_RE.search(ln)
        if ring_m is not None:
            if ring_m.group(4) == "0":
                m.ring_save_figh_bad += 1
            if ring_m.group(5) == "0":
                m.ring_save_anim_bad += 1

    return m


def _session_record(records: dict[str, SessionRecord], order: list[str], session_id: str) -> SessionRecord:
    if session_id not in records:
        records[session_id] = SessionRecord(session_id=session_id)
        order.append(session_id)
    return records[session_id]


def _pick_primary_session(records: dict[str, SessionRecord], order: list[str]) -> SessionRecord | None:
    if not records:
        return None

    def score(rec: SessionRecord) -> tuple[int, int, int]:
        has_sim = 1 if rec.max_sim_tick is not None else 0
        max_tick = rec.max_sim_tick or 0
        idx = order.index(rec.session_id) if rec.session_id in order else 0
        return (has_sim, max_tick, idx)

    return max(records.values(), key=score)


def collect_session_identity(label: str, lines: list[str]) -> NetplaySessionIdentity:
    """Extract automatch session id and RNG seeds from raw log lines."""
    records: dict[str, SessionRecord] = {}
    order: list[str] = []
    current_session: str | None = None

    for ln in lines:
        mm = MM_POLL_MATCHED_RE.search(ln)
        if mm is not None:
            rec = _session_record(records, order, mm.group(1))
            rec.automatch_ticket = mm.group(3)
            current_session = rec.session_id
            continue

        ack = INPUT_BIND_ACK_RE.search(ln)
        if ack is not None:
            rec = _session_record(records, order, ack.group(1))
            rec.netplay_role = ack.group(2)
            current_session = rec.session_id
            continue

        if current_session is None:
            continue
        rec = records[current_session]

        staged = MATCH_CONFIG_STAGED_RE.search(ln)
        if staged is not None:
            rec.match_config_staged_seed = int(staged.group(1))
            continue

        composed = METADATA_COMPOSED_RE.search(ln)
        if composed is not None:
            rec.metadata_composed_seed = int(composed.group(1))
            continue

        start = HOST_START_RE.search(ln)
        if start is not None:
            rec.host_start_seed = int(start.group(1))
            continue

        applied = BOOTSTRAP_APPLIED_RE.search(ln)
        if applied is not None:
            rec.bootstrap_applied_seed = int(applied.group(1))
            continue

        fields = parse_sim_state_fields(ln)
        if fields is None or "tick" not in fields:
            continue
        try:
            tick = int(fields["tick"])
        except ValueError:
            continue
        if rec.max_sim_tick is None or tick > rec.max_sim_tick:
            rec.max_sim_tick = tick
        if tick == 1:
            rec.sim_tick1_rng = fields.get("rng")
            rec.sim_tick1_cseed = fields.get("cseed")

    primary = _pick_primary_session(records, order)
    if primary is None:
        return NetplaySessionIdentity(label=label)

    return NetplaySessionIdentity(
        label=label,
        session_id=primary.session_id,
        automatch_ticket=primary.automatch_ticket,
        netplay_role=primary.netplay_role,
        match_config_staged_seed=primary.match_config_staged_seed,
        metadata_composed_seed=primary.metadata_composed_seed,
        host_start_seed=primary.host_start_seed,
        bootstrap_applied_seed=primary.bootstrap_applied_seed,
        sim_tick1_rng=primary.sim_tick1_rng,
        sim_tick1_cseed=primary.sim_tick1_cseed,
        max_sim_tick=primary.max_sim_tick,
        session_count=len(records),
        all_session_ids=list(order),
    )


def format_session_identity_lines(identity: NetplaySessionIdentity) -> list[str]:
    out: list[str] = []
    if identity.session_count > 1:
        out.append(
            f"    WARNING: {identity.session_count} automatch sessions in log "
            f"(ids={','.join(identity.all_session_ids)}); primary below"
        )
    if identity.session_id is None:
        out.append("    session: (none found — missing automatch / VS bootstrap lines?)")
        return out

    role_s = identity.netplay_role or "?"
    ticket_s = identity.automatch_ticket or "?"
    out.append(
        f"    [{identity.label}] session: id={identity.session_id} role={role_s} ticket={ticket_s}"
    )

    seed_parts: list[str] = []
    if identity.bootstrap_applied_seed is not None:
        seed_parts.append(f"bootstrap_applied={identity.bootstrap_applied_seed}")
    if identity.match_config_staged_seed is not None:
        seed_parts.append(f"match_config_staged={identity.match_config_staged_seed}")
    if identity.metadata_composed_seed is not None:
        seed_parts.append(f"metadata_composed={identity.metadata_composed_seed}")
    if identity.host_start_seed is not None:
        seed_parts.append(f"host_start={identity.host_start_seed}")
    if seed_parts:
        out.append(f"    [{identity.label}] rng_seed: {' '.join(seed_parts)}")
    else:
        out.append(f"    [{identity.label}] rng_seed: (none found)")

    tick1_parts: list[str] = []
    if identity.sim_tick1_rng is not None:
        tick1_parts.append(f"rng={identity.sim_tick1_rng}")
    if identity.sim_tick1_cseed is not None:
        tick1_parts.append(f"cseed={identity.sim_tick1_cseed}")
    if identity.max_sim_tick is not None:
        tick1_parts.append(f"max_sim_tick={identity.max_sim_tick}")
    if tick1_parts:
        out.append(f"    [{identity.label}] sim_tick1: {' '.join(tick1_parts)}")
    return out


def evaluate_pair_session(
    identity_a: NetplaySessionIdentity,
    identity_b: NetplaySessionIdentity,
) -> tuple[bool, list[str]]:
    """Return (is_paired, report lines)."""
    out: list[str] = ["=== pair session check ==="]
    ok = True

    if identity_a.session_id is None or identity_b.session_id is None:
        out.append("  verdict: UNKNOWN (session id missing in one or both logs)")
        return False, out

    if identity_a.session_id != identity_b.session_id:
        ok = False
        out.append(
            f"  session_id: MISMATCH {identity_a.label}={identity_a.session_id} "
            f"{identity_b.label}={identity_b.session_id}"
        )
    else:
        out.append(f"  session_id: OK ({identity_a.session_id})")

    seed_a = identity_a.bootstrap_applied_seed
    seed_b = identity_b.bootstrap_applied_seed
    if seed_a is None or seed_b is None:
        out.append(
            f"  bootstrap_seed: incomplete ({identity_a.label}={seed_a} {identity_b.label}={seed_b})"
        )
        ok = False
    elif seed_a != seed_b:
        ok = False
        out.append(
            f"  bootstrap_seed: MISMATCH {identity_a.label}={seed_a} {identity_b.label}={seed_b}"
        )
    else:
        out.append(f"  bootstrap_seed: OK ({seed_a})")

    roles = {identity_a.netplay_role, identity_b.netplay_role}
    if roles <= {"host", "client"} and len(roles) == 2:
        out.append(f"  netplay_roles: {identity_a.label}={identity_a.netplay_role} "
                   f"{identity_b.label}={identity_b.netplay_role}")
        host_id = identity_a if identity_a.netplay_role == "host" else identity_b
        client_id = identity_b if identity_a.netplay_role == "host" else identity_a
        host_authoritative = host_id.metadata_composed_seed or host_id.host_start_seed
        client_staged = client_id.match_config_staged_seed
        if host_authoritative is not None and client_staged is not None:
            if host_authoritative != client_staged:
                ok = False
                out.append(
                    f"  wire_seed: MISMATCH host_sent={host_authoritative} "
                    f"client_staged={client_staged}"
                )
            else:
                out.append(f"  wire_seed: OK ({host_authoritative})")

    tick1_rng_a = identity_a.sim_tick1_rng
    tick1_rng_b = identity_b.sim_tick1_rng
    if tick1_rng_a is not None and tick1_rng_b is not None:
        if tick1_rng_a == tick1_rng_b:
            out.append(f"  sim_tick1_rng_hash: OK ({tick1_rng_a})")
        else:
            out.append(
                f"  sim_tick1_rng_hash: differ {identity_a.label}={tick1_rng_a} "
                f"{identity_b.label}={tick1_rng_b} "
                "(expected when bootstrap_seed mismatches or intro RNG consumption diverged)"
            )

    if ok:
        out.append("  verdict: PAIRED (same session_id + bootstrap rng_seed)")
    else:
        out.append("  verdict: UNPAIRED — logs may be from different automatch sessions")
    out.append("")
    return ok, out


def sync_report_verdict(m: SyncReportMetrics) -> tuple[str, list[str]]:
    """Return (verdict_label, reason_strings) for one peer log."""
    reasons: list[str] = []
    if m.sigsegv:
        reasons.append(f"SIGSEGV x{m.sigsegv}")
    if m.load_hash_abort:
        reasons.append(f"LOAD_HASH abort x{m.load_hash_abort}")
    if m.peer_snapshot_diverge:
        reasons.append(f"PEER_SNAPSHOT_DIVERGE x{m.peer_snapshot_diverge}")
    if m.synctest_fail:
        tick = m.first_synctest_fail_tick
        reasons.append(
            f"SYNCTEST_FAIL x{m.synctest_fail}"
            + (f" first={tick}" if tick is not None else "")
        )
    if m.load_hash_item_mismatch:
        tick = m.first_item_drift_tick
        suffix = f" first={tick}" if tick is not None else ""
        if m.resim_sim_core_ok:
            reasons.append(
                f"item LOAD_HASH_DRIFT x{m.load_hash_item_mismatch} "
                f"(soft recovery x{m.resim_sim_core_ok}){suffix}"
            )
        else:
            reasons.append(f"item LOAD_HASH_DRIFT x{m.load_hash_item_mismatch}{suffix}")
    if m.frame_commit_state_diverge:
        fields = ",".join(sorted(m.frame_commit_diverged_fields)) or "?"
        tick = m.frame_commit_first_tick
        suffix = f" first={tick}" if tick is not None else ""
        det = (
            f" ({m.frame_commit_diverge_inputs_match} with identical inputs)"
            if m.frame_commit_diverge_inputs_match
            else ""
        )
        reasons.append(
            f"FRAME_COMMIT_STATE_DIVERGE x{m.frame_commit_state_diverge} "
            f"[{fields}]{det}{suffix}"
        )
    if m.peer_baseline_resync_storm:
        reasons.append(
            f"PEER_BASELINE_RESYNC_STORM x{m.peer_baseline_resync_storm} (recovery aborted)"
        )
    if m.resim_sim_core_reject:
        reasons.append(f"resim-sim-core-reject x{m.resim_sim_core_reject}")
    if m.desync_report:
        reasons.append(f"DESYNC REPORT x{m.desync_report}")

    hard = bool(
        m.sigsegv
        or m.load_hash_abort
        or m.peer_snapshot_diverge
        or m.synctest_fail
        or m.resim_sim_core_reject
        or m.frame_commit_state_diverge
        or m.peer_baseline_resync_storm
        or (m.load_hash_item_mismatch and not m.resim_sim_core_ok)
    )
    if hard:
        return "UNSTABLE", reasons

    soft = bool(
        m.resim_sim_core_ok
        or m.resim_complete
        or m.synctest_skip
        or m.load_hash_drift
    )
    if soft:
        return ("STABLE (soft recovery)", reasons) if reasons else ("STABLE (soft recovery)", [])

    if m.synctest_ok:
        return "STABLE", []
    if m.max_sim_tick is not None:
        return "STABLE (no synctest signals)", []
    return "UNKNOWN (no battle sim_state_tick)", []


def _rb_applied_int(fields: dict[str, str]) -> int:
    try:
        return int(fields.get("rb_applied", "0"))
    except ValueError:
        return 0


def collect_sim_state_by_tick(lines: list[str]) -> dict[str, dict[str, str]]:
    by_tick: dict[str, dict[str, str]] = {}
    for ln in lines:
        fields = parse_sim_state_fields(ln)
        if not (fields and "tick" in fields):
            continue
        tick = fields["tick"]
        prev = by_tick.get(tick)
        # A tick can be logged more than once:
        #   * the forward commit(s) (rb_applied=0), re-emitted as more confirmed
        #     remote input arrives (each later one is more authoritative), and
        #   * resim replay re-logs (rb_applied>=1), which are transient mid-
        #     rollback states one peer may emit and the other may not.
        # Settled value = latest forward commit. Prefer a forward row over any
        # resim row; among same-class rows keep the latest. Last-write-wins (the
        # old behavior) let a one-sided resim re-log clobber the forward commit
        # and compared the host's post-rollback value against the guest's
        # forward value, reporting a phantom desync.
        if prev is not None and _rb_applied_int(fields) != 0 and _rb_applied_int(prev) == 0:
            continue  # keep the forward commit; ignore the resim re-log
        by_tick[tick] = fields
    return by_tick


def collect_force_mismatch_inject_tick(lines: list[str]) -> int | None:
    """First wire tick where rollback debug injected an input tamper (both peers log it)."""
    ticks: list[int] = []
    for ln in lines:
        m = FORCE_MISMATCH_INJECT_RE.search(ln)
        if m is not None:
            ticks.append(int(m.group(1)))
    return min(ticks) if ticks else None


def _pair_force_mismatch_inject_tick(lines_a: list[str], lines_b: list[str]) -> int | None:
    ta = collect_force_mismatch_inject_tick(lines_a)
    tb = collect_force_mismatch_inject_tick(lines_b)
    if ta is not None and tb is not None:
        return min(ta, tb)
    return ta if ta is not None else tb


def _sim_state_item_skew_expected_force_mismatch(
    tick: str,
    diffs: list[str],
    fa: dict[str, str],
    fb: dict[str, str],
    force_inject: int | None,
) -> bool:
    """Item-only cross-peer sim_state skew after FORCE_MISMATCH while core partitions match."""
    if force_inject is None or int(tick) < force_inject:
        return False
    if set(diffs) != {"item"}:
        return False
    core_keys = ("figh", "world", "rng", "wpn", "mph")
    return all(fa.get(k) == fb.get(k) and k in fa and k in fb for k in core_keys)


def diff_sync_report_pair(
    metrics_a: SyncReportMetrics,
    lines_a: list[str],
    metrics_b: SyncReportMetrics,
    lines_b: list[str],
) -> tuple[list[str], bool]:
    """First sim_state_tick hash skew between host/guest raw logs. Returns (lines, mismatch)."""
    by_a = collect_sim_state_by_tick(lines_a)
    by_b = collect_sim_state_by_tick(lines_b)
    out: list[str] = []
    common = sorted(set(by_a.keys()) & set(by_b.keys()), key=int)
    if not common:
        out.append("  pair: no overlapping sim_state_tick rows")
        return out, False

    force_inject = _pair_force_mismatch_inject_tick(lines_a, lines_b)
    compare_keys = ["figh", "item", "anim", "eff", "world", "wpn"]
    skipped_gen = 0
    expected_item_skew = 0
    first_expected_note: str | None = None
    for tick in common:
        fa, fb = by_a[tick], by_b[tick]
        # Compare like-with-like only. If one peer re-logged this tick during a
        # resim (rb_applied>=1) but the other did not, the surviving rows are at
        # different rollback generations and must not be diffed against each
        # other — that is the phantom-desync this report used to emit.
        if _rb_applied_int(fa) != _rb_applied_int(fb):
            skipped_gen += 1
            continue
        diffs = [k for k in compare_keys if fa.get(k) != fb.get(k) and k in fa and k in fb]
        if not diffs:
            continue
        if _sim_state_item_skew_expected_force_mismatch(tick, diffs, fa, fb, force_inject):
            expected_item_skew += 1
            if first_expected_note is None:
                first_expected_note = (
                    f"  pair: sim_state item-only skew tick={tick} expected "
                    f"(FORCE_MISMATCH inject={force_inject}; core partitions match)"
                )
            continue
        out.append(f"  pair: first sim_state mismatch tick={tick} fields={','.join(diffs)}")
        for k in diffs:
            out.append(f"    {k}: {metrics_a.label}={fa.get(k)} {metrics_b.label}={fb.get(k)}")
        if first_expected_note is not None:
            out.append(first_expected_note)
        return out, True

    note = f"  pair: sim_state_tick aligned on {len(common)} overlapping ticks"
    if skipped_gen:
        note += f" ({skipped_gen} skipped: resim/forward generation mismatch)"
    if expected_item_skew:
        note += (
            f"; {expected_item_skew} tick(s) item-only skew expected "
            f"(FORCE_MISMATCH inject={force_inject})"
        )
        if first_expected_note is not None:
            out.append(first_expected_note)
    out.append(note)
    return out, False


def pair_sync_verdict(metrics: list[SyncReportMetrics], pair_mismatch: bool) -> str:
    if pair_mismatch:
        return "UNSTABLE"
    verdicts = [sync_report_verdict(m)[0] for m in metrics]
    if any(v.startswith("UNSTABLE") for v in verdicts):
        return "UNSTABLE"
    if any(v.startswith("UNKNOWN") for v in verdicts):
        return "UNKNOWN"
    if any(v == "STABLE (soft recovery)" for v in verdicts):
        return "STABLE (soft recovery)"
    return "STABLE"


def format_sync_report_line(label: str, m: SyncReportMetrics) -> list[str]:
    verdict, reasons = sync_report_verdict(m)
    tick_s = str(m.max_sim_tick) if m.max_sim_tick is not None else "?"
    parts = [
        f"  [{label}] {verdict}  max_sim_tick={tick_s}",
        (
            f"    resim={m.resim_complete} synctest_ok={m.synctest_ok} "
            f"fail={m.synctest_fail} skip={m.synctest_skip}"
        ),
        (
            f"    load_hash_drift={m.load_hash_drift} item_mm={m.load_hash_item_mismatch} "
            f"fc_item_div={m.frame_commit_item_diverge} fc_rng_div={m.frame_commit_rng_diverge}"
        ),
    ]
    if m.frame_commit_state_diverge or m.peer_baseline_resync_storm:
        fields = ",".join(sorted(m.frame_commit_diverged_fields)) or "-"
        parts.append(
            f"    fc_state_div={m.frame_commit_state_diverge} fc_figh_div={m.frame_commit_figh_diverge} "
            f"fc_div_inputs_match={m.frame_commit_diverge_inputs_match} "
            f"resync_storm={m.peer_baseline_resync_storm} fc_div_fields=[{fields}]"
        )
    if m.resim_sim_core_ok or m.resim_sim_core_reject:
        parts.append(
            f"    resim_core_ok={m.resim_sim_core_ok} resim_core_reject={m.resim_sim_core_reject}"
        )
    if m.session_stop or m.css_return or m.sigsegv:
        parts.append(
            f"    session_stop={m.session_stop} css_return={m.css_return} sigsegv={m.sigsegv}"
        )
    if m.ring_save_figh_bad or m.ring_save_anim_bad:
        parts.append(
            f"    ring_save_bad figh={m.ring_save_figh_bad} anim={m.ring_save_anim_bad}"
        )
    if reasons:
        parts.append(f"    reasons: {'; '.join(reasons)}")
    return parts


def build_sync_report(
    metrics: list[SyncReportMetrics],
    raw_lines: list[list[str]],
    identities: list[NetplaySessionIdentity] | None = None,
) -> tuple[list[str], bool]:
    """Return (report lines, unstable)."""
    out: list[str] = ["=== sync-report ==="]
    unstable = False

    if identities is not None and len(identities) >= 2:
        paired, pair_session_lines = evaluate_pair_session(identities[0], identities[1])
        out.extend(pair_session_lines)
        if not paired:
            unstable = True
        for ident in identities:
            out.extend(format_session_identity_lines(ident))

    for m in metrics:
        verdict, _ = sync_report_verdict(m)
        if verdict.startswith("UNSTABLE"):
            unstable = True
        out.extend(format_sync_report_line(m.label, m))

    pair_mismatch = False
    if len(metrics) >= 2:
        pair_lines, pair_mismatch = diff_sync_report_pair(
            metrics[0], raw_lines[0], metrics[1], raw_lines[1]
        )
        out.append(f"  MATCH: {pair_sync_verdict(metrics, pair_mismatch)}")
        out.extend(pair_lines)
        if pair_mismatch or pair_sync_verdict(metrics, pair_mismatch) == "UNSTABLE":
            unstable = True
    out.append("")
    return out, unstable


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
        if lg.session_identity is not None:
            out.extend(format_session_identity_lines(lg.session_identity))
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
        out.extend(collect_whispy_repair_summary(lg.kept))
        out.extend(collect_quake_sanitize_summary(lg.kept))
        out.extend(collect_guard_shield_load_drift_summary(lg.kept))
        out.extend(collect_load_hash_partition_summary(lg.kept))
        out.extend(collect_fighter_anim_drift_summary(lg.kept))
        out.extend(collect_ness_pkthunder_summary(lg.kept))
        out.extend(collect_gobj_link_audit_summary(lg.kept))
        out.extend(collect_effect_xf_stale_summary(lg.kept))
        out.extend(collect_fireball_spawn_summary(lg.kept))
        out.extend(collect_guard_shield_no_fighter_summary(lg.kept))
        out.extend(collect_crash_summary(lg.kept))
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
            or lg.collapse_stats.get("effect_xf_stale_collapsed", 0)
            or lg.collapse_stats.get("guard_shield_no_fighter_collapsed", 0)
        ):
            out.append(
                f"    collapsed repeats: r_stall={lg.collapse_stats.get('r_stall_collapsed', 0)} "
                f"sim_frozen={lg.collapse_stats.get('sim_frozen_collapsed', 0)} "
                f"ring_save={lg.collapse_stats.get('ring_save_collapsed', 0)} "
                f"yamabuki_gate={lg.collapse_stats.get('yamabuki_gate_collapsed', 0)} "
                f"yamabuki_gate_node={lg.collapse_stats.get('yamabuki_gate_node_collapsed', 0)} "
                f"yamabuki_hitokage={lg.collapse_stats.get('yamabuki_hitokage_collapsed', 0)} "
                f"ness_pkthunder_stall={lg.collapse_stats.get('ness_pkthunder_stall_collapsed', 0)} "
                f"netstatusvars={lg.collapse_stats.get('netstatusvars_collapsed', 0)} "
                f"effect_xf_stale={lg.collapse_stats.get('effect_xf_stale_collapsed', 0)} "
                f"guard_shield_no_fighter={lg.collapse_stats.get('guard_shield_no_fighter_collapsed', 0)}"
            )
    if len(logs) >= 2:
        id_a = logs[0].session_identity
        id_b = logs[1].session_identity
        if id_a is not None and id_b is not None:
            _, pair_lines = evaluate_pair_session(id_a, id_b)
            out.extend(pair_lines)
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
    force_inject = _pair_force_mismatch_inject_tick(log_a.kept, log_b.kept)
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
    item_first_expected = False
    expected_item_skew = 0
    pacing_first: tuple[str, list[str]] | None = None
    for tick in common:
        fa, fb = by_tick_a[tick], by_tick_b[tick]
        diffs = [
            k for k in compare_keys if fa.get(k) != fb.get(k) and k in fa and k in fb
        ]
        if diffs and _sim_state_item_skew_expected_force_mismatch(
            tick, diffs, fa, fb, force_inject
        ):
            expected_item_skew += 1
            if item_first is None:
                item_first = (tick, fa.get("item", "?"), fb.get("item", "?"))
                item_first_expected = True
            continue
        if item_first is None and fa.get("item") != fb.get("item") and "item" in fa and "item" in fb:
            item_first = (tick, fa.get("item", "?"), fb.get("item", "?"))
        if pacing_first is None:
            pacing_diffs = [
                k for k in ("ahead", "hr", "commit_gen") if fa.get(k) != fb.get(k) and k in fa and k in fb
            ]
            if pacing_diffs:
                pacing_first = (tick, pacing_diffs)
        if diffs and item_first is None and pacing_first is None:
            out.append(f"  first mismatch tick={tick} fields={','.join(diffs)}")
            for k in diffs:
                out.append(f"    {k}: {log_a.label}={fa.get(k)} {log_b.label}={fb.get(k)}")
            break
    else:
        if item_first is not None:
            tick, ia, ib = item_first
            if item_first_expected and force_inject is not None:
                out.append(
                    f"  first item mismatch tick={tick} expected "
                    f"(FORCE_MISMATCH inject={force_inject}; core partitions match)"
                )
            else:
                out.append(f"  first item mismatch tick={tick}")
            out.append(f"    item: {log_a.label}={ia} {log_b.label}={ib}")
            if expected_item_skew > 1:
                out.append(
                    f"  ({expected_item_skew} total item-only skew ticks suppressed as expected)"
                )
        if pacing_first is not None:
            tick, fields = pacing_first
            fa, fb = by_tick_a[tick], by_tick_b[tick]
            out.append(f"  first pacing skew tick={tick} fields={','.join(fields)}")
            for k in fields:
                out.append(f"    {k}: {log_a.label}={fa.get(k)} {log_b.label}={fb.get(k)}")
        if item_first is None and pacing_first is None:
            note = "  no field mismatch on overlapping sim_state_tick rows"
            if expected_item_skew:
                note += (
                    f" ({expected_item_skew} item-only skew ticks expected "
                    f"FORCE_MISMATCH inject={force_inject})"
                )
            out.append(note)
    out.append("")
    return out


def collect_nan_signatures(lines: list[str]) -> dict[str, dict[str, str]]:
    """Per-tick bit-pattern signatures for the cross-ISA NaN families."""
    fams: dict[str, dict[str, str]] = {
        "airveltransn_nan": {},
        "corrupt jumpaerial": {},
        "fighter_nan": {},
    }
    for ln in lines:
        p = parse_netstatusvars_line(ln)
        if p is not None:
            tick, event, fields = p
            if event == "corrupt" and fields.get("kind") == "jumpaerial":
                fams["corrupt jumpaerial"][tick] = (
                    f"drift={fields.get('drift')} vel_air={fields.get('vel_air')}"
                )
            elif event == "airveltransn_nan":
                fams["airveltransn_nan"][tick] = (
                    f"out={fields.get('out')} rot_z={fields.get('rot_z')} "
                    f"transn_t={fields.get('transn_t')} topn_s={fields.get('topn_s')} "
                    f"anim_vel={fields.get('anim_vel')} cos={fields.get('cos')} sin={fields.get('sin')}"
                )
            continue
        p = parse_ness_pkthunder_gate_line(ln)
        if p is not None:
            tick, event, fields = p
            if event == "fighter_nan":
                fams["fighter_nan"][tick] = (
                    f"bad={fields.get('bad')} translate={fields.get('translate')} "
                    f"vel_air={fields.get('vel_air')}"
                )
    return fams


def diff_netstatusvars_nan(log_a: LabeledLog, log_b: LabeledLog) -> list[str]:
    """Report the first tick where a NaN-family bit pattern diverges between the two peers.

    This automates the cross-ISA root-cause walk: the JumpAerial / airveltransn NaN carries a
    sign bit that differs per ISA (+nan 0x7fc00000 vs -nan 0xffc00000), which is the actual hash
    divergence even when both peers later settle to identical ±inf."""
    fa = collect_nan_signatures(log_a.kept)
    fb = collect_nan_signatures(log_b.kept)
    out: list[str] = [f"=== NaN cross-ISA diff ({log_a.label} vs {log_b.label}) ==="]
    any_family = False
    for family in ("airveltransn_nan", "corrupt jumpaerial", "fighter_nan"):
        ma, mb = fa[family], fb[family]
        if not ma and not mb:
            continue
        any_family = True
        common = sorted(set(ma.keys()) & set(mb.keys()), key=int)
        out.append(
            f"  [{family}] {log_a.label} rows={len(ma)} {log_b.label} rows={len(mb)} "
            f"common_ticks={len(common)}"
        )
        reported = False
        for tick in common:
            if ma[tick] != mb[tick]:
                out.append(f"    first divergent tick={tick}")
                out.append(f"      {log_a.label}: {ma[tick]}")
                out.append(f"      {log_b.label}: {mb[tick]}")
                reported = True
                break
        if not reported and common:
            out.append("    no bit-pattern divergence on common ticks (identical across ISAs)")
    if not any_family:
        out.append("  no NaN-family diagnostics in either log")
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
    all_lines: list[str] = []
    prev_snapshot_save: dict[str, str | None] = {p: None for p in SNAPSHOT_SAVE_DEDUPE_PREFIXES}
    with path.open(encoding="utf-8", errors="replace") as f:
        for line in f:
            lg.lines_read += 1
            raw = line.rstrip("\n")
            all_lines.append(raw)
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
    lg.session_identity = collect_session_identity(label, all_lines)
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
    parser.add_argument(
        "--sync-report",
        action="store_true",
        help="Print brief stability report (synctest, hash drift, resim, pair diff) to stdout",
    )
    parser.add_argument("--diff-ticks", action="store_true")
    parser.add_argument(
        "--diff-death-rebirth",
        action="store_true",
        help="Include death_rebirth_sim + REBIRTH_GATE diffs (default when --diff-ticks)",
    )
    args = parser.parse_args()
    diff_death_rebirth = args.diff_death_rebirth or args.diff_ticks
    sync_unstable = False
    pair_session_ok = True

    if args.sync_report:
        sync_metrics: list[SyncReportMetrics] = []
        sync_raw_lines: list[list[str]] = []
        sync_identities: list[NetplaySessionIdentity] = []
        for label, path_s in args.label:
            path = Path(path_s)
            if not path.is_file():
                print(f"error: not a file: {path}", file=sys.stderr)
                return 2
            raw = read_raw_log_lines(path)
            sync_raw_lines.append(raw)
            sync_metrics.append(collect_sync_report_metrics(label, path, raw))
            sync_identities.append(collect_session_identity(label, raw))
        if len(sync_identities) >= 2:
            pair_session_ok, _ = evaluate_pair_session(sync_identities[0], sync_identities[1])
        sync_report_lines, sync_unstable = build_sync_report(
            sync_metrics, sync_raw_lines, sync_identities
        )
        sys.stdout.write("\n".join(sync_report_lines))
        trim_wanted = bool(
            args.output
            or args.tick_min is not None
            or args.tick_max is not None
            or args.diff_ticks
            or diff_death_rebirth
            or args.summary_only
        )
        if not trim_wanted:
            return 1 if sync_unstable else 0

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
        xf_stale_collapsed: int
        lg.kept, xf_stale_collapsed = collapse_effect_xf_stale_lines(lg.kept)
        lg.collapse_stats["effect_xf_stale_collapsed"] = xf_stale_collapsed
        shield_orphan_collapsed: int
        lg.kept, shield_orphan_collapsed = collapse_guard_shield_no_fighter_lines(lg.kept)
        lg.collapse_stats["guard_shield_no_fighter_collapsed"] = shield_orphan_collapsed
        lg.lines_kept = len(lg.kept)

    summary = build_summary(logs)
    if len(logs) >= 2:
        id_a = logs[0].session_identity
        id_b = logs[1].session_identity
        if id_a is not None and id_b is not None:
            pair_session_ok, _ = evaluate_pair_session(id_a, id_b)
    elif not args.sync_report:
        pair_session_ok = True
    if args.diff_ticks and len(logs) >= 2:
        summary.extend(diff_sim_state_ticks(logs[0], logs[1]))
        summary.extend(diff_netstatusvars_nan(logs[0], logs[1]))
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
    if sync_unstable or not pair_session_ok:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
