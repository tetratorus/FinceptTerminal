#!/usr/bin/env python3
"""Retarget a Qt for iOS install's arm64 (device) slices to the arm64 simulator.

Qt's prebuilt iOS packages are fat static libs: x86_64 = simulator, arm64 = device.
Apple Silicon simulators on iOS 26+ only run arm64, so the device slice is relabelled
as iOS-simulator (LC_BUILD_VERSION platform 2 -> 7) in place. Run it on an APFS clone
of the iOS kit (cp -Rc ios ios-sim), never on the one used for device builds.

usage: retarget_qt_arm64_simulator.py <qt-ios-dir>
"""
import mmap, os, struct, sys

FAT_MAGIC, MH_MAGIC_64 = 0xCAFEBABE, 0xFEEDFACF
CPU_ARM64 = 0x0100000C
LC_BUILD_VERSION, LC_VERSION_MIN_IPHONEOS = 0x32, 0x25
PLATFORM_IOS, PLATFORM_IOSSIMULATOR = 2, 7
stats = {"patched": 0, "legacy": 0}


def patch_macho(m, off):
    magic, cpu, _, _, ncmds, _, _, _ = struct.unpack_from("<IiiIIIII", m, off)
    if magic != MH_MAGIC_64 or cpu != CPU_ARM64:
        return
    p = off + 32
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", m, p)
        if cmd == LC_BUILD_VERSION and struct.unpack_from("<I", m, p + 8)[0] == PLATFORM_IOS:
            struct.pack_into("<I", m, p + 8, PLATFORM_IOSSIMULATOR)
            stats["patched"] += 1
        elif cmd == LC_VERSION_MIN_IPHONEOS:
            stats["legacy"] += 1
        p += size


def patch_archive(m, off, end):
    p = off + 8
    while p + 60 <= end:
        name, size = m[p:p + 16].decode().strip(), int(m[p + 48:p + 58].decode().strip())
        data = p + 60
        if name.startswith("#1/"):
            data += int(name[3:])
        patch_macho(m, data)
        p += 60 + size + (size & 1)


def patch_slice(m, off, end):
    if m[off:off + 8] == b"!<arch>\n":
        patch_archive(m, off, end)
    else:
        patch_macho(m, off)


def patch_file(path):
    with open(path, "r+b") as f:
        if os.fstat(f.fileno()).st_size < 32:
            return
        m = mmap.mmap(f.fileno(), 0)
        if struct.unpack_from(">I", m, 0)[0] == FAT_MAGIC:
            for i in range(struct.unpack_from(">I", m, 4)[0]):
                cpu, _, off, size, _ = struct.unpack_from(">iiIII", m, 8 + 20 * i)
                if cpu == CPU_ARM64:
                    patch_slice(m, off, off + size)
        else:
            patch_slice(m, 0, len(m))
        m.flush()
        m.close()


for root, _, files in os.walk(sys.argv[1]):
    for fn in files:
        path = os.path.join(root, fn)
        if os.path.islink(path) or "Headers" in root:
            continue
        if fn.endswith((".a", ".o")) or (".framework" in root and fn == root.rsplit("/", 1)[-1].split(".framework")[0]):
            patch_file(path)
print(f"retargeted {stats['patched']} objects; {stats['legacy']} use LC_VERSION_MIN (not patched)")
