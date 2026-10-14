#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""fetch.py [ZIP]: build/base, the C: drive run.py starts from.

FreeMiNT 1.17.0 (GPL; not kept in this repository) comes from the
Sparemint archive on GitHub, or from ZIP when given, and must match its
SHA-256. From it: AUTO\\MINT030.PRG, the kernel for a 68030, and
MINT\\1-17-0\\XAAES\\, XaAES with its 68030 module as XAAES.KM. Then
ours: AUTO\\OPNWK.PRG (make: opens the VDI's physical workstation, which
XaAES 1.17 expects an AES to have opened), and MINT.CNF, MINT.INI and
XAAES.CNF from cnf/ (no network, no modules, no boot delay, memory
protection off; XaAES starts by itself). C:\\CLIPBRD is the clipboard.

1.18.0's XaAES (the RPMs on Sparemint) crashes at the start on EmuTOS,
and no 1.19 could be fetched: hence 1.17.0.
"""
import hashlib
import os
import shutil
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, "build")
URL = "https://raw.githubusercontent.com/freemint/sparemint/master/mint/kernel/1.17.0/freemint-1.17.0.zip"
SHA256 = "24d3ecfa349798ffa9ebf8998e9119c821e8adf150885ed00442b8e300070d9a"
XA = "mint/1-17-0/xaaes/"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 16), b""):
            h.update(block)
    return h.hexdigest()


def get_zip(given):
    path = given or os.path.join(BUILD, "freemint-1.17.0.zip")
    if not os.path.exists(path):
        os.makedirs(BUILD, exist_ok=True)
        print("fetching %s" % URL)
        part = path + ".part"
        # urllib takes HTTPS_PROXY, and OpenSSL SSL_CERT_FILE, from the environment
        with urllib.request.urlopen(URL, timeout=120) as r, open(part, "wb") as f:
            shutil.copyfileobj(r, f)
        os.replace(part, path)
    got = sha256(path)
    if got != SHA256:
        sys.exit("%s: SHA-256 %s, not FreeMiNT 1.17.0's %s" % (path, got, SHA256))
    return path


def main():
    z = zipfile.ZipFile(get_zip(sys.argv[1] if len(sys.argv) > 1 else None))
    base = os.path.join(BUILD, "base")
    shutil.rmtree(base, ignore_errors=True)
    os.makedirs(os.path.join(base, "auto"))

    def put(member, dest):
        dest = os.path.join(base, dest)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with z.open(member) as s, open(dest, "wb") as d:
            shutil.copyfileobj(s, d)

    put("auto/mint030.prg", "auto/mint030.prg")
    for m in z.namelist():
        if not m.startswith(XA) or m.endswith("/"):
            continue
        rel = m[len(XA):]
        name = os.path.basename(rel)
        if name == "xaaes030.km":
            put(m, XA + "xaaes.km")
        elif name.endswith(".km") or name in ("xaaes_de.rsc", "xa_he_de.txt", "xaaes.cnf"):
            continue		# the other CPUs' modules, German, the example settings
        else:
            put(m, XA + rel)
    shutil.copy(os.path.join(BUILD, "OPNWK.PRG"), os.path.join(base, "auto", "opnwk.prg"))
    shutil.copy(os.path.join(HERE, "cnf", "mint.cnf"), os.path.join(base, "mint/1-17-0/mint.cnf"))
    shutil.copy(os.path.join(HERE, "cnf", "mint.ini"), os.path.join(base, "mint/1-17-0/mint.ini"))
    shutil.copy(os.path.join(HERE, "cnf", "xaaes.cnf"), os.path.join(base, XA + "xaaes.cnf"))
    os.makedirs(os.path.join(base, "clipbrd"))
    n = sum(len(f) for _, _, f in os.walk(base))
    print("%s: %d files" % (base, n))


if __name__ == "__main__":
    main()
