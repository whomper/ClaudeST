#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""ext2img.py: a hard-disk image (MBR + one Linux ext2 partition) from a host
folder, for FreeMiNT's ext2.xfs, and the files read back out of one, names in
their exact case (the guidelines, 6.6: on ext2 EWEB.INF and eweb.inf are two
files). Needs e2fsprogs (mke2fs -d, debugfs).

  python3 ext2img.py build FOLDER IMAGE [SIZE_MB]
  python3 ext2img.py ls IMAGE           every file, its path as it is
  python3 ext2img.py extract IMAGE FOLDER

Used by run.py --ext2: the image is Hatari's IDE slave, EmuTOS gives its
partition a drive (D:) and serves it through XHDI, and FreeMiNT mounts it
with ext2.xfs. The file system is ext2 revision 1 with 128-byte inodes and
no optional features, as ext2.xfs (2011) reads it, in 1 KB blocks. Every
file is rw-r--r--, folders and programs (.prg .app .tos .ttp .gtp .acc,
any case) rwxr-xr-x: on ext2 MiNT runs only what has the execute bit, as
someone who copies a program there gives it (chmod +x)."""
import os, shutil, struct, subprocess, sys, tempfile

SECTOR = 512
PART_START = 2048          # the partition's first sector (1 MB in)
MKE2FS = shutil.which("mke2fs") or "/usr/sbin/mke2fs"
DEBUGFS = shutil.which("debugfs") or "/usr/sbin/debugfs"
PROGRAMS = (".prg", ".app", ".tos", ".ttp", ".gtp", ".acc")


def _staged(folder, tmp):
    """a copy of folder with the modes MiNT wants (names untouched)"""
    root = os.path.join(tmp, "root")
    shutil.copytree(folder, root)
    for d, dirs, files in os.walk(root):
        os.chmod(d, 0o755)
        for f in files:
            exe = f.lower().endswith(PROGRAMS)
            os.chmod(os.path.join(d, f), (0o755 if exe else 0o644))
    return root


def build(folder, out, size_mb=16):
    total = size_mb * 1024 * 1024 // SECTOR
    psize = total - PART_START
    with tempfile.TemporaryDirectory() as tmp:
        fs = os.path.join(tmp, "part.ext2")
        with open(fs, "wb") as f:
            f.truncate(psize * SECTOR)
        root = _staged(folder, tmp)
        r = subprocess.run([MKE2FS, "-q", "-F", "-t", "ext2", "-r", "1", "-O", "none", "-I", "128",
                            "-b", "1024", "-m", "0", "-L", "AERA", "-d", root, fs],
                           capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError("mke2fs: " + (r.stderr or r.stdout).strip())
        mbr = bytearray(SECTOR)
        # one primary partition, type 0x83 (Linux), CHS fields "use LBA"
        mbr[0x1BE:0x1CE] = struct.pack("<B3sB3sII", 0x00, b"\xfe\xff\xff", 0x83, b"\xfe\xff\xff",
                                       PART_START, psize)
        mbr[0x1FE:0x200] = b"\x55\xaa"
        with open(out, "wb") as o:
            o.write(mbr)
            o.write(bytes((PART_START - 1) * SECTOR))
            with open(fs, "rb") as f:
                shutil.copyfileobj(f, o, 1 << 20)


def _part(image, tmp):
    """the partition alone, for debugfs (which reads a file system, not a disk)"""
    fs = os.path.join(tmp, "part.ext2")
    with open(image, "rb") as i, open(fs, "wb") as o:
        mbr = i.read(SECTOR)
        start, size = struct.unpack("<II", mbr[0x1C6:0x1CE])
        i.seek(start * SECTOR)
        left = size * SECTOR
        while left:
            b = i.read(min(left, 1 << 20))
            if not b:
                break
            o.write(b)
            left -= len(b)
    return fs


def ls(image):
    """every file and folder on the image, as "path" ("dir/" for folders)"""
    with tempfile.TemporaryDirectory() as tmp:
        fs = _part(image, tmp)
        out = []

        def walk(d, prefix):
            r = subprocess.run([DEBUGFS, "-R", "ls -p %s" % (d or "/"), fs], capture_output=True, text=True)
            for line in r.stdout.splitlines():
                # /ino/mode/uid/gid/name/size/
                p = line.split("/")
                if len(p) < 7 or p[5] in (".", "..", "lost+found", ""):
                    continue
                name, isdir = p[5], int(p[2], 8) & 0o170000 == 0o040000
                out.append(prefix + name + ("/" if isdir else ""))
                if isdir:
                    walk((d or "") + "/" + name, prefix + name + "/")
        walk("", "")
        return out


def extract(image, folder):
    with tempfile.TemporaryDirectory() as tmp:
        fs = _part(image, tmp)
        os.makedirs(folder, exist_ok=True)
        r = subprocess.run([DEBUGFS, "-R", "rdump / %s" % os.path.abspath(folder), fs],
                           capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError("debugfs: " + (r.stderr or r.stdout).strip())
        lf = os.path.join(folder, "lost+found")
        if os.path.isdir(lf) and not os.listdir(lf):
            os.rmdir(lf)


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) >= 3 and a[0] == "build":
        build(a[1], a[2], int(a[3]) if len(a) > 3 else 16)
    elif len(a) == 2 and a[0] == "ls":
        print("\n".join(ls(a[1])))
    elif len(a) == 3 and a[0] == "extract":
        extract(a[1], a[2])
    else:
        sys.exit(__doc__)
