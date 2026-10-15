# The AERA gateways for the Raspberry Pi

Servers on the internet want encrypted connections today (TLS, SSH), and
an Atari can't make them at a usable speed, or at all. So the AERA
programs talk plainly to a Raspberry Pi on your home network, and the Pi
makes the secure connection onwards:

```
Atari (EMail)    --plain IMAP, SMTP, POP3--> Pi  emailgate --TLS-->   imap.gmail.com, smtp.gmail.com
Atari (ECal,     --plain HTTP, proxy form--> Pi  ecalgate  --HTTPS--> caldav.icloud.com, any .ics feed
       EContact)
Atari (EFTP)     --plain FTP-->              Pi  eftpgate  --FTPS-->  ftp.example.org
                                                           --SFTP-->  my.server.net
Atari (ClaudeST) --STinG or a serial cable-> Pi  claude-st --HTTPS--> claude.ai
```

One program, `aera-gateway`, sets them all up, brings them up to date and
says how they are. Any computer with Debian, Ubuntu or Raspberry Pi OS
will do, and one Pi can run all of them.

| Service  | For                 | Program on the Pi          | Port(s) on the Pi                        |
|----------|---------------------|----------------------------|------------------------------------------|
| `mail`   | EMail               | `emailgate.py`             | 143 IMAP, 587 SMTP, 110 POP3             |
| `dav`    | ECal and EContact   | `ecalgate.py`              | 8180                                     |
| `ftp`    | EFTP                | `eftpgate.py`              | 2121, and 30000-30099 for data           |
| `claude` | ClaudeST            | ClaudeST's bridge          | 2323 (or a serial cable)                 |

