#!/bin/bash
# Install the Claude ST bridge on a Raspberry Pi (or any Debian-style Linux)
# as an always-on systemd service called claude-st.
#
#   sudo ./install.sh                     install or update, asks what it needs
#   sudo ./install.sh --network --atari 192.168.1.20
#                                         wireless: Claude ST connects over STinG
#                                         (several Ataris: --atari IP1,IP2;
#                                          any address: --atari any)
#   sudo ./install.sh --backend api       use the Anthropic API instead of claude.ai
#   sudo ./install.sh --port /dev/ttyUSB0 --baud 9600   serial cable instead
#   sudo ./install.sh --set-key           paste a new claude.ai session key
#   sudo ./install.sh --uninstall
#
# It only adds its own user, /opt/claude-st, /etc/claude-st and one service;
# nothing else on the Pi is changed.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
PREFIX=/opt/claude-st
CONF_DIR=/etc/claude-st
CONF=$CONF_DIR/claude-st.env
SVC=/etc/systemd/system/claude-st.service

backend="" port="" baud="" mode="" atari="" tcp_port="" set_key=0 uninstall=0
while [ $# -gt 0 ]; do
  case "$1" in
    --backend) backend=$2; shift 2 ;;
    --port) port=$2; mode=serial; shift 2 ;;
    --baud) baud=$2; mode=serial; shift 2 ;;
    --network) mode=tcp; shift ;;
    --atari) atari=$2; mode=tcp; shift 2 ;;
    --listen-port) tcp_port=$2; mode=tcp; shift 2 ;;
    --set-key) set_key=1; shift ;;
    --uninstall) uninstall=1; shift ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

[ "$(id -u)" -eq 0 ] || { echo "Run me with sudo." >&2; exit 1; }

say() { printf '\n== %s\n' "$*"; }

# NAME VALUE -> CONF, replacing any earlier NAME= line
set_var() {
  local tmp
  tmp=$(mktemp)
  grep -v "^$1=" "$CONF" > "$tmp" || true
  printf '%s=%s\n' "$1" "$2" >> "$tmp"
  install -m 640 -o root -g claude-st "$tmp" "$CONF"
  rm -f "$tmp"
}

get_var() { sed -n "s/^$1=//p" "$CONF" 2>/dev/null | tail -1; }

ask_secret() {
  local prompt=$1 value
  if [ ! -t 0 ]; then echo ""; return; fi
  read -rsp "$prompt" value; echo >&2
  echo "$value"
}

if [ $uninstall -eq 1 ]; then
  say "Removing Claude ST"
  systemctl disable --now claude-st 2>/dev/null || true
  rm -f "$SVC"
  systemctl daemon-reload
  rm -rf "$PREFIX"
  echo "Kept $CONF_DIR and /var/lib/claude-st (your settings and API-mode history)."
  echo "Delete them by hand if you don't need them; the claude-st user is left too."
  exit 0
fi

if [ $set_key -eq 1 ]; then
  [ -f "$CONF" ] || { echo "Not installed yet; run without --set-key first." >&2; exit 1; }
  key=$(ask_secret "Paste the claude.ai sessionKey (input hidden): ")
  [ -n "$key" ] || { echo "No key given, nothing changed."; exit 1; }
  set_var CLAUDE_SESSION_KEY "$key"
  systemctl restart claude-st
  echo "Key updated and claude-st restarted."
  exit 0
fi

say "Installing packages"
apt-get update -qq
apt-get install -y -qq python3 python3-venv > /dev/null

say "Creating the claude-st service user"
if ! id claude-st >/dev/null 2>&1; then
  useradd --system --home-dir /var/lib/claude-st --shell /usr/sbin/nologin claude-st
fi
usermod -aG dialout claude-st

