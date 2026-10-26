#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The pictures (src/pic.c) against Pillow, on the pictures fetch.sh gets
and more made from them here.

    run.py TEST_PIC                 every check (make test)
    run.py TEST_PIC_DSP             the same, and the DSP's dither against
                                    the 68030's (make testdsp)
    run.py --same TEST_PIC OTHER    OTHER (the 68030 build under QEMU) must
                                    give the very same bytes (make test68k)

Each picture is decoded at its own size and compared with Pillow's: PNG,
GIF and lossless WebP must be the same in every pixel (to the Falcon's
5-6-5 colours), JPEG and lossy WebP close (TJpgDec's IDCT and its chroma,
nearest rather than smoothed, differ a little from libjpeg's; a lossy
WebP's chroma too, from Pillow's libwebp's). WebP only in EWeb's builds
(libjpeg-turbo's and the DSP's: -DPIC_LIBWEBP); the others refuse it. Then
shrunk to a third and enlarged by half, compared with Pillow's box filter
and nearest neighbour, and dithered to 16 colours (the mean colour kept).
Last, every file cut short and with bytes changed, under AddressSanitizer
and UBSan. Needs Pillow and numpy."""
import glob
import os
import shlex
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, "build", "images")
GEN = os.path.join(HERE, "build", "gen")
OUT = os.path.join(HERE, "build", "out")
# arithmetic coding: neither TJpgDec nor jpegdc reads it (nor do browsers
# meet it: libjpeg left it out for years over its patents); libjpeg-turbo
# does, and is compared as the rest
UNSUPPORTED = {"testimgari.jpg", "animated.webp"}


def has_webp(cmd):
    """EWeb's builds (libjpeg-turbo's, the DSP's) have libwebp"""
    return any(k in os.path.basename(shlex.split(cmd)[0]) for k in ("_lj", "_dsp"))


def make_more():
    """progressive and other JPEGs, big ones for the scaled paths, PNG with
    alpha, GIFs with transparency, interlaced and animated"""
    os.makedirs(GEN, exist_ok=True)
    src = Image.open(os.path.join(IMG, "testorig.jpg")).convert("RGB")
    big = src.resize((src.width * 7, src.height * 7), Image.BICUBIC)
    for name, im, kw in [
        ("prog.jpg", src, dict(progressive=True, quality=85)),
        ("s444.jpg", src, dict(subsampling=0, quality=90)),
        ("s422.jpg", src, dict(subsampling=1, quality=90)),
        ("s420.jpg", src, dict(subsampling=2, quality=90)),
        ("grey.jpg", src.convert("L"), dict(quality=90)),
        ("odd.jpg", src.crop((0, 0, 221, 143)), dict(quality=90)),
        ("big.jpg", big, dict(quality=85)),
        ("bigprog.jpg", big, dict(quality=85, progressive=True)),
    ]:
        im.save(os.path.join(GEN, name), "JPEG", **kw)
    rgba = src.convert("RGBA")
    a = Image.linear_gradient("L").resize(src.size)
    rgba.putalpha(a)
    rgba.save(os.path.join(GEN, "alpha.png"))
    big.save(os.path.join(GEN, "big.png"))
    p = src.convert("P", palette=Image.ADAPTIVE, colors=64)
    p.save(os.path.join(GEN, "pal.gif"))
    p.save(os.path.join(GEN, "pal_int.gif"), interlace=True)
    p.save(os.path.join(GEN, "trans.gif"), transparency=0)
    frames = [p, p.rotate(90), p.rotate(180)]
    frames[0].save(os.path.join(GEN, "anim.gif"), save_all=True, append_images=frames[1:], duration=100, loop=0)
    # WebP: lossy and lossless, with alpha or not, an odd size, a big one
    # (shrunk), and an animation (not taken)
    for name, im, kw in [
        ("lossy.webp", src, dict(quality=80)),
        ("lossy_alpha.webp", rgba, dict(quality=80)),
        ("lossy_odd.webp", src.crop((0, 0, 221, 143)), dict(quality=80)),
        ("lossy_big.webp", big, dict(quality=75)),
        ("lossless.webp", src, dict(lossless=True)),
        ("lossless_alpha.webp", rgba, dict(lossless=True)),
        ("lossless_pal.webp", p.convert("RGB"), dict(lossless=True)),
    ]:
        im.save(os.path.join(GEN, name), "WEBP", **kw)
    frames = [src, src.rotate(90), src.rotate(180)]
    frames[0].save(os.path.join(GEN, "animated.webp"), save_all=True, append_images=frames[1:], duration=100, loop=0,
                   quality=80)


def reference(path):
    """R G B A, as Pillow reads it (16-bit samples to their top 8 bits)"""
    im = Image.open(path)
    im.seek(0)
    if im.mode in ("I;16", "I;16B", "I", "I;16L"):
        v = np.array(im, dtype=np.uint32)
        key = im.info.get("transparency")
        g = (v >> 8).astype(np.uint8)
        a = np.where(v == key, 0, 255).astype(np.uint8) if isinstance(key, int) else np.full(g.shape, 255, np.uint8)
        return np.dstack([g, g, g, a])
    key = im.info.get("transparency")
    if im.mode == "L" and isinstance(key, int) and path.endswith(".png"):
        # a grey key below 8 bits: Pillow compares it with the scaled
        # grey, so makes nothing clear; the key is the sample's value
        with open(path, "rb") as f:
            depth = f.read(25)[24]
        g = np.array(im, dtype=np.uint8)
        if depth < 8:
            key = key * 255 // ((1 << depth) - 1)
        return np.dstack([g, g, g, np.where(g == key, 0, 255).astype(np.uint8)])
    return np.array(im.convert("RGBA"), dtype=np.uint8)


def to565(a):
    """as the Falcon's 16-bit colour shows it, back to 8 bits"""
    r = a[..., 0] & 0xF8
    g = a[..., 1] & 0xFC
    b = a[..., 2] & 0xF8
    return np.dstack([r | (r >> 5), g | (g >> 6), b | (b >> 5), a[..., 3]]).astype(np.uint8)


def decode(cmd, path, w, h, dither=False):
    """dither: True (16 colours), "-m" (black and white) or False"""
    flag = "-d" if dither is True else dither or None
    out = os.path.join(OUT, os.path.basename(path) + ".%dx%d%s.raw" % (w, h, flag or ""))
    args = shlex.split(cmd) + ([flag] if flag else []) + [path, str(w), str(h), out]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        return None, (r.stdout + r.stderr).strip()
    with open(out, "rb") as f:
        head = f.readline().split()
        pw, ph, how, scale = (int(x) for x in head)
        px = np.frombuffer(f.read(), dtype=np.uint8).reshape(ph, pw, 4)
    return (px, how, scale), out


def compare(ours, ref, exact):
    """(ok, what): clear pixels' colours don't count"""
    if ours.shape != ref.shape:
        return False, "size %s, Pillow %s" % (ours.shape[:2], ref.shape[:2])
    solid = ref[..., 3] >= 128
    da = np.abs(ours[..., 3].astype(int) - ref[..., 3].astype(int))
    d = np.abs(ours[..., :3].astype(int) - ref[..., :3].astype(int))[solid]
    if exact:
        bad = int((d.max(axis=1) > 0).sum()) if d.size else 0
        bada = int((da > 0).sum())
        return bad == 0 and bada == 0, "%d colours, %d alphas differ" % (bad, bada)
    mean = float(d.mean()) if d.size else 0.0
    p99 = float(np.percentile(d, 99)) if d.size else 0.0
    return mean < 3.0 and p99 < 40 and da.max() <= 1, "mean %.2f, 99%% %.0f, alpha %d" % (mean, p99, da.max())


def centre(i, n, m):
    """pic.c's centre(): pixel i of n to m, by its centre"""
    return ((2 * i + 1) * m) // (2 * n)


def model(ref, w, h):
    """pic.c's scaler, in numpy: each source pixel to the pixel shown its
    centre falls in (shrinking), or each pixel shown from the source pixel
    its centre falls in (enlarging); colours weighted by alpha"""
    sh, sw = ref.shape[:2]
    xs = np.array([centre(i, w, sw) for i in range(w)]) if w >= sw else None
    ys = np.array([centre(i, h, sh) for i in range(h)]) if h >= sh else None
    a = ref[..., 3].astype(np.int64)
    c = ref[..., :3].astype(np.int64)
    wc = np.where(a[..., None] == 255, c, (c * (a[..., None] + 1)) >> 8)
    wc = np.where(a[..., None] == 0, 0, wc)
    sums = np.zeros((h, w, 5), np.int64)
    dx = np.array([centre(i, sw, w) for i in range(sw)]) if w < sw else None
    dy = np.array([centre(i, sh, h) for i in range(sh)]) if h < sh else None
    for y in range(h):
        rows = np.nonzero(dy == y)[0] if dy is not None else [ys[y]]
        for x in range(w):
            cols = np.nonzero(dx == x)[0] if dx is not None else [xs[x]]
            blk = np.ix_(rows, cols)
            sums[y, x, :3] = wc[blk].reshape(-1, 3).sum(axis=0)
            sums[y, x, 3] = a[blk].sum()
            sums[y, x, 4] = len(rows) * len(cols)
    out = np.zeros((h, w, 4), np.int64)
    n, sa = sums[..., 4], sums[..., 3]
    opaque = sa == 255 * n
    # the rows shown while every row before was opaque (the solid rows'
    # sums): the mean rounded, as (sum * recip[n] + 32768) >> 16
    row_opaque = np.minimum.accumulate((a == 255).all(axis=1))
    last = [int(np.nonzero(dy == y)[0].max()) if dy is not None and (dy == y).any() else
            (int(ys[y]) if ys is not None else 0) for y in range(h)]
    rounded = np.array([bool(row_opaque[r]) for r in last])[:, None] & np.ones((1, w), bool)
    nn = np.maximum(n, 1)
    recip = (65536 + nn // 2) // nn
    mean_r = np.where(nn[..., None] == 1, sums[..., :3],
                      np.where(nn[..., None] < 258, (sums[..., :3] * recip[..., None] + 32768) >> 16,
                               sums[..., :3] // nn[..., None]))
    with np.errstate(divide="ignore", invalid="ignore"):
        mean = np.where(rounded[..., None], mean_r, sums[..., :3] // nn[..., None])
        solid = np.where(opaque[..., None], mean,
                         np.minimum(255, sums[..., :3] * 255 // np.maximum(sa, 1)[..., None]))
    out[..., :3] = np.where((sa == 0)[..., None], 0, solid)
    out[..., 3] = np.where(opaque, 255, np.where(sa == 0, 0, sa // np.maximum(n, 1)))
    return to565(out.astype(np.uint8))


def box(ref, w, h):
    """Pillow's box filter, the colours weighted by alpha"""
    im = Image.fromarray(ref, "RGBA").convert("RGBa").resize((w, h), Image.BOX).convert("RGBA")
    return np.array(im, dtype=np.uint8)


def nearest(ref, w, h):
    return np.array(Image.fromarray(ref, "RGBA").resize((w, h), Image.NEAREST), dtype=np.uint8)


def pictures():
    files = sorted(glob.glob(os.path.join(IMG, "*")) + glob.glob(os.path.join(GEN, "*")))
    return [f for f in files if f.lower().endswith((".png", ".jpg", ".gif", ".webp"))]


def main():
    os.makedirs(OUT, exist_ok=True)
    if not os.path.isdir(IMG):
        sys.exit("run fetch.sh first")
    if sys.argv[1] == "--same":
        return same(sys.argv[2], sys.argv[3])
    cmd = sys.argv[1]
    make_more()
    fails = 0
    checks = 0
    for path in pictures():
        name = os.path.basename(path)
        try:
            ref = reference(path)
        except Exception as e:  # Pillow refuses it: ours may too
            print("skip %s (Pillow: %s)" % (name, e))
            continue
        res, why = decode(cmd, path, 0, 0)
        checks += 1
        if name.endswith(".webp") and not has_webp(cmd):
            ok = res is None and "can't be shown" in why
            print("%s %-22s refused (no libwebp in this build), as it should be" % ("ok  " if ok else "FAIL", name))
            fails += not ok
            continue
        if name in UNSUPPORTED and res is None:
            ok = "can't be shown" in why
            print("%s %-22s refused, as it should be" % ("ok  " if ok else "FAIL", name))
            fails += not ok
            continue
        if res is None:
            fails += 1
            print("FAIL %s: not decoded: %s" % (name, why))
            continue
        ours, how, scale = res
        # (lossy: compared as a JPEG is)
        jpeg = name.endswith(".jpg") or (name.endswith(".webp") and "lossless" not in name)
        if how == 100:
            # the averages of 8x8 blocks, enlarged: compared at an eighth
            hb, wb = (ref.shape[0] + 7) // 8, (ref.shape[1] + 7) // 8
            res8, _ = decode(cmd, path, wb, hb)
            ok, what = compare(res8[0], box(ref, wb, hb), False) if res8 else (False, "not decoded")
            # chroma at half size (4:2:0) is averaged over 16x16
            ok = ok or float(np.abs(res8[0][..., :3].astype(int) - box(ref, wb, hb)[..., :3].astype(int)).mean()) < 12
            what = "block averages: " + what
        else:
            ok, what = compare(ours, to565(ref) if not jpeg else ref, not jpeg)
        print("%s %-22s %s" % ("ok  " if ok else "FAIL", name, what))
        fails += not ok
        h0, w0 = ref.shape[:2]
        # shrunk to a third, and enlarged by half
        for w, h, kind in ((max(1, w0 // 3), max(1, h0 // 3), "shrunk"), (w0 * 3 // 2, h0 * 3 // 2, "enlarged")):
            if how == 100:
                break
            res, why = decode(cmd, path, w, h)
            checks += 1
            if res is None:
                fails += 1
                print("FAIL %s %s: %s" % (name, kind, why))
                continue
            if jpeg:
                # TJpgDec's own smaller picture, then averaged: close to
                # Pillow's box filter on libjpeg's
                want = box(ref, w, h) if kind == "shrunk" else nearest(ref, w, h)
                ok, what = compare(res[0], want, False)
                ok = ok or float(np.abs(res[0][..., :3].astype(int) - want[..., :3].astype(int)).mean()) < 7
            else:
                # the same as the scaler's model, to a step of 5-6-5 (its
                # rounding), and close to Pillow's box filter
                want = model(ref, w, h)
                d = np.abs(res[0].astype(int) - want.astype(int))
                solid = want[..., 3] >= 128
                ok = int(d[..., :3][solid].max(initial=0)) <= 8 and int(d[..., 3].max()) <= 1
                what = "model: colour %d, alpha %d" % (d[..., :3][solid].max(initial=0), d[..., 3].max())
                if kind == "shrunk":
                    pb = box(ref, w, h)
                    mean = float(np.abs(res[0][..., :3].astype(int) - pb[..., :3].astype(int))[pb[..., 3] >= 128].mean())
                    ok = ok and mean < 12
                    what += ", Pillow's box filter %.1f" % mean
            print("%s %-22s %s %dx%d%s: %s" % ("ok  " if ok else "FAIL", name, kind, w, h,
                                              " (JPEG at 1/%d)" % (1 << res[2]) if jpeg else "", what))
            fails += not ok
        # dithered to 16 colours: the mean colour stays
        res, why = decode(cmd, path, 0, 0, dither=True)
        checks += 1
        if res is not None:
            d = res[0]
            solid = ref[..., 3] >= 128
            if solid.sum() > 64:
                m1 = d[..., :3][solid].astype(float).mean(axis=0)
                m2 = ref[..., :3][solid].astype(float).mean(axis=0)
                # 16 colours can't make every colour: light blue on
                # waves.gif comes out a little greyer
                ok = float(np.abs(m1 - m2).max()) < 32
                if not ok:
                    print("FAIL %-22s dithered: mean %s, Pillow %s" % (name, m1.round(), m2.round()))
                    fails += 1
        # to black and white: the mean brightness stays
        res, why = decode(cmd, path, 0, 0, dither="-m")
        checks += 1
        if res is None and not os.path.basename(path) in UNSUPPORTED:
            print("FAIL %-22s black and white: %s" % (name, why))
            fails += 1
        if res is not None:
            d = res[0]
            solid = ref[..., 3] >= 128
            if solid.sum() > 64:
                lum = lambda a: (a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114)  # noqa: E731
                m1 = float(lum(d[..., :3].astype(float))[solid].mean())
                m2 = float(lum(ref[..., :3].astype(float))[solid].mean())
                ok = abs(m1 - m2) < 16 and set(np.unique(d[..., :3][solid])) <= {0, 255}
                if not ok:
                    print("FAIL %-22s black and white: mean %.0f, Pillow %.0f" % (name, m1, m2))
                    fails += 1
    if "dsp" in os.path.basename(shlex.split(cmd)[0]):
        # dithered on the DSP (dsp/dither.a56) and on the 68030: the same
        # pens, to 4, 16 and 256 colours
        r = subprocess.run(shlex.split(cmd) + ["-X"] + pictures(), capture_output=True, text=True)
        checks += 1
        print(r.stdout.strip()[-3000:], r.stderr.strip()[-2000:])
        if r.returncode != 0:
            fails += 1
            print("FAIL dithered on the DSP")
    # in slices: the same with turns taken, and stopped by one
    r = subprocess.run(shlex.split(cmd) + ["-s"] + pictures(), capture_output=True, text=True)
    checks += 1
    print(r.stdout.strip()[-3000:], r.stderr.strip()[-2000:])
    if r.returncode != 0:
        fails += 1
        print("FAIL in slices")
    r = subprocess.run(shlex.split(cmd) + ["-r"] + pictures(), capture_output=True, text=True)
    checks += 1
    print(r.stdout.strip(), r.stderr.strip()[-2000:])
    if r.returncode != 0:
        fails += 1
        print("FAIL robustness")
    print("%d checks, %d failed" % (checks, fails))
    sys.exit(1 if fails else 0)


def same(host, other):
    """the 68030 build gives the same bytes as the build machine's"""
    make_more()
    fails = 0
    files = pictures()
    for path in files:
        for w, h, d in ((0, 0, False), (37, 23, False), (0, 0, True), (0, 0, "-m")):
            a, _ = decode(host, path, w, h, d)
            os.replace(_, _ + ".host") if a else None
            b, _ = decode(other, path, w, h, d)
            if a is None and b is None:
                continue
            if a is None or b is None or not np.array_equal(a[0], b[0]):
                fails += 1
                print("FAIL %s %dx%d%s: not the same" % (os.path.basename(path), w, h, " dithered %s" % d if d else ""))
    print("%d pictures, %d differ" % (len(files), fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
