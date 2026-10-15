#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Install the ClaudeST bridge on a Raspberry Pi (or any Debian-style Linux)
# as an always-on systemd service called claude-st.
#
#   sudo ./install.sh                     install or update, asks what it needs
#   sudo ./install.sh --network --atari 192.168.1.20
#                                         wireless: ClaudeST connects over STinG
#                                         (several Ataris: --atari IP1,IP2;
#                                          any address on the home
#                                          network: --atari any)
#   sudo ./install.sh --backend api       use the Anthropic API instead of claude.ai
#   sudo ./install.sh --port /dev/ttyUSB0 --baud 9600   serial cable instead
#   sudo ./install.sh --set-key           paste a new claude.ai session key
#   sudo ./install.sh --uninstall
#
# aera-gateway, the installer of all the AERA gateways (gateway/, a copy of
# AERA-Gateway's), does the work, with the bridge from ClaudeST's latest
# release (never from this checkout):
#   sudo gateway/aera-gateway install claude [the same options]
# The usual way is AERA-Gateway's own copy and its update-pi.sh (README).
# It only adds its own user, /opt/claude-st, /etc/claude-st and one service;
# nothing else on the Pi is changed.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
case " $* " in
  *" -h "*|*" --help "*) sed -n '4,16p' "$0"; exit 0 ;;
esac
[ "$(id -u)" -eq 0 ] || { echo "Run me with sudo." >&2; exit 1; }
command -v python3 >/dev/null || { apt-get update -qq && apt-get install -y -qq python3; }
exec python3 "$HERE/../gateway/aera-gateway" install claude "$@"
