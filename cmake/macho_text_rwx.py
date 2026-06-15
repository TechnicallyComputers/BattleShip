#!/usr/bin/env python3
"""Raise the __TEXT segment's maxprot to rwx in a Mach-O binary.

Why: the mod hook backend (funchook) rewrites engine function prologues
at runtime. Apple Silicon caps a normally-linked __TEXT segment at
maxprot = r-x, so the kernel refuses every attempt to make those pages
writable (plain mprotect AND mach_vm_protect+VM_PROT_COPY both EACCES),
and funchook_install fails. Bumping maxprot to rwx lets a JIT-entitled,
(ad-hoc-)signed process flip a page RW to patch it, then restore R-X.
initprot is left untouched (r-x) so the segment still loads
read-only/executable normally — only the *ceiling* is raised.

`-Wl,-segprot` is the linker-level equivalent, but Apple's new linker
(ld-prime) silently ignores it, so we edit the load command directly.
Run BEFORE codesign so the signature seals the modified binary.

Handles thin arm64 and fat/universal Mach-O. Idempotent.
"""
import struct
import sys

VM_PROT_READ, VM_PROT_WRITE, VM_PROT_EXECUTE = 1, 2, 4
RWX = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE

MH_MAGIC_64, MH_CIGAM_64 = 0xFEEDFACF, 0xCFFAEDFE
FAT_MAGIC, FAT_CIGAM = 0xCAFEBABE, 0xBEBAFECE
LC_SEGMENT_64 = 0x19


def patch_macho_at(buf, base):
    """Patch the __TEXT LC_SEGMENT_64.maxprot of one Mach-O at offset base.

    Self-detects this slice's endianness from its magic.
    """
    if struct.unpack_from("<I", buf, base)[0] == MH_MAGIC_64:
        e = "<"
    elif struct.unpack_from(">I", buf, base)[0] == MH_MAGIC_64:
        e = ">"
    else:
        return False
    ncmds = struct.unpack_from(e + "I", buf, base + 16)[0]
    off = base + 32  # sizeof(mach_header_64)
    changed = False
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from(e + "II", buf, off)
        if cmd == LC_SEGMENT_64:
            segname = buf[off + 8: off + 24].split(b"\0", 1)[0]
            if segname == b"__TEXT":
                # segment_command_64: maxprot @ +56, initprot @ +60
                maxprot_off = off + 56
                maxprot = struct.unpack_from(e + "i", buf, maxprot_off)[0]
                if maxprot != RWX:
                    struct.pack_into(e + "i", buf, maxprot_off, RWX)
                    changed = True
                break
        off += cmdsize
    return changed


def main(path):
    with open(path, "rb") as f:
        buf = bytearray(f.read())

    # fat_header is always big-endian on disk.
    be_magic = struct.unpack_from(">I", buf, 0)[0]
    any_changed = False

    if be_magic in (FAT_MAGIC, FAT_CIGAM):
        nfat = struct.unpack_from(">I", buf, 4)[0]
        for i in range(nfat):
            # fat_arch (big-endian): offset @ +8 of each 20-byte entry,
            # after the 8-byte fat_header.
            ent = 8 + i * 20
            arch_off = struct.unpack_from(">I", buf, ent + 8)[0]
            any_changed |= patch_macho_at(buf, arch_off)
    else:
        any_changed |= patch_macho_at(buf, 0)

    if any_changed:
        with open(path, "wb") as f:
            f.write(buf)
        print("macho_text_rwx: __TEXT maxprot -> rwx in %s" % path)
    else:
        print("macho_text_rwx: __TEXT maxprot already rwx in %s (noop)" % path)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: macho_text_rwx.py <mach-o>")
    main(sys.argv[1])
