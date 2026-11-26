# Running coax on a Debian nasne (MIPS) — measured results

This directory collects the build procedure and the on-device
measurement results for running coax / coaxd with a CECH-ZD1J
(torne) on a Sony nasne running Debian, from the 2026-11-24 test
sessions.

## Test setup

- Host: Sony nasne, Debian 12 + kernel 6.12.112-nasne (mipsel,
  single core), root operation.
- Tuner: CECH-ZD1J on the nasne's USB port. Enumerates as sysfs
  device `1-2` (054c:04b2, manufacturer Sony, product CECH-ZD1 J);
  no kernel driver binds to it, so no unbind step is needed.
- The nasne's internal tuner is served separately by nasne-recpt1
  (HTTP, port 8301). coaxd uses a Unix socket by default and does not
  conflict with it.
- Test channel: ISDB-T ch27 (a local terrestrial broadcast, full TS
  rate about 17.7 Mbps).

## Cross build

Debian's mipsel port and the userland binaries already running on
the device are hard-float ABI, so `GOMIPS=hardfloat` is the primary
build (a `softfloat` build was also made and used for fault
isolation; it behaves identically).

```sh
CGO_ENABLED=0 GOOS=linux GOARCH=mipsle GOMIPS=hardfloat go build -o coax  ./cmd/coax
CGO_ENABLED=0 GOOS=linux GOARCH=mipsle GOMIPS=hardfloat go build -o coaxd ./cmd/coaxd
```

Place `coax` and `coaxd` in the same directory (e.g.
`/usr/local/bin`); the CLI finds and auto-starts `coaxd` next to
itself. No firmware files are needed for the CECH-ZD1J.

Two patches from this fork are required for the nasne, both already
applied on this fork's `main`:

1. **usbfs MIPS ioctl numbers** — without it, opening the device
   fails immediately with `inappropriate ioctl for device` (ENOTTY),
   because the request numbers were encoded with asm-generic IOC
   constants that do not match the MIPS kernel ABI.
2. **TS analyser head offset** — without it, reception works but
   cannot keep up with real time (see throughput below).

Final on-device binaries (with both patches):

| Binary | Size (bytes) | SHA-256 |
|---|---|---|
| coax  | 11,444,713 | `2d048dc0b021f2facbafae902e02a4e53124239b979713ef61563f7c8e2df0c5` |
| coaxd | 14,346,402 | `ab8a549c8cf855ab7f70ce6f76424c0610d01afbe03264e6f631e6c69c6daf9b` |

## Reception results

- `coax list` shows `cech-zd1j:p1-2:0 ISDB-T free Sony CECH-ZD1 J
  (MB86H47A + DiB8096)`, same as on an x86 host.
- `coax tune --system ISDBT --ch 27` locks and produces a clean TS:
  sync bytes 100%, continuity errors 0 on all content PIDs, PAT
  present, 30–32 PIDs including video 0x100 and audio 0x110 —
  the same composition as a full TS captured on an x86 host.
- coaxd idle (resident, `--idle 0`): RSS about 11 MB, 0% CPU over a
  20 s sample.

## Throughput: the analyser bottleneck and the fix

With only the ioctl patch, reception was established but the
effective rate was only about 0.34 MB/s while coaxd used 93–96% of
one core (98.8% of it user time). The captured TS had no missing
content — its PCR span matched "bytes ÷ full rate" — i.e. the host
was reading the bridge ring out more slowly than real time, falling
behind instead of dropping data.

A CPU profile (pprof build, 15 s during reception) put 99.14% of
samples in `runtime.memmove`, called from
`pkg/mpegts/analyse.(*Analyser).advance`: the analyser shifted its
whole retained buffer for every 188-byte packet, an O(chunk²/188)
copy pattern (about 170x copy amplification at 64 KiB chunks). On
x86 this is hidden by memmove bandwidth; on the nasne's single MIPS
core it saturates the CPU.

With the head-offset fix (this fork's second patch):

| Build | Effective rate | coaxd CPU (of 1 core) |
|---|---|---|
| ioctl patch only | 0.34–0.36 MB/s | ~93–96% |
| + analyser fix (final) | **2.19–2.24 MB/s (full rate)** | **~26%** |

Final-build spot check: 46,780,228 bytes / 248,831 packets, sync
mismatches 0, continuity errors 0, PCR span 21.40 s matching both
"bytes ÷ full rate" (21.14 s) and the actual reception window —
real-time reception is achieved. coaxd RSS during reception is
about 15–21 MB.

## Simultaneous reception: torne + internal tuner

Both tuners receiving ch27 at the same time for ~60–100 s
(torne via coax writing to disk on the device; internal tuner
fetched by curl from a remote host):

- torne: 17.26–17.28 Mbps sustained, sync 100%, continuity errors
  only on the null PID (content PIDs: 0).
- internal: full duration delivered, continuity errors only at
  connection start (3–4), no driver ReceiveStream failures.
- Whole-system CPU: 45.7% busy (idle 54.3%) — coaxd 26.5%
  (RSS 15.7 MB), coax CLI 7.8% (RSS 7.5 MB), everything else
  about 11%.
- Memory: MemAvailable stable around 170–179 MB, no swap.
- No USB disconnects or errors in dmesg.

So the nasne can record from its internal tuner and a torne at the
same time, with roughly half the CPU still idle. Note the streams
differ in composition: the internal tuner's stream is processed
(19 PIDs, no null padding, about 16 Mbps by PCR), while coax
delivers the raw multiplex (about 17.3 Mbps).

## Operational cautions found during testing

- **Do not fetch the internal tuner's (8301) stream with a slow
  on-device loop.** A Python fetcher on the nasne itself (small
  reads alternating with disk writes) back-pressured the stream;
  the driver logged hundreds of ReceiveStream failures and recpt1
  closed the stream early — while the delivered bytes stayed nearly
  intact, so it looks like a rate problem. It reproduces with the
  internal tuner alone. Receive from a remote client (curl) or a
  well-buffered reader.
- **The device's /tmp is a 120 MB tmpfs.** A measurement run failed
  with ENOSPC because leftover TS captures had filled it; the
  output file stopped at exactly 70 × 64 KiB. Write captures to
  disk-backed storage and check free space first.
- During all sessions the torne never disconnected or re-enumerated
  on bus power. Simultaneous use with an external USB SSD was not
  tested; if bus power becomes an issue, use a self-powered hub.

## Not yet verified on the device

- `coax scan` on the nasne (tune is verified; scan is not).
- The MMT/TLV analyser change (same fix) against a real TLV stream;
  it is covered by unit tests only.
- Descrambling: coax delivers the broadcast-scrambled TS as-is.
  Decryption is an external decoder's job; the B-CAS card in the
  torne's slot is reachable through the PC/SC socket that
  `coax pcsc` provides.
