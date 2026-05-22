#!/usr/bin/env python3
"""Triage SetSlopeContour sites in fighter MainMotion scripts.

Outputs CSV + markdown summary with ±25 attack-coll context and ±50 catch/throw
context for class B vs D classification.
"""

from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
RELOC_DIR = REPO_ROOT / "decomp" / "src" / "relocData"
FTDATA_PATH = REPO_ROOT / "decomp" / "src" / "ft" / "ftdata.c"

MAIN_MOTION_GLOB = "*MainMotion.c"
SKIP_FILES = {"205_MMarioMainMotion.c", "249_BossMainMotion.c"}

WINDOW_ATTACK = 25
WINDOW_CATCH = 50
GRAB_ANGLE = 361

RE_SLOPE = re.compile(r"ftMotionCommandSetSlopeContour\((\d+)\)")
RE_ATTACK = re.compile(
    r"ftMotionCommandMakeAttackColl\([^,]+,[^,]+,[^,]+,[^,]+,[^,]+,[^,]+,"
    r"[^,]+,[^,]+,[^,]+,[^,]+,\s*(-?\d+),\s*"
)
RE_SETTHROW = re.compile(r"ftMotionCommandSetThrow\(")
RE_CATCH_FGM = re.compile(r"ftMotionPlayFGM\(nSYAudioFGMCatch\)")
RE_SCRIPT_START = re.compile(r"^u32 (d\w+MainMotion_0x[0-9A-Fa-f]+)\[\] = \{")
RE_CATCH_FTDATA = re.compile(
    r"\{\s*&llFT\w+AnimCatchFileID,\s*(d\w+MainMotion_0x[0-9A-Fa-f]+(?:\s*\+\s*0x[0-9A-Fa-f]+)?)"
)


@dataclass
class Command:
    index: int
    line_no: int
    text: str
    kind: str
    slope_flags: int | None = None
    attack_angle: int | None = None


@dataclass
class Script:
    symbol: str
    file_name: str
    commands: list[Command] = field(default_factory=list)


def parse_catch_scripts(ftdata_text: str) -> dict[str, set[str]]:
    """Map MainMotion.c basename -> set of standing-grab script symbols."""
    by_file: dict[str, set[str]] = {}
    for match in RE_CATCH_FTDATA.finditer(ftdata_text):
        symbol = match.group(1).replace(" ", "")
        if "+" in symbol:
            continue
        file_hint = symbol.split("_")[0].replace("d", "", 1)
        for path in RELOC_DIR.glob(MAIN_MOTION_GLOB):
            if path.name in SKIP_FILES:
                continue
            if file_hint.lower() in path.name.lower():
                by_file.setdefault(path.name, set()).add(symbol)
                break
    return by_file


def parse_motion_file(path: Path) -> list[Script]:
    scripts: list[Script] = []
    current: Script | None = None
    cmd_index = 0

    with path.open(encoding="utf-8", errors="replace") as fh:
        for line_no, raw in enumerate(fh, 1):
            line = raw.strip()
            m_script = RE_SCRIPT_START.match(line)
            if m_script:
                if current is not None:
                    scripts.append(current)
                current = Script(symbol=m_script.group(1), file_name=path.name)
                cmd_index = 0
                continue
            if current is None:
                continue
            if not line or line.startswith("//") or line.startswith("/*"):
                continue
            if line.startswith("};"):
                scripts.append(current)
                current = None
                cmd_index = 0
                continue
            if not line.endswith(","):
                continue
            entry = line.rstrip(",").strip()
            cmd = Command(index=cmd_index, line_no=line_no, text=entry, kind="other")
            sm = RE_SLOPE.search(entry)
            if sm:
                cmd.kind = "slope"
                cmd.slope_flags = int(sm.group(1))
            am = RE_ATTACK.search(entry)
            if am:
                cmd.kind = "attack"
                cmd.attack_angle = int(am.group(1))
            elif RE_SETTHROW.search(entry):
                cmd.kind = "setthrow"
            elif RE_CATCH_FGM.search(entry):
                cmd.kind = "catch_fgm"
            current.commands.append(cmd)
            cmd_index += 1
    return scripts


def nearest_of_kind(
    commands: list[Command], origin: int, kind: str, window: int
) -> tuple[int | None, Command | None]:
    best_dist: int | None = None
    best_cmd: Command | None = None
    for cmd in commands:
        if cmd.kind != kind:
            continue
        dist = abs(cmd.index - origin)
        if dist > window:
            continue
        if best_dist is None or dist < best_dist:
            best_dist = dist
            best_cmd = cmd
    return best_dist, best_cmd


def has_in_window(
    commands: list[Command], origin: int, predicate, window: int
) -> bool:
    for cmd in commands:
        if abs(cmd.index - origin) > window:
            continue
        if predicate(cmd):
            return True
    return False


