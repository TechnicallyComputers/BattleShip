#!/usr/bin/env python3
"""
Compare two NetPeer bootstrap_readiness logs keyed by sequence field `n=`.

Usage:
  ./scripts/diff-bootstrap-readiness-logs.py peer_a.log peer_b.log

Environment (optional):
  SSB64_BOOTSTRAP_DIFF_IGNORE_FIELDS — comma-separated keys to skip when comparing
    (default: path,lx_exec,lx_run_sim — skew slice vs main update send different tags).

Exit status: 0 if no divergence in overlapping n, 1 if divergence or missing keys.
"""

from __future__ import annotations

import os
import re
import sys

MARKER = "bootstrap_readiness"
FIELD_RE = re.compile(r"\b([a-zA-Z_][a-zA-Z0-9_]*)=(-?\d+)\b")


def parse_line(line: str) -> dict[str, str] | None:
    if MARKER not in line:
        return None
    fields = dict(FIELD_RE.findall(line))
    if not fields or "n" not in fields:
        return None
    return fields


def load_by_n(path: str) -> tuple[dict[str, dict[str, str]], dict[str, str]]:
    """First occurrence wins per n. Also keep raw first line per n for display."""
    out: dict[str, dict[str, str]] = {}
    raw: dict[str, str] = {}
    seen: set[str] = set()
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            rec = parse_line(line)
            if rec is None:
                continue
            n = rec["n"]
            if n not in seen:
                seen.add(n)
                out[n] = rec
                raw[n] = line.rstrip("\n")
    return out, raw


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    path_a, path_b = sys.argv[1], sys.argv[2]
    ignore_env = os.environ.get("SSB64_BOOTSTRAP_DIFF_IGNORE_FIELDS", "path,lx_exec,lx_run_sim")
    ignore = {x.strip() for x in ignore_env.split(",") if x.strip()}

    by_a, raw_a = load_by_n(path_a)
    by_b, raw_b = load_by_n(path_b)

    print(f"A: {path_a}  ({len(by_a)} bootstrap_readiness lines)")
    print(f"B: {path_b}  ({len(by_b)} bootstrap_readiness lines)")
    print(f"Ignoring fields: {sorted(ignore) or '(none)'}")
    print()

    all_n = sorted(set(by_a.keys()) | set(by_b.keys()), key=lambda x: int(x))
    for n in all_n:
        if n not in by_a:
            print(f"FIRST divergence: n={n} missing from A (present only in B).")
            print(f"  B line: {raw_b.get(n, '')}")
            return 1
        if n not in by_b:
            print(f"FIRST divergence: n={n} missing from B (present only in A).")
            print(f"  A line: {raw_a.get(n, '')}")
            return 1
        ra, rb = by_a[n], by_b[n]
        diff: dict[str, tuple[str, str]] = {}
        for k in sorted(set(ra.keys()) | set(rb.keys())):
            if k in ignore:
                continue
            va = ra.get(k, "")
            vb = rb.get(k, "")
            if va != vb:
                diff[k] = (va, vb)
        if diff:
            print(f"FIRST divergence at n={n}")
            print(f"  A line: {raw_a[n]}")
            print(f"  B line: {raw_b[n]}")
            print("  Differing fields:")
            for k in sorted(diff.keys()):
                va, vb = diff[k]
                print(f"    {k}:  A={va}  B={vb}")
            return 1

    print("No divergence: same n keys and all compared fields match.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
