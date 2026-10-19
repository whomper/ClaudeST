#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The test pictures, at fixed commits, into build/images (never committed):
# libpng's PngSuite (contrib/pngsuite: every colour type and depth,
# interlaced or not, with transparency), libjpeg-turbo's test images and
# libnsgif's test GIFs. run.py makes more from them with Pillow.
set -e
cd "$(dirname "$0")"
B=build/images
mkdir -p build "$B"
get() {	# repo commit folder
	d=build/src-$(basename "$1")
	if [ ! -d "$d" ]; then
		git clone -q --filter=blob:none --no-checkout "$1" "$d"
		git -C "$d" sparse-checkout set "$3" >/dev/null
	fi
	git -C "$d" checkout -q "$2"
	cp "$d/$3"/* "$B"/ 2>/dev/null || true
}
get https://github.com/pnggroup/libpng 18ea3e9404a09f42d92f952a4c0d1dd3d688182a contrib/pngsuite
get https://github.com/libjpeg-turbo/libjpeg-turbo 2fa570c514a4b0e70342ef8b32ce1cda21b85710 testimages
get https://github.com/netsurf-browser/libnsgif 22e99eb6818b1284d0f3ff1b7f46159e87221220 test/data
rm -f "$B"/*.icc "$B"/*.txt "$B"/README "$B"/*.scan
ls "$B" | wc -l
