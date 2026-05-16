# shellcheck shell=bash
# Shared PKGBUILD patches for x86_64-only MinGW cross toolchain.

patch_gettext_pkgbuild() {
    local dir="${PARU_CLONE:-$HOME/.cache/paru/clone}/mingw-w64-gettext"
    local pkgbuild="$dir/PKGBUILD"
    [[ -f "$pkgbuild" ]] || return 0
    if grep -q '_Countof/_countof' "$pkgbuild" 2>/dev/null; then
        return 0
    fi
    echo "==> mingw-w64-gettext: applying _Countof + x86_64-only patch"
    python3 - "$pkgbuild" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
text = p.read_text()
for old in (
    "_architectures='i686-w64-mingw32 x86_64-w64-mingw32'",
    '_architectures="i686-w64-mingw32 x86_64-w64-mingw32"',
):
    text = text.replace(old, "_architectures='x86_64-w64-mingw32'")
needle = "  patch -p1 -i ../0024-disable-gnu-format.patch\n}"
if needle in text and "_Countof/_countof" not in text:
    text = text.replace(
        needle,
        "  patch -p1 -i ../0024-disable-gnu-format.patch\n"
        "  find . -path '*/gnulib-lib/options.h' -exec sed -i 's/_Countof/_countof/g' {} +\n}",
    )
text = text.replace(
    "export CFLAGS=-fpermissive CXXFLAGS=-fpermissive",
    "export CFLAGS='-fpermissive -D_Countof=_countof'\n"
    "    export CXXFLAGS='-fpermissive -D_Countof=_countof'",
)
p.write_text(text)
PY
}

patch_mingw_x86_64_only() {
    local name="$1"
    local dir="${PARU_CLONE:-$HOME/.cache/paru/clone}/$name"
    local f="$dir/PKGBUILD"
    [[ -f "$f" ]] || return 0

    if [[ "$name" == "mingw-w64-gettext" ]]; then
        patch_gettext_pkgbuild
    fi

    if ! grep -qE 'i686-w64-mingw32' "$f"; then
        return 0
    fi

    python3 - "$f" <<'PY'
import re
import sys
from pathlib import Path
p = Path(sys.argv[1])
text = p.read_text()
orig = text
text = re.sub(
    r"_architectures=['\"]i686-w64-mingw32 x86_64-w64-mingw32['\"]",
    "_architectures='x86_64-w64-mingw32'",
    text,
)
text = re.sub(
    r'_architectures=\(\s*"i686-w64-mingw32"\s+"x86_64-w64-mingw32"\s*\)',
    "_architectures=('x86_64-w64-mingw32')",
    text,
)
text = re.sub(
    r"_archs=\(\s*'i686-w64-mingw32'\s+'x86_64-w64-mingw32'\s*\)",
    "_archs=('x86_64-w64-mingw32')",
    text,
)
if text != orig:
    p.write_text(text)
PY

    echo "==> $name: PKGBUILD set to x86_64-w64-mingw32 only"
    find "$dir" -maxdepth 5 -type d -name 'build-i686-w64-mingw32' -prune -exec rm -rf {} + 2>/dev/null || true
}