def suggest_class(
    slope_flags: int,
    class_b: bool,
    has_361_in_50: bool,
    has_setthrow_in_50: bool,
    catch_reachable: bool,
) -> str:
    if class_b:
        return "B"
    if slope_flags == 0 and has_361_in_50:
        return "C"
    if has_361_in_50 and not has_setthrow_in_50:
        return "D"
    if has_361_in_50:
        return "E"
    if catch_reachable and slope_flags in (3, 4):
        return "B?"
    if slope_flags in (3, 4):
        return "A"
    return "orphan"


def triage_site(script: Script, cmd: Command, catch_symbols: set[str]) -> dict:
    cmds = script.commands
    idx = cmd.index

    ac_dist, ac_cmd = nearest_of_kind(cmds, idx, "attack", WINDOW_ATTACK)
    st_dist, _ = nearest_of_kind(cmds, idx, "setthrow", WINDOW_CATCH)
    fgm_dist, _ = nearest_of_kind(cmds, idx, "catch_fgm", WINDOW_CATCH)

    has_361_50 = has_in_window(
        cmds, idx, lambda c: c.kind == "attack" and c.attack_angle == GRAB_ANGLE, WINDOW_CATCH
    )
    has_setthrow_50 = has_in_window(cmds, idx, lambda c: c.kind == "setthrow", WINDOW_CATCH)
    class_b = has_361_50 and has_setthrow_50
    catch_reachable = script.symbol in catch_symbols

    suggested = suggest_class(
        cmd.slope_flags or 0,
        class_b,
        has_361_50,
        has_setthrow_50,
        catch_reachable,
    )

    tags = []
    if class_b:
        tags.append("class_b_unambiguous")
    if ac_cmd is not None:
        tags.append("attack-coll-adjacent")
    if st_dist is not None or fgm_dist is not None:
        tags.append("grab-adjacent")
    if catch_reachable:
        tags.append("catch_script")
    if not tags:
        tags.append("locomotion-only" if cmd.slope_flags in (3, 4) else "orphan")

    return {
        "file": script.file_name,
        "script": script.symbol,
        "line": cmd.line_no,
        "cmd_index": idx,
        "slope_flags": cmd.slope_flags,
        "nearest_attack_coll_dist": ac_dist if ac_dist is not None else "",
        "nearest_attack_angle": ac_cmd.attack_angle if ac_cmd else "",
        "nearest_setthrow_dist": st_dist if st_dist is not None else "",
        "nearest_catch_fgm_dist": fgm_dist if fgm_dist is not None else "",
        "class_b_unambiguous": class_b,
        "catch_script_reachable": catch_reachable,
        "suggested_class": suggested,
        "tags": "|".join(tags),
    }


def write_markdown_summary(rows: list[dict], out_path: Path) -> None:
    counts: dict[str, int] = {}
    for row in rows:
        counts[row["suggested_class"]] = counts.get(row["suggested_class"], 0) + 1
        if row["class_b_unambiguous"]:
            counts["class_b_unambiguous"] = counts.get("class_b_unambiguous", 0) + 1

    lines = [
        "# SetSlopeContour triage summary (generated)",
        "",
        f"Total sites: **{len(rows)}**",
        "",
        "## Counts by suggested_class",
        "",
    ]
    for key in sorted(counts.keys()):
        lines.append(f"- {key}: {counts[key]}")
    lines.extend(["", "## Priority ROM fixes (class B unambiguous)", ""])
    for row in rows:
        if not row["class_b_unambiguous"]:
            continue
        lines.append(
            f"- `{row['file']}` `{row['script']}` idx={row['cmd_index']} flags={row['slope_flags']}"
        )
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--csv",
        type=Path,
        default=REPO_ROOT / "build" / "slope_contour_audit.csv",
    )
    parser.add_argument(
        "--md",
        type=Path,
        default=REPO_ROOT / "build" / "slope_contour_audit_summary.md",
    )
    args = parser.parse_args()

    ftdata_text = FTDATA_PATH.read_text(encoding="utf-8", errors="replace")
    catch_by_file = parse_catch_scripts(ftdata_text)

    rows: list[dict] = []
    for path in sorted(RELOC_DIR.glob(MAIN_MOTION_GLOB)):
        if path.name in SKIP_FILES:
            continue
        catch_symbols = catch_by_file.get(path.name, set())
        for script in parse_motion_file(path):
            for cmd in script.commands:
                if cmd.kind != "slope":
                    continue
                rows.append(triage_site(script, cmd, catch_symbols))

    args.csv.parent.mkdir(parents=True, exist_ok=True)
    if rows:
        with args.csv.open("w", newline="", encoding="utf-8") as fh:
            writer = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)
    write_markdown_summary(rows, args.md)
    print(f"Wrote {len(rows)} sites to {args.csv}")
    print(f"Summary: {args.md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
