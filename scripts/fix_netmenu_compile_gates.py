#!/usr/bin/env python3
"""Wrap #ifdef PORT blocks containing syNet* with SSB64_NETMENU compile gate."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NETMENU = "#if defined(PORT) && defined(SSB64_NETMENU)"

FILES = [
    "decomp/src/ft/ftchar/ftness/ftnessspecialn.c",
    "decomp/src/ft/ftchar/ftness/ftnessspeciallw.c",
    "decomp/src/ft/ftchar/ftpikachu/ftpikachuspecialn.c",
    "decomp/src/ft/ftchar/ftpikachu/ftpikachuspeciallw.c",
    "decomp/src/ft/ftchar/ftkirby/ftkirbycopymariospecialn.c",
    "decomp/src/ft/ftchar/ftkirby/ftkirbycopypikachuspecialn.c",
    "decomp/src/ft/ftchar/ftkirby/ftkirbycopysamusspecialn.c",
    "decomp/src/ft/ftchar/ftsamus/ftsamusspecialn.c",
    "decomp/src/ft/ftchar/ftyoshi/ftyoshispecialhi.c",
    "decomp/src/ft/ftcommon/ftcommontwister.c",
    "decomp/src/gm/gmcollision.c",
    "decomp/src/sys/objanim.c",
    "decomp/src/wp/wpness/wpnesspkthunder.c",
    "decomp/src/it/itcommon/itstarrod.c",
    "decomp/src/it/itcommon/itfflower.c",
    "decomp/src/it/itcommon/itlgun.c",
    "decomp/src/it/itground/ithitokage.c",
    "decomp/src/gr/grcommon/grjungle.c",
    "decomp/src/gr/grcommon/grhyrule.c",
    "decomp/src/gr/grcommon/grzebes.c",
    "decomp/src/gr/grcommon/grsector.c",
    "decomp/src/gr/grcommon/gryoster.c",
    "decomp/src/gr/grcommon/gryamabuki.c",
    "decomp/src/ft/ftcommon/ftcommonrebirth.c",
]

OPENERS = (
    "#ifdef PORT\n",
    "#if defined(PORT)\n",
)


def scan_if_block(text: str, start: int) -> tuple[int, int, str] | None:
    """Return (body_start, end_after_endif, opener_line) for #if at start."""
    for opener in OPENERS:
        if text.startswith(opener, start):
            opener_line = opener.rstrip("\n")
            body_start = start + len(opener)
            depth = 1
            p = body_start
            n = len(text)
            while p < n:
                nl = text.find("\n", p)
                if nl == -1:
                    nl = n
                line = text[p:nl]
                stripped = line.strip()
                if stripped.startswith("#if"):
                    depth += 1
                elif stripped.startswith("#endif"):
                    depth -= 1
                    if depth == 0:
                        return body_start, nl + 1, opener_line
                p = nl + 1
            return None
    if text.startswith("#if defined(PORT) && defined(SSB64_NETMENU)\n", start):
        return None
    return None


def wrap_sy_net_blocks(text: str) -> str:
    changed = True
    while changed:
        changed = False
        i = 0
        while i < len(text):
            blk = scan_if_block(text, i)
            if blk is None:
                i += 1
                continue
            body_start, end, opener_line = blk
            body = text[body_start : end - 1]
            # trim to before #endif
            body = re.sub(r"#endif\s*$", "", body.rstrip("\n"))
            if "syNet" in body and "SSB64_NETMENU" not in opener_line:
                replacement = NETMENU + "\n" + body + "\n#endif\n"
                text = text[:i] + replacement + text[end:]
                changed = True
                i += len(replacement)
            else:
                i = end
    return text


def wrap_net_includes(text: str) -> str:
    for opener in OPENERS:
        idx = 0
        while True:
            pos = text.find(opener, idx)
            if pos == -1:
                break
            blk = scan_if_block(text, pos)
            if blk is None:
                idx = pos + 1
                continue
            body_start, end, opener_line = blk
            body = text[body_start : end - 1]
            body = re.sub(r"#endif\s*$", "", body.rstrip("\n"))
            if "<sys/net" in body and "SSB64_NETMENU" not in opener_line:
                text = text[:pos] + NETMENU + "\n" + body + "\n#endif\n" + text[end:]
                idx = pos + len(NETMENU) + 1
            else:
                idx = end
    return text


def main() -> int:
    n = 0
    for rel in FILES:
        path = ROOT / rel
        if not path.exists():
            print("missing", rel)
            continue
        orig = path.read_text()
        new = wrap_net_includes(wrap_sy_net_blocks(orig))
        if new != orig:
            path.write_text(new)
            n += 1
            print("updated", rel)
    print("total", n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