This folder is AERA-Gateway's
([whomper/AERA-Gateway](https://github.com/whomper/AERA-Gateway)). EMail,
ECal, EContact, EFTP and ClaudeST carry a copy of it, as they carry
`common/` from AERA-Atari: change it in AERA-Gateway, never in a copy.

## Setting it up

On the Pi:

```
git clone https://github.com/whomper/AERA-Gateway
cd AERA-Gateway
sudo gateway/aera-gateway install mail imap.gmail.com smtp.gmail.com pop.gmail.com
sudo gateway/aera-gateway install dav
sudo gateway/aera-gateway install ftp
sudo gateway/aera-gateway install claude --network --atari 192.168.1.20
```

(only the ones you need). The same commands work in the `gateway/` folder
of an app's own copy. Each gateway runs as a service of its own
(`emailgate`, `ecalgate`, `eftpgate`, `claude-st`) under a user of its
own with no access to the rest of the Pi, starts now and at every boot,
and `install` prints what to type into the Atari program.

Afterwards:

```
cd AERA-Gateway && git pull
sudo gateway/aera-gateway update          every gateway on the Pi, its settings kept
gateway/aera-gateway status               what runs, on which ports, with which settings
sudo gateway/aera-gateway install dav --allow-local     change a setting; the others stay
sudo gateway/aera-gateway remove ftp
```

`sudo gateway/aera-gateway install SERVICE --help` lists a service's
settings. A new gateway is checked before it replaces the old one: if it
doesn't start and answer on its ports within ten seconds, the one before
comes back, and `aera-gateway` says so.

## mail: EMail's gateway

Name your provider's servers: the IMAP server, the SMTP server and, if
you want POP3, the POP3 server.

```
sudo gateway/aera-gateway install mail imap.gmail.com smtp.gmail.com pop.gmail.com
```

| Provider    | IMAP                  | SMTP                  | POP3                 |
|-------------|-----------------------|-----------------------|----------------------|
| Gmail       | imap.gmail.com        | smtp.gmail.com        | pop.gmail.com        |
| iCloud      | imap.mail.me.com      | smtp.mail.me.com:587 \* |                    |
| Fastmail    | imap.fastmail.com     | smtp.fastmail.com     | pop.fastmail.com     |
| Yahoo       | imap.mail.yahoo.com   | smtp.mail.yahoo.com   | pop.mail.yahoo.com   |
| GMX         | imap.gmx.net          | mail.gmx.net          | pop.gmx.net          |

\* iCloud takes mail only on port 587 with STARTTLS: with `:587` the
gateway says EHLO and STARTTLS itself, and hands EMail the server's
greeting as if nothing had happened. The others use the TLS ports (993,
995, 465).

Gmail, iCloud and Yahoo need an **app password** for programs like EMail:
create one in your account's security settings and use it as EMail's
password. Outlook.com and Microsoft 365 accept only OAuth sign-in, which
EMail can't do.

In EMail, Options > Accounts: the Pi's IP address as the server (plain
TOS with STinG can't always look up names on the home network), port 143
for IMAP (110 for POP3), and the Pi again as the outgoing server on port
587, with your usual mail login. When the gateway can't help (it can't
reach the provider, or the provider's certificate doesn't check out),
EMail's alert says so in a line that starts with "emailgate:".

When the usual ports are taken on the Pi, give others and type them into
EMail instead: `--ports 2143,2587,2110` (IMAP, SMTP, POP3).

**Coming from EMail 1.x?** Its gateway was an stunnel set-up.
`sudo gateway/aera-gateway install mail` (no servers needed) takes its
servers and ports from `/etc/stunnel/atari-mail.conf`, so nothing changes
in EMail, and once emailgate answers it removes that file, stops stunnel
unless something else uses it, and drops the `ufw deny` rules 1.x's
`--allow` added. If emailgate doesn't start, the stunnel set-up comes
back. Name other servers or `--ports` and they win; `install` says if
EMail's ports must change.

## dav: ECal's and EContact's gateway

```
sudo gateway/aera-gateway install dav
```

Calendar and contact servers live on many hosts (iCloud moves you from
caldav.icloud.com to a server of its own, and every subscription is
somewhere else), so ecalgate can reach any of them. The Atari sends each
request in proxy form, `GET https://host/path HTTP/1.1`, and ecalgate:

- checks the server's certificate against the Pi's root certificates;
- keeps one connection per server open, so a sync doesn't pay for a TLS
  handshake per request;
- asks for compressed answers and unpacks them, and turns chunked answers
  into one block with a length;
- trims calendar subscriptions: when the Atari says which months it keeps
  (`X-Calendar-Window: 20260701-20271101`), ecalgate passes on only the
  events in that window, every repeating event that still repeats, and the
  time zones, and leaves out HTML descriptions and attached files. A
  public holiday or sports feed can be several megabytes; trimmed, it fits
  an ST.

In ECal and EContact, ≡ menu > Settings > Connection: Pi gateway = the
Pi's IP address, port 8180. Then add your accounts with their real
`https://` addresses: ecalgate does the rest. Check it from any browser on
your network: `http://PI_IP:8180/` answers "ecalgate is running".

`--allow-local` lets it reach servers on your home network too (a
Radicale or Nextcloud at home); `--port N` gives it another port; `-v`
logs every request (the server, never the address's path: a private
feed's address is its password). A Pi that only goes out through a proxy:
ecalgate follows `HTTPS_PROXY`; put it in the service with
`sudo systemctl edit ecalgate` (an `Environment=` line), and
`aera-gateway` keeps it.

## ftp: EFTP's gateway

```
sudo gateway/aera-gateway install ftp
```

It installs `python3-paramiko` too (for SSH) if it isn't there. The Atari
names the server in its login, the way FTP proxies have always done it,

```
USER sftp:erez@my.server.net:22         also ftps:  and  ftps-implicit:
PASS (the server's own password)
```

and EFTP does that for you: a site's Protocol in File > Sites says which.
To the Atari, eftpgate is an ordinary FTP server that does everything
EFTP likes (exact sizes and UTC times in MLSD, REST to resume both ways,
MFMT to keep dates, UTF-8 names, passive mode). On the way it:

- checks FTPS servers' certificates against the Pi's root certificates,
  and resumes the TLS session on every data connection (vsftpd, FileZilla
  Server and others insist on that);
- remembers an SSH server's key the first time (in
  `/var/lib/eftpgate/known_hosts`) and refuses the server if the key
  changes later; if the server was really reinstalled, remove its line
  there. `update` and `remove` never touch that file;
- turns line ends for text files on SFTP, which has no text mode: CR LF
  on the Atari, LF on the server;
- keeps the server's connection awake while EFTP keeps the Pi's.

In EFTP, Options > Settings > **Secure**: Pi gateway = the Pi's IP
address, port 2121. Then give your sites their real server names and
choose FTPS or SFTP as their Protocol in File > Sites. Falcon mode
doesn't need the Pi for FTPS (EFTP.PRG on a Falcon or TT does TLS
itself); SFTP always goes through it.

