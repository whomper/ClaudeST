# slowdisk: the rig's disk at a real card's speed

Hatari's GEMDOS drive (`--harddrive`) answers every file call in no
emulated time, so a speed-rig stretch never contains a disk wait. A table
here gives a card's time for each file call, microseconds a call, in the
form `common/tests/calib` (CALIB.PRG) prints (`name value` a line:
`create_us`, `rename_us`, `read16k_us`, `close_after_write16k_us`...).

No table measured on the owner's machines is kept here (decided 24 Nov
2026: nothing the owner provides is committed). A table is made from
scratch, in round figures of the kind such a card gives: sequential reads
are cheap, everything that changes the directory or the FAT is not (a
create, rename or delete about a quarter of a second, the close after a
write about 0.3 s on a stock Falcon's IDE, a seek and read about 9 ms).

The shim (an AUTO-folder program hooking trap #1) is to charge these times
in emulated time: `Falcon-4` a stock Falcon's table, `Falcon` an
accelerated one's.
