#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The old way in, kept: `sudo ./install.sh ...` in the gateway/ folder of
# EMail, ECal, EContact or EFTP. The gateways live in AERA-Gateway now and
# aera-gateway sets them up; this finds which one the app around it means
# and hands it the options as they were (--allow, --port, --ports,
# --allow-local, --remove, the mail servers).
HERE=$(cd "$(dirname "$0")" && pwd)
case "$(sed -n '1s/^# //p' "$HERE/../README.md" 2>/dev/null)" in
  EMail) SERVICE=mail ;;
  ECal|EContact) SERVICE=dav ;;
  EFTP) SERVICE=ftp ;;
  *) echo "Which gateway? sudo $HERE/aera-gateway install mail|dav|ftp|claude ... (see $HERE/README.md)" >&2
     exit 2 ;;
esac
echo "(install.sh hands on to: aera-gateway install $SERVICE $*)"
exec "$HERE/aera-gateway" install "$SERVICE" "$@"
