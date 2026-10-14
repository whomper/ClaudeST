# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""fatimg.py: make a hard-disk image (MBR + one FAT16 partition) from a host folder,
and read files back out of one. No mtools needed.

  python3 fatimg.py build FOLDER IMAGE [SIZE_MB]
  python3 fatimg.py extract IMAGE FOLDER

Names must be 8.3 (they are upper-cased). Used by run.py: FreeMiNT cannot see
Hatari's GEMDOS-emulated drive (its FAT driver reads sectors through the BIOS),
so C: is an IDE image that EmuTOS and FreeMiNT both read."""
import os, struct, sys, time

SECTOR = 512
SPC = 4                # sectors per cluster (2 KB)
RESERVED = 1
NFATS = 2
ROOT_ENTRIES = 512
PART_START = 63


def dos_time(t):
    lt = time.localtime(t)
    d = ((max(lt.tm_year, 1980) - 1980) << 9) | (lt.tm_mon << 5) | lt.tm_mday
    tt = (lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec // 2)
    return d, tt


def name83(name):
    n = name.upper()
    if n in (".", ".."):
        return n.ljust(11).encode()
    base, dot, ext = n.partition(".")
    if not base or len(base) > 8 or len(ext) > 3 or "." in ext:
        raise ValueError("not an 8.3 name: %r" % name)
    ok = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-!#$%&'()@^`{}~")
    if not set(base + ext) <= ok:
        raise ValueError("bad character in name: %r" % name)
    return (base.ljust(8) + ext.ljust(3)).encode()


def dirent(raw11, attr, cluster, size, mtime):
    d, t = dos_time(mtime)
    return struct.pack("<11sBBBHHHHHHHI", raw11, attr, 0, 0, t, d, d, 0, t, d, cluster, size)


class Builder:
    def __init__(self, size_mb):
        self.total = size_mb * 1024 * 1024 // SECTOR       # whole disk
        self.psize = self.total - PART_START                # partition sectors
        root_secs = ROOT_ENTRIES * 32 // SECTOR
        fat_secs = 1
        while True:   # find the FAT size that fits the cluster count
            data = self.psize - RESERVED - NFATS * fat_secs - root_secs
            clusters = data // SPC
            need = ((clusters + 2) * 2 + SECTOR - 1) // SECTOR
            if need <= fat_secs:
                break
            fat_secs = need
        assert 4085 <= clusters < 65525, clusters
        self.fat_secs, self.root_secs, self.clusters = fat_secs, root_secs, clusters
        self.data_start = RESERVED + NFATS * fat_secs + root_secs   # in partition sectors
        self.fat = [0] * (clusters + 2)
        self.fat[0], self.fat[1] = 0xFFF8, 0xFFFF
        self.next = 2
        self.img = bytearray(self.total * SECTOR)

    def alloc(self, nbytes):
        n = max(1, (nbytes + SPC * SECTOR - 1) // (SPC * SECTOR))
        first = self.next
        if first + n > self.clusters + 2:
            raise RuntimeError("image full")
        for c in range(first, first + n - 1):
            self.fat[c] = c + 1
        self.fat[first + n - 1] = 0xFFFF
        self.next += n
        return first

    def write_clusters(self, first, data):
        off = (PART_START + self.data_start + (first - 2) * SPC) * SECTOR
        self.img[off:off + len(data)] = data

    def add_dir(self, path, cluster, parent_cluster):
        """Write the directory at host PATH; cluster None means the root."""
        entries = []
        if cluster is not None:
            mt = os.path.getmtime(path)
            entries.append(dirent(name83("."), 0x10, cluster, 0, mt))
            entries.append(dirent(name83(".."), 0x10, parent_cluster or 0, 0, mt))
        else:
            entries.append(dirent(b"MINTHD     ", 0x08, 0, 0, time.time()))
        subdirs = []
        seen = set()
        for name in sorted(os.listdir(path)):
            full = os.path.join(path, name)
            raw = name83(name)
            if raw in seen:
                raise ValueError("two files map to %r" % raw)
            seen.add(raw)
            mt = os.path.getmtime(full)
            if os.path.isdir(full):
                n = len(os.listdir(full)) + 2
                c = self.alloc(n * 32)
                entries.append(dirent(raw, 0x10, c, 0, mt))
                subdirs.append((full, c))
            else:
                data = open(full, "rb").read()
                c = self.alloc(len(data)) if data else 0
                if data:
                    self.write_clusters(c, data)
                entries.append(dirent(raw, 0x20, c, len(data), mt))
        blob = b"".join(entries)
        if cluster is None:
            if len(entries) > ROOT_ENTRIES:
                raise RuntimeError("too many files in the root")
            off = (PART_START + RESERVED + NFATS * self.fat_secs) * SECTOR
            self.img[off:off + len(blob)] = blob
        else:
            self.write_clusters(cluster, blob)
        for full, c in subdirs:
            self.add_dir(full, c, cluster)

    def finish(self, out):
        # MBR
        mbr = bytearray(SECTOR)
        ptype = 0x04 if self.psize < 65536 else 0x06
        mbr[0x1BE:0x1CE] = struct.pack("<B3sB3sII", 0x80, b"\xfe\xff\xff", ptype, b"\xfe\xff\xff",
                                       PART_START, self.psize)
        mbr[0x1B8:0x1BC] = struct.pack("<I", 0x4D494E54)
        mbr[510:512] = b"\x55\xaa"
        self.img[0:SECTOR] = mbr
        # boot sector of the partition
        bs = bytearray(SECTOR)
        bs[0:3] = b"\xeb\x3c\x90"
        bs[3:11] = b"MSWIN4.1"
        small = self.psize if self.psize < 65536 else 0
        big = 0 if small else self.psize
        bs[11:36] = struct.pack("<HBHBHHBHHHII", SECTOR, SPC, RESERVED, NFATS, ROOT_ENTRIES, small,
                                0xF8, self.fat_secs, 32, 64, PART_START, big)
        bs[36:62] = struct.pack("<BBBI11s8s", 0x80, 0, 0x29, 0x4D494E54, b"MINTHD     ", b"FAT16   ")
        bs[510:512] = b"\x55\xaa"
        off = PART_START * SECTOR
        self.img[off:off + SECTOR] = bs
        fat = struct.pack("<%dH" % len(self.fat), *self.fat)
        for i in range(NFATS):
            o = (PART_START + RESERVED + i * self.fat_secs) * SECTOR
            self.img[o:o + len(fat)] = fat
        with open(out, "wb") as f:
            f.write(self.img)


def build(folder, out, size_mb=32):
    b = Builder(size_mb)
    b.add_dir(folder, None, None)
    b.finish(out)
    return b.next - 2, b.clusters


def extract(image, folder):
    img = open(image, "rb").read()
    start = struct.unpack_from("<I", img, 0x1BE + 8)[0]
    bs = start * SECTOR
    bps, spc, res, nfats, rootn, small, _m, fats = struct.unpack_from("<HBHBHHBH", img, bs + 11)
    fat_off = bs + res * bps
    root_off = fat_off + nfats * fats * bps
    data_off = root_off + rootn * 32
    csize = spc * bps

    def chain(c):
        while 2 <= c < 0xFFF8:
            yield c
            c = struct.unpack_from("<H", img, fat_off + 2 * c)[0]

    def read_chain(c):
        return b"".join(img[data_off + (x - 2) * csize:data_off + (x - 1) * csize] for x in chain(c))

    def walk(raw, dest):
        os.makedirs(dest, exist_ok=True)
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] == 0x0F or e[11] & 0x08:
                continue
            base = e[0:8].decode("latin-1").rstrip()
            ext = e[8:11].decode("latin-1").rstrip()
            if base in (".", ".."):
                continue
            name = base + ("." + ext if ext else "")
            if "/" in name or name in (".", ".."):
                continue
            c = struct.unpack_from("<H", e, 26)[0]
            size = struct.unpack_from("<I", e, 28)[0]
            path = os.path.join(dest, name)
            if e[11] & 0x10:
                walk(read_chain(c), path)
            else:
                with open(path, "wb") as f:
                    f.write(read_chain(c)[:size] if size else b"")

    walk(img[root_off:root_off + rootn * 32], folder)


if __name__ == "__main__":
    if sys.argv[1] == "build":
        used, total = build(sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 32)
        print("%s: %d of %d clusters used" % (sys.argv[3], used, total))
    elif sys.argv[1] == "extract":
        extract(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)