`--allow-local` lets it reach servers at home (a NAS). A NAS whose FTPS
certificate comes from its own CA: copy the CA's certificate to the Pi
and give `--cafile /path/to/ca.pem`. `-v` logs every command (never the
passwords).

## claude: ClaudeST's bridge

The bridge is ClaudeST's own (it changes with `CLAUDEST.PRG`);
`aera-gateway` sets it up from the latest ClaudeST release, or from a
copy you name with `--from`:

```
sudo gateway/aera-gateway install claude --network --atari 192.168.1.20
sudo gateway/aera-gateway install claude --serial /dev/ttyUSB0 --baud 9600
sudo gateway/aera-gateway install claude --backend api
sudo gateway/aera-gateway set-key        paste a new claude.ai session key
```

Its settings stay in `/etc/claude-st/claude-st.env` and its chats in
`/var/lib/claude-st`; `remove claude` keeps both. ClaudeST's README says
what the settings mean. In a ClaudeST copy, `pi/install.sh` still works:
it hands its options to `aera-gateway install claude --from` that copy.

## Keeping it private

Between the Atari and the Pi nothing is encrypted, as on every network in
the 1990s: keep the gateways on your home network and never forward their
ports on your router.

- They answer only addresses on the private networks (192.168.x.x,
  10.x.x.x, 172.16-31.x.x), never anyone on the internet, even if a port
  were forwarded. If others share your network, `--allow ATARI_IP` limits
  a gateway to your Atari (several: `--allow IP1,IP2`; `--allow any` for
  the home network again).
- ecalgate and eftpgate connect onwards anywhere the Atari asks, except
  your own network: without `--allow-local` they refuse a server whose
  address is at home, and they connect to the address they checked, so a
  name can't change its answer in between and lead them home.
- They use TLS 1.2 or newer and check every server's certificate and
  name.
- Their journals (`sudo journalctl -u emailgate`) say who connected where
  and why a connection ended; never what went through (your passwords
  do).
- If the Pi's firewall (ufw) is on, `install` lets the Atari in on the
  gateway's ports (or the private networks, without `--allow`); if it
  can't add a rule, it says which and stops.

## Moving from the apps' own install.sh

Until 2026 each app set up its own gateway with an `install.sh` of its
own (in `EMail/gateway`, `ECal/gateway`, `EFTP/gateway` and
`ClaudeST/pi`). `sudo gateway/aera-gateway update` takes those over: the
same service names, users, ports and settings (`--allow`, `--allow-local`,
`--cafile`, `-v`, the mail servers and ports, any `Environment=` line),
with the old program removed only once the new one answers. The old
commands still work and hand on to `aera-gateway`.

## Tests

```
pip install paramiko pyftpdlib pyopenssl
python3 -m unittest discover -s gateway -p 'test_*.py'
```

- `test_aeragate.py`: the address rules, one look-up per connection, TLS,
  the relay.
- `test_emailgate.py`: a relay over TLS, STARTTLS, a certificate nobody
  vouches for, the wrong name, a provider that can't be reached.
- `test_ecalgate.py`: trimming, compression, chunked answers, redirects,
  the home-network rule, a log without paths.
- `test_eftpgate.py`: an SFTP and an FTPS server on this computer, and a
  plain FTP client (and EFTP's own FTP core, when built) talking to
  eftpgate as the Atari does.
- `test_installer.py`: `aera-gateway` on a stand-in Pi: units, settings,
  taking over the apps' set-ups, the way back when a new gateway fails.

AERA-Gateway's `tests/systemd.sh` runs `aera-gateway` for real on a
computer with systemd (CI does on every push): it sets the gateways up
with the apps' old installers, takes them over, installs and removes, and
checks the claude recipe against ClaudeST's own `pi/install.sh`.
