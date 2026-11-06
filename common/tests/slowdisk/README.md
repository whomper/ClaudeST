# slowdisk: the rig's disk at the speed of Erez's cards

Hatari's GEMDOS drive (`--harddrive`) answers every file call in no
emulated time, so a speed-rig stretch never contains a disk wait. The
tables here are what `common/tests/calib` (CALIB.PRG, release rigtools-1)
measured on the real machines, microseconds a call:

- `falcon-ide16.txt`: Erez's Falcon (TOS 4.04, GEMDOS 0.30), stock 16 MHz, IDE, 5 Nov 2026.
- `falcon-ide50-dfb1.txt`: the same, with the DFB1 at 50 MHz, IDE, 5 Nov 2026.
- `st.txt`: his ST, when it arrives.

The shim (an AUTO-folder program hooking trap #1) charges these times in
emulated time. Read the table as: sequential reads are cheap, everything
that changes the directory or the FAT is not (create, rename, delete about
0.25 s, make folder 0.35 s, the close after a write 0.3 s on the stock
Falcon; a seek and read about 9 ms). `Falcon-4` takes the stock table,
`Falcon` the DFB1's.
