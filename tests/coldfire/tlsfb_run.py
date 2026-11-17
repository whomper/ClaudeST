#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""tlsfb_run.py - test_tlsfb's servers, and the run.

Makes two authorities (RSA 2048, ECDSA P-256) and the certificates for
"localhost" they sign (RSA 2048, ECDSA P-256, ECDSA P-384), and a third
authority not loaded with one of its own, with openssl into WORK; then
TLS 1.2 servers on 127.0.0.1 (Python's ssl), one port each, in
test_tlsfb.c's order (its cases name them by number), and runs

    QEMU... TEST OVL WORK/ca.pem WORK PORT0 ... PORT7

Each server reads a request "GET /SEED ...", answers 40,000 bytes made
from SEED (test_tlsfb.c's lcg), sends close_notify and closes.

    tlsfb_run.py WORK OVL CACERT.PEM QEMU... TEST [-v]
"""
import os
import socket
import ssl
import subprocess
import sys
import threading

BODY = 40000

# (certificate, ciphers, ECDHE curve or None for OpenSSL's, X25519 first)
SERVERS = [
    ("rsa", "ECDHE-RSA-CHACHA20-POLY1305", None),
    ("rsa", "ECDHE-RSA-AES128-GCM-SHA256", "prime256v1"),
    ("rsa", "ECDHE-RSA-AES256-GCM-SHA384", "secp384r1"),
    ("rsa", "ECDHE-RSA-AES128-SHA256", None),
    ("rsa", "AES128-GCM-SHA256", None),
    ("ec256", "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES128-GCM-SHA256", None),
    ("ec384", "ECDHE-ECDSA-AES128-GCM-SHA256", "secp384r1"),
    ("rogue", "ECDHE-RSA-CHACHA20-POLY1305:ECDHE-RSA-AES128-GCM-SHA256", None),
]


def openssl(*args):
    subprocess.run(["openssl", *args], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def make_certs(work):
    def p(name):
        return os.path.join(work, name)

    def ca(name, keyargs, cn):
        openssl("req", "-x509", "-new", "-nodes", *keyargs, "-keyout", p(name + ".key"), "-out", p(name + ".crt"),
                "-days", "30", "-subj", "/CN=" + cn, "-addext", "basicConstraints=critical,CA:TRUE",
                "-addext", "keyUsage=critical,keyCertSign,cRLSign")

    def leaf(name, keyargs, signer, usage):
        ext = p(name + ".ext")
        with open(ext, "w") as f:
            f.write("basicConstraints=CA:FALSE\nkeyUsage=critical,%s\nextendedKeyUsage=serverAuth\n"
                    "subjectAltName=DNS:localhost\n" % usage)
        openssl("req", "-new", "-nodes", *keyargs, "-keyout", p(name + ".key"), "-out", p(name + ".csr"),
                "-subj", "/CN=localhost")
        openssl("x509", "-req", "-in", p(name + ".csr"), "-CA", p(signer + ".crt"), "-CAkey", p(signer + ".key"),
                "-CAcreateserial", "-days", "30", "-sha256", "-extfile", ext, "-out", p(name + ".crt"))

    rsa = ["-newkey", "rsa:2048"]
    ca("ca-rsa", rsa, "AERA test RSA authority")
    ca("ca-ec", ["-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256"], "AERA test ECDSA authority")
    ca("ca-rogue", rsa, "An authority nobody loaded")
    leaf("rsa", rsa, "ca-rsa", "digitalSignature,keyEncipherment")
    leaf("ec256", ["-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256"], "ca-ec", "digitalSignature")
    leaf("ec384", ["-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-384"], "ca-ec", "digitalSignature")
    leaf("rogue", rsa, "ca-rogue", "digitalSignature,keyEncipherment")


def body(seed):
    x, out = seed, bytearray(BODY)
    for i in range(BODY):
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF
        out[i] = x >> 24
    return bytes(out)


def serve(ctx, lsock):
    while True:
        try:
            raw, _ = lsock.accept()
        except OSError:
            return
        raw.settimeout(120)
        try:
            with ctx.wrap_socket(raw, server_side=True) as s:
                req = b""
                while b"\r\n\r\n" not in req:
                    d = s.recv(4096)
                    if not d:
                        break
                    req += d
                seed = int(req.split(b" ")[1][1:])
                s.sendall(b"HTTP/1.0 200 OK\r\nContent-Length: %d\r\n\r\n" % BODY + body(seed))
                try:
                    s.unwrap()
                except (ssl.SSLError, OSError):
                    pass
        except (ssl.SSLError, OSError, ValueError, IndexError):
            pass
        finally:
            raw.close()


def main():
    if len(sys.argv) < 6:
        sys.exit(__doc__)
    work, ovl, cacert = sys.argv[1:4]
    rest = sys.argv[4:]
    verbose = rest and rest[-1] == "-v"
    cmd = rest[:-1] if verbose else rest
    os.makedirs(work, exist_ok=True)
    for f in ("seed.dat", "sigs.dat", "roots.dat"):
        if os.path.exists(os.path.join(work, f)):
            os.remove(os.path.join(work, f))
    make_certs(work)
    ca = os.path.join(work, "ca.pem")
    with open(ca, "w") as out:
        for f in (cacert, os.path.join(work, "ca-rsa.crt"), os.path.join(work, "ca-ec.crt")):
            with open(f) as i:
                out.write(i.read())
    ports = []
    for cert, ciphers, curve in SERVERS:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.minimum_version = ctx.maximum_version = ssl.TLSVersion.TLSv1_2
        ctx.set_ciphers(ciphers)
        if curve:
            ctx.set_ecdh_curve(curve)
        ctx.load_cert_chain(os.path.join(work, cert + ".crt"), os.path.join(work, cert + ".key"))
        ls = socket.socket()
        ls.bind(("127.0.0.1", 0))
        ls.listen(4)
        ports.append(ls.getsockname()[1])
        threading.Thread(target=serve, args=(ctx, ls), daemon=True).start()
    args = cmd + [ovl, ca, work] + [str(p) for p in ports] + (["-v"] if verbose else [])
    try:
        r = subprocess.run(args, timeout=900)
    except subprocess.TimeoutExpired:
        print("FAIL  test_tlsfb took more than 15 minutes")
        sys.exit(1)
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
