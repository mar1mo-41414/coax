# Re-takeover recovery: coax fails after another loader used the tuner

## Symptom

Within one power session of the CECH-ZD1J, if another loader
(torne_user / torne_drv) drove the tuner after coax did, a subsequent
coax run initializes the device normally but every tune fails with:

```
MB86H47A codec core 1 did not finish its job
```

A USB bus reset does not help. Unplugging (power cycling) the tuner
does help, but is not necessary — see the recovery below.

## Cause

coax's loader (`loadLocked` in
`internal/coaxd/usb/bridge/mb86h47/mb86h47.go`) keeps a *signature
note* in core1 data RAM at `0x17a3800`: two words,
`{0x10ad5160, FNV-1a hash of the program}`. If the note matches the
program it is about to load, the loader skips the load entirely,
trusting that the program is still resident in instruction RAM.

torne_drv's loader does not know about the note and never touches it.
It differential-loads its own core1 program, which is 495 words and
was generated from coax v0.1.0 — a generation that has no job
mechanism at all. Only the first 495 words of instruction RAM are
replaced; the rest still holds the tail of the current 963-word coax
program, and the note still claims the current program is resident.

When coax runs again, it sees the matching note, skips the load, and
starts the old program. The old program services the mailbox and
command channel with the same layout, so initialization passes, but
it has no job dispatch: the first job issued during tuning (TMCC
recall) never completes and the host times out.

core0 is immune: the core0 programs of both loaders are
word-identical (185 words, unchanged since v0.1.0), so its note at
`0x16a3800` is always truthful.

## Recovery

Write one zero word over the note's magic, then run coax normally:

```sh
memtool write32 0x17a3800 0
coax tune --system ISDBT --ch 27
```

The loader then sees a note mismatch, differential-loads the current
program, and rewrites the note correctly. Only the magic word needs
to be cleared; the hash word may stay. No power cycle and no replug
is required.

Verified on real hardware, twice (2026-11-24): after torne_user ran,
coax failed with the error above; after clearing the note, the same
`coax tune` succeeded and produced a clean TS (188-byte aligned, zero
sync mismatches), and instruction RAM matched the current program
963/963 words.

## memtool

`memtool.c` (in this directory) is a small raw chip-memory
read/write tool used for the investigation and the recovery above.
It talks to the chip through the memory-request path (vendor request
0xbc) over libusb, using the torne driver core
(`torne_mem_read32` / `torne_mem_write32` from torne_drv's
`torne_bridge.c`, with the same libusb transport as torne_user).

```
memtool read32 <addr>            print one word
memtool read <addr> <count>      dump count words, "ADDR: VALUE" per line
memtool write32 <addr> <value>   write one word, then read it back
```

Building it requires `torne_core.h` and the torne driver core from
[mihalinn/torne_drv](https://github.com/mihalinn/torne_drv), plus
libusb-1.0. It is a diagnostic tool, not part of the coax build.

## Scope notes

- The stale-note state lives in the chip's RAM and disappears when
  USB power is lost. A tuner that was driven by torne_user on one
  host and then replugged into another host does not need this
  recovery.
- A host that only ever runs coax (for example the nasne setup in
  `docs/nasne-debian/`) never triggers the problem.
- Whether the PS3 torne software writes an equivalent note is
  unconfirmed; if it does, the same failure and recovery would apply
  after PS3 use.
