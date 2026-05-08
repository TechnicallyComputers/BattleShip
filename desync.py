#!/usr/bin/env python3
"""
Compare host vs client BattleShip netplay logs at aligned sim ticks.

Parses:
  SSB64 NetPeer: tick_diag ... sim_tick=... exec_rdy=... hr=...
  SSB64 NetSync: tick_diag tick=... tick_minus_hr=... exec_rdy=...

Default: compare only **exec_rdy** at the same sim_tick (execution gate / whether a full VS step
could run — directly tied to sim advancing). Last line per tick wins.

**Not used for default compare** (non–sim-state or misleading cross-peer):
  scene, push, tm_*, unix_ms, bar_rel, deadline_*, delay, late, tag, role;
  cross-peer hr / tick_minus_hr (each peer is local). Optional heuristic: --compare-local-skew.

Usage:
  python3 tools/netplay_tick_desync_compare.py host.log client.log
  python3 tools/netplay_tick_desync_compare.py host.log client.log --compare-local-skew --skew-diff 2
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from typing import Dict, Iterator, Optional, Tuple

PEER_TICK_DIAG_RE = re.compile(
    r"SSB64 NetPeer: tick_diag "
    r"tag=(?P<tag>\S+) "
    r"role=(?P<role>host|client) "
    r"sim_tick=(?P<sim_tick>\d+) "
    r"push=(?P<push>-?\d+) "
    r"tm_up=(?P<tm_up>\d+) "
    r"tm_fr=(?P<tm_fr>\d+) "
    r"scene=(?P<scene>\d+) "
    r"exec_rdy=(?P<exec_rdy>[01]) "
    r"bar_rel=(?P<bar_rel>[01]) "
    r"unix_ms=(?P<unix_ms>\d+) "
    r"deadline_valid=(?P<deadline_valid>[01]) "
    r"deadline_ms=(?P<deadline_ms>\d+) "
    r"deadline_vi_ph=(?P<deadline_vi_ph>\d+) "
    r"delay=(?P<delay>\d+) "
    r"hr=(?P<hr>\d+) "
    r"late=(?P<late>\d+)"
)

NETSYNC_TICK_DIAG_RE = re.compile(
    r"SSB64 NetSync: tick_diag "
    r"tick=(?P<tick>\d+) "
    r"push=(?P<push>-?\d+) "
    r"tm_up=(?P<tm_up>\d+) "
    r"tm_fr=(?P<tm_fr>\d+) "
    r"scene=(?P<scene>\d+) "
    r"unix_ms=(?P<unix_ms>\d+) "
    r"tick_minus_hr=(?P<tick_minus_hr>-?\d+) "
    r"bar_rel=(?P<bar_rel>[01]) "
    r"exec_rdy=(?P<exec_rdy>[01])"
)


@dataclass
class TickSnapshot:
    sim_tick: int
    exec_ready: Optional[int] = None
    local_skew_lead: Optional[int] = None

    def effective_exec_ready(self) -> Optional[int]:
        return self.exec_ready


def _apply_peer_line(cur: TickSnapshot, m: re.Match[str]) -> None:
    assert cur.sim_tick == int(m.group("sim_tick"))
    cur.exec_ready = int(m.group("exec_rdy"))
    hr = int(m.group("hr"))
    cur.local_skew_lead = cur.sim_tick - hr


def _apply_netsync_line(cur: TickSnapshot, m: re.Match[str]) -> None:
    assert cur.sim_tick == int(m.group("tick"))
    cur.exec_ready = int(m.group("exec_rdy"))
    cur.local_skew_lead = int(m.group("tick_minus_hr"))


def parse_log_lines(lines: Iterator[str]) -> Dict[int, TickSnapshot]:
    by_tick: Dict[int, TickSnapshot] = {}

    for line in lines:
        m = PEER_TICK_DIAG_RE.search(line)
        if m:
            t = int(m.group("sim_tick"))
            cur = by_tick.get(t)
            if cur is None:
                cur = TickSnapshot(sim_tick=t)
                by_tick[t] = cur
            _apply_peer_line(cur, m)
            continue

        m = NETSYNC_TICK_DIAG_RE.search(line)
        if m:
            t = int(m.group("tick"))
            cur = by_tick.get(t)
            if cur is None:
                cur = TickSnapshot(sim_tick=t)
                by_tick[t] = cur
            _apply_netsync_line(cur, m)

    return by_tick


def parse_file(path: str) -> Dict[int, TickSnapshot]:
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return parse_log_lines(f)


def analyze(
    host: Dict[int, TickSnapshot],
    client: Dict[int, TickSnapshot],
    compare_local_skew: bool,
    skew_diff_threshold: int,
) -> Tuple[str, Optional[int], Optional[TickSnapshot], Optional[TickSnapshot], Optional[str]]:
    for t in sorted(set(host) & set(client)):
        h, c = host[t], client[t]
        he, ce = h.effective_exec_ready(), c.effective_exec_ready()
        if he is not None and ce is not None and he != ce:
            return (
                "EXEC_READY_MISMATCH",
                t,
                h,
                c,
                "exec_rdy differs at same sim_tick (later NetSync line overwrites NetPeer for that tick)",
            )
        if compare_local_skew:
            hs, cs = h.local_skew_lead, c.local_skew_lead
            if hs is not None and cs is not None and abs(hs - cs) > skew_diff_threshold:
                return (
                    "LOCAL_SKEW_DIFF_HEURISTIC",
                    t,
                    h,
                    c,
                    f"heuristic only: host_skew={hs} client_skew={cs} thr={skew_diff_threshold}",
                )
    return ("NO_MISMATCH", None, None, None, None)


def print_report(
    category: str,
    tick: Optional[int],
    h: Optional[TickSnapshot],
    c: Optional[TickSnapshot],
    detail: Optional[str],
) -> None:
    print("\n===== SSB64 NETPLAY TICK COMPARE (determinism-focused) =====\n")
    if category == "NO_MISMATCH":
        print("No exec_rdy mismatch on aligned sim_ticks.")
        print(detail or "")
        print()
        return
    print(f"FIRST sim_tick: {tick}")
    print(f"SIGNAL: {category}")
    if detail:
        print(f"DETAIL: {detail}\n")
    if h:
        print("HOST:", h)
    if c:
        print("CLIENT:", c)
    print("\n============================================================\n")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("host_log")
    ap.add_argument("client_log")
    ap.add_argument(
        "--compare-local-skew",
        action="store_true",
        help="Heuristic: |host local_skew_lead - client local_skew_lead| vs --skew-diff (default off).",
    )
    ap.add_argument("--skew-diff", type=int, default=0)
    args = ap.parse_args()

    host = parse_file(args.host_log)
    client = parse_file(args.client_log)
    note = (
        "Compared: exec_rdy only. For resolved inputs / commit tokens, extend the parser "
        "(NetSync hist windows, FRAME COMMIT REPORT, etc.)."
    )
    cat, tick, hs, cs, det = analyze(host, client, args.compare_local_skew, args.skew_diff)
    print_report(cat, tick, hs, cs, det if cat != "NO_MISMATCH" else note)


if __name__ == "__main__":
    main()