say "Copying the bridge to $PREFIX"
mkdir -p "$PREFIX/bridge"
cp "$HERE"/../bridge/*.py "$HERE"/../bridge/requirements.txt "$PREFIX/bridge/"
rm -f "$PREFIX/bridge/test_bridge.py"

say "Setting up Python packages (this can take a few minutes on a Pi)"
[ -x "$PREFIX/venv/bin/python" ] || python3 -m venv "$PREFIX/venv"
"$PREFIX/venv/bin/pip" install -q --upgrade pip
"$PREFIX/venv/bin/pip" install -q pyserial requests anthropic
if ! "$PREFIX/venv/bin/pip" install -q curl_cffi; then
  echo "Note: curl_cffi isn't available for this Pi; using plain requests instead."
fi

say "Settings in $CONF"
mkdir -p "$CONF_DIR"
if [ ! -f "$CONF" ]; then
  install -m 640 -o root -g claude-st /dev/null "$CONF"
  cat > "$CONF" <<'CONF_EOF'
# Claude ST gateway settings. After editing: sudo systemctl restart claude-st
# Bridge options, see: claude_bridge.py --help
CLAUDE_ST_ARGS=--backend claudeai --serial auto --baud 19200
# claude.ai "sessionKey" cookie (for --backend claudeai). Change it with:
#   sudo ./install.sh --set-key
CLAUDE_SESSION_KEY=
# Anthropic API key (for --backend api)
#ANTHROPIC_API_KEY=
CONF_EOF
fi

args=$(get_var CLAUDE_ST_ARGS)
cur_backend=$(sed -n 's/.*--backend \([a-z]*\).*/\1/p' <<<"$args")
cur_port=$(sed -n 's/.*--serial \([^ ]*\).*/\1/p' <<<"$args")
cur_baud=$(sed -n 's/.*--baud \([0-9]*\).*/\1/p' <<<"$args")
cur_tcp=$(sed -n 's/.*--tcp [^ ]*:\([0-9]*\).*/\1/p' <<<"$args")
cur_atari=$(grep -o -- '--allow [^ ]*' <<<"$args" | awk '{print $2}' | paste -sd, -)
[ -z "$cur_atari" ] && [ -n "$cur_tcp" ] && cur_atari=any
cur_mode=serial; [ -n "$cur_tcp" ] && cur_mode=tcp
backend=${backend:-${cur_backend:-claudeai}}
mode=${mode:-$cur_mode}
port=${port:-${cur_port:-auto}}
baud=${baud:-${cur_baud:-19200}}
tcp_port=${tcp_port:-${cur_tcp:-2323}}
atari=${atari:-$cur_atari}
case "$backend" in claudeai|api|demo) ;; *) echo "backend must be claudeai, api or demo" >&2; exit 2 ;; esac
if [ "$mode" = tcp ]; then
  if [ -z "$atari" ] && [ -t 0 ]; then
    read -rp "The Atari's IP address (several: IP1,IP2; or 'any'): " atari
  fi
  if [ -z "$atari" ]; then
    echo "Network mode needs the Atari's IP address, e.g. --atari 192.168.1.20 (or --atari any)" >&2
    exit 2
  fi
  allow=""
  if [ "$atari" != any ]; then
    for ip in ${atari//,/ }; do
      [[ "$ip" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "--atari wants IP addresses or 'any', not $ip" >&2; exit 2; }
      allow="$allow --allow $ip"
    done
  fi
  set_var CLAUDE_ST_ARGS "--backend $backend --tcp 0.0.0.0:$tcp_port$allow"
  if command -v ufw >/dev/null && ufw status 2>/dev/null | grep -q "Status: active"; then
    if [ "$atari" = any ]; then
      ufw allow "$tcp_port"/tcp >/dev/null
    else
      for ip in ${atari//,/ }; do ufw allow from "$ip" to any port "$tcp_port" proto tcp >/dev/null; done
    fi
    echo "Firewall (ufw): allowed $atari to port $tcp_port"
  fi
else
  set_var CLAUDE_ST_ARGS "--backend $backend --serial $port --baud $baud"
fi

if [ "$backend" = claudeai ] && [ -z "$(get_var CLAUDE_SESSION_KEY)" ]; then
  echo "claude.ai needs your browser's sessionKey cookie (see README.md)."
  key=$(ask_secret "Paste it now, or press Return to add it later with --set-key: ")
  [ -n "$key" ] && set_var CLAUDE_SESSION_KEY "$key"
fi
if [ "$backend" = api ] && [ -z "$(get_var ANTHROPIC_API_KEY)" ]; then
  key=$(ask_secret "Anthropic API key (input hidden): ")
  [ -n "$key" ] && set_var ANTHROPIC_API_KEY "$key"
fi

say "Installing the service"
install -m 644 "$HERE/claude-st.service" "$SVC"
systemctl daemon-reload
systemctl enable claude-st >/dev/null
systemctl restart claude-st

if [ "$mode" = serial ]; then
  say "Serial ports on this Pi"
  ls -l /dev/serial/by-id/ 2>/dev/null || echo "(no USB serial adapters plugged in)"
  [ -e /dev/serial0 ] && echo "Pi UART: /dev/serial0 (needs an RS-232 level shifter, see README)"
fi

say "Done"
if [ "$mode" = tcp ]; then
  ip=$(hostname -I 2>/dev/null | awk '{print $1}')
  echo "Backend: $backend   wireless: listening on port $tcp_port for the Atari at $atari"
  echo "On the Atari, CLAUDE.INF should say:  tcp ${ip:-THIS-PI-IP} $tcp_port"
else
  echo "Backend: $backend   serial port: $port   baud: $baud"
fi
echo "Status:  systemctl status claude-st"
echo "Log:     journalctl -u claude-st -f"
