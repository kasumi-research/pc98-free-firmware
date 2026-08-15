# Notes

The working record behind the code: what was measured, what was tried
and rejected, and which gaps are decisions rather than oversights.
`README.md` is the map of the machine; this is the log of finding it.

Everything here was measured against the NEC firmware on the same
emulated machine and the same disk image, not recalled.  Where a
finding lives better as a comment next to the code it explains, it is
there and this file only points at it.

## 1. State

Both targets boot to a working desktop with no NEC ROM present:

* **`pc98-xa7` boots Windows 98** to an interactive 640x480 desktop,
  with the Trident TGUI9660 driving the CRT and `TR968X.DLL` loaded —
  on the first boot of a fresh image, with no display-settings dialog.
* **`pc98-rvii26` boots Windows 2000** to a 1280x1024 desktop with both
  processors running kernel code and a taskbar clock that keeps time.

Ours: POST, PCI resource assignment and interrupt routing, the work
area, the IVT, INT 18h/1Ah/1Bh/1Ch/1Fh, the display and calendar
services, the IDE transport, memory sizing, the PCI expansion-ROM scan,
the MPS tables, the IPL, the PCI BIOS, BIOS32 and `$PnP`.  The RvII26's
disk comes from the AIC-7860 card's own expansion ROM, which our scan
stages, initialises, relocates into the C-bus window and installs — the
same two-stage protocol the NEC ROM uses, recovered by diffing the two
firmwares' register traces of the card (`core/boot/optrom.c`).

N88-BASIC(86) is not implemented and nothing we boot needs it.

## 2. Publishing a structure where the OS will find it

A published structure has to be **inside the window the OS scan
actually covers**, and that window is narrower than the specification
suggests.  Measured with 16-byte-boundary probes on a Windows 98 boot:

* The ring-0 scanner at `pc=c180699b` reads one dword per 16-byte
  boundary across **F0000-F7FFF only**.  On the reference it stops
  after 1308 boundaries — 1308 × 16 = 0x51C0, i.e. it found `$PnP` at
  F51B0 and quit — and four more ring-0 passes then run over the same
  window (`c0002370`, `c180c610`, `c18068cc`, `c180d6d2`).
* With our structures up at F8xxx it read all 2048 boundaries, found
  nothing, and not one of those later passes ran.

Two earlier placements were both wrong for this reason: FD030/FD360
(never reached at all) and then F8EE0/F9080 inside BANK7 ("low", but
still past the end of the scan).  BANK6 is mapped unconditionally at
F0000, so both structures now sit at the reference's own addresses —
`_32_` at F4C40, `$PnP` at F51B0.  A structure the scan cannot reach is
exactly as invisible as no structure at all, and just as silent.

The checksum is filled in by `tools/mkflash.py` for the same reason the
MPS checksums are: an assembler cannot sum bytes it has not emitted
yet, and a structure with the wrong sum is invisible in exactly the
same silent way.

## 3. The Windows 98 display path

Getting Windows 98 to drive the Trident instead of falling back to the
mainboard adapter took five things, each necessary and none sufficient.
The visible symptom for all of them was the same: `EGCN4.DRV` loaded
instead of `TR968X.DLL`, and a pre-shell "display settings updated …
restart" dialog on every virgin image.

1. **INT 18h AH=31h must report the mode the machine is actually in.**
   Windows reads it at startup and, when it does not describe the mode
   it means to run, sets it with AH=30h and records that as a settings
   change.  (`core/bios/int18.c`)
2. **PCI INT_LINE gets written for every device with a pin**, 0xFF
   included — the byte tracks the pin, not whether the routing is
   known.  Leaving the model's 0x00 standing told Windows the display
   adapter was wired to IRQ 0, the system timer.  (`core/pci/pci.c`)
3. **The Xa7's PIRQ router is programmed at POST.**  It is the C-bus
   bridge's, at 00:06.0 config 0x60-0x63 — the same place the RvII26's
   Champion keeps its own, not "unlocated on this chipset" as the code
   claimed for a long time.  (`hal/xa7/chipset.c`)
4. **`_32_` and `$PnP` are published inside F0000-F7FFF** — section 2.
5. **The `$PnP` protected-mode entry is pinned at 000F5600:0024.**

Item 5 is the one that is not a matter of doing the general thing
correctly.  Windows 98's `BIOS.VXD` does not accept an arbitrary PnP
BIOS entry point: it recognises exactly two, **by hardcoded address**,
and ignores every other one — disassembled from a running reference at
`c1806a6d`:

```
cmpl $0xd8000,-0xc(%ebp)    PM code base 000D8000
cmpw $0x3a,-0x8(%ebp)         with entry offset 003A
cmpl $0xf5600,-0xc(%ebp)    PM code base 000F5600
cmp  %si,-0x8(%ebp)           with entry offset 0024   (esi = 0x24)
```

Only those two reach the call pair at `c1806a8c` that leads on to bus
enumeration.  With the entry anywhere else, `BIOS.VXD` reads the
structure, verifies its checksum, reads all seven fields — and then
falls through the whitelist and quietly stops.  No Start, no Enumerate,
no `BIOS\*PNP0A03\00`, no PCI bus, no Trident.  The first address is
the NEC firmware's own D800:003A, inside the paged D8000 window we do
not have; the second lands at F5624, in BANK6, which we can serve
directly.  See `include/pc98/layout.h`.

**Acceptance metrics.**  Virgin overlay, free ROM, against the
reference — this is what "the display path works" means concretely:

|                            | before | after           | reference |
|---|---|---|---|
| INT 18h AH=81h calls       | 0      | 1               | 1         |
| INT 18h AH=80h calls       | 0      | 1               | 1         |
| INT 1Fh AH=CCh calls       | 0      | 18              | 18        |
| calls from cs=2037 (EGCN4) | 8      | 0               | 0         |
| port 0FACh video relay     | 00     | 00/02, settles 02 | 00/02, settles 02 |

The 18 AH=CCh calls are a driver at cs=033f doing the installation
check, finding 1023:9660, then sizing the Trident's BAR0 by writing
FFFF:FFFF and restoring it.

## 4. Tried and rejected — do not retry

* **Advertising no protected-mode `$PnP` entry** (entry offset and PM
  code base both zero), on the theory that the reference takes only a
  real-mode call while we take both: it **hangs the boot** — black
  screen, the display stage never reached.
* **Running an expansion ROM image in the C-bus window instead of
  staging it in conventional memory first**: register-for-register
  identical to the NEC ROM for 36776 card accesses, and then it simply
  stops, right where the NEC ROM relocates and carries on.  The
  two-stage shape is forced by measurement (`core/boot/optrom.c`).

## 5. Oracles, and how any of this is checked

The method throughout is differential: boot the same disk image on the
same machine twice, once with `-bios` pointed at a dump of the NEC ROM
and once at ours, and diff.  A finding that cannot be stated as a
difference between those two runs is not a finding yet.

* `test/wadump.asm` — a boot sector that dumps the 0000:0400 work area
  over the serial port.  Every value the work area is seeded with was
  established by booting this through both firmwares and diffing.
* `test/pnpdump.asm` — finds the `$PnP` structure, sweeps the far-call
  functions and the INT 1Fh AH=CEh subfunctions, and prints every
  status.  **It loops functions 0 through 9 only**, so its "the machine
  answers 0x82 to everything" result does not cover function 40h, which
  the reference does implement.
* A TCG plugin that logs the `CD 18` instruction itself, so the same
  instrument runs unmodified on either ROM — that is what showed the
  two boots in lockstep up to the AH=31h call and diverging immediately
  after it.
* Physical-address read probes on 16-byte boundaries, which is how the
  scan windows in section 2 were bounded, and how "read 84 times a
  boot" is distinguished from "never read".
* Register-level traces of the AIC-7860 under both firmwares, compared
  access by access: identical for 35422 accesses and then not, which is
  what located the second stage of the option-ROM protocol.

Two working rules: boot Windows images over a **throwaway qcow2
overlay, never the base image**, and re-test on a **virgin** image —
several of these symptoms only appear on a first boot, because Windows
caches its conclusions about the hardware and a machine that once
booted wrong keeps booting wrong for its own reasons.

## 6. Distant symptoms, one-byte causes

Kept as an index; each is explained where it is fixed.

| Symptom | Cause |
|---|---|
| SDIP buffer written and read back with different contents | The SHUT0 test read the 8255 port *between* the `xor` that cleared AX and the `mov`s that loaded SS and ES from it — the whole cold path ran with SS = ES = 00B8 and DS = 0000 (`core/bios/entry.S`) |
| What looks like an interrupt storm | It is a fault storm: NTLDR thunks to real mode with ESP = 0x00067FA8, and 16-bit code addressing locals off `%esp` runs past the segment limit.  The giveaway is IF clear in the interrupted frame (`core/bios/vectors.S`) |
| Windows 2000 binds no keyboard; IOAPIC pin 1 masked at vector 255 for the life of the boot | Work-area 0x0481 (KEYB_TYPE) written as 0x02 — copied from a capture without decoding it — which says "old keyboard, or none attached" (`core/bios/bios.c`) |
| A machine with no battery-backed state boots with a wrong GDC clock, wrong text rows, wrong DMA clock | An all-zero SDIP store fails its own parity discipline; the NEC ROM detects that and writes factory defaults (`core/lib/sdip.c`) |
| The disk is found and sized, and then nothing can reach it | The option ROM's second stage — relocate into the C-bus window and call +0x24 *there* — was skipped (`core/boot/optrom.c`) |
| HIMEM.SYS refuses to load | Megabyte-granular memory sizing overshoots; the Xa7 has a real hole between the two regions, so 0x0401 and 0x0594 are probed separately |

## 7. Deliberate gaps

`core/pci/pci.c` (`pci_assign()`) is deliberately narrower than the NEC
firmware's PCI/PnP resource arbitrator.  That narrowness is a decision:
NEC needed a dynamic pool allocator because a real machine had
user-installed cards in physical slots the firmware could not know
about; we know our slot population at build time and the metal's
routing is hardwired, so a static per-machine routing function is the
more faithful model.  Adopting NEC's dynamic policy is how you get "the
rebuilt config puts the AIC on IRQ5 when the metal says IRQ3".

Each gap below is correct for the devices we have today, with the
evidence for that and the precise trigger that turns it into work.
None of them is a reason to reimplement the module.

**1. Only function 0 of each device is probed.**  `pci_assign()` builds
`bdf = (u8)(dev << 3)` and walks `dev` 0-31, so functions 1-7 are never
enumerated — no BARs placed, no interrupt line, no command bits.
Correct today: every device on both machines is single-function.
*Trigger:* the first multifunction device on either bus.  Roughly three
lines — an inner loop over `fn`, honouring the multifunction bit in the
header type.

**2. The memory and expansion-ROM windows are 16 MiB apart with no
bound check.**  `PCI_MEM_BASE` 0x20000000 and `PCI_ROM_BASE` 0x21000000
both grow upward with natural alignment, and `assign_bar()` has no
failure path, so a single large aperture consumes the gap and
subsequent BARs land silently on top of the ROM window.  Correct today:
the current device set fits with room to spare.  *Trigger:* the first
device with a large memory BAR — a framebuffer aperture is the obvious
one.  The fix is a bound plus a wider separation, and on the RvII26 it
must also dodge the 16 MB system space (host-bridge cfg 0x77 bit 4) and
the PEGC aperture at 0xFFF00000.

**3. Xa7 interrupt routing** — *closed.*  This was recorded as a gap
whose trigger was "the first Xa7 device whose expansion ROM hooks an
interrupt at POST", on the reasoning that Windows programs the PIRQ
router itself.  The trigger fired the same day it was written, from a
direction it did not predict: not an option ROM, but Windows 98's own
display detection.  `hal_pci_irq_init()` now programs the router.  The
shape of the entry was still the point — record why a gap is safe now
and what will make it unsafe, so the next person can tell a decision
from an oversight.

**4. PCI-PCI bridges are skipped** (`if (hdr != 0) continue;`).
Correct today: the only bridge on either bus is the C-bus bridge at
06.0, whose config space we read for interrupt routing but which has no
PCI devices behind it.  *Trigger:* the first device behind a bridge.
This is the one place the NEC arbitrator does something we do not — it
recurses into class 0x0604 and sizes bridge windows at 4 KiB I/O /
1 MiB memory granularity.  Write the recursion then; it is still not a
reason to port 16 KiB of 16-bit code.

**5. INT 1Fh AH=CEh subfunction 03, the ACFG write side, is not
implemented.**  The measured contract: `CE03` takes DS:SI, stores
`-(sum of [si..si+len-3])` as a word at `[si+len-2]`, then writes
`[si]` bytes back to the battery-backed CG config plane.  Correct
today: we have no arbitrator producing a blob, and on metal a write
could only corrupt a real one.  *Trigger:* the moment we need to
*persist* resource policy rather than recompute it at POST.  This is
not a consumer-less service — the ACFG blob is where IRQ reservations
live, and the reservation of IRQ5 for the second CCU is what makes the
Windows 2000 NTDETECT serial probe pass.  As IRQs get scarce, "IRQ5
belongs to the second CCU, do not hand it to PCI" stops being firmware
bookkeeping and becomes shared policy.  BIOS32 therefore publishes
`$PCI` only, not the reference's `$ACF`: offering a store we cannot
keep is worse than not offering it.

**6. INT 1Fh AH=CDh, the chipset window-decode service, answers
AX=8000h to all of it.**  That is the service's own idiom for a request
it will not answer, and it is truthful; before it, AH=CDh fell through
the "80h-8Fh: succeed" arm and returned CF=0 with stale registers,
which a caller cannot detect.  The contract is fully decoded (handler
at 0xa7d of the reference's D8000 module, reached because the AH
dispatcher at 0x7a5 routes everything that is not CCh or CEh there).
Status is AX=0000h success / AX=8000h error, and the dispatcher copies
the handler's carry into the IRET frame.

*AL=01h, query one window.*  BH = the window's segment high byte, which
must be C0h-DFh or the answer is 8000h.

```
reg   = 0x66 + ((BH - 0xC0) >> 4)   /* 0x66 covers C0000-CFFFF, 0x67 D0000-DFFFF */
byte  = host-bridge (0:0.0) config byte `reg`
field = (BH >> 2) & 3               /* which 16 KiB granule of the four */
DL    = (byte & (1 << (field * 2))) ? 0 : 1
```

Each register holds four 2-bit fields, one per 16 KiB granule, and the
query tests the low bit of the field — the same bit pair the reference's
D8000 dispatch thunk clears to page its module in.

*AL=02h, set one or more windows.*  BH as above, BL nonzero, DL = the
new value.  Builds an OR value from `((DL & 1) ^ 1) << (field * 2)` and
an AND mask of `~(1 << (field * 2))`, then repeats `(BL >> 5)` times —
or once if that is zero — read-modify-writing the register and
advancing BH by 4 (one 16 KiB granule) each pass.

**Do not implement AL=02h from this description alone.**  The OR value
and AND mask are computed once, before the loop, from the original BH,
while the register index *is* recomputed from the advancing BH inside
it.  Either that is a bug in the NEC firmware or the register
save/restore around the loop body means something other than it
appears.  It writes chipset decode registers, so a misreading is
expensive, and there is no measured consumer.

*Trigger for either:* something actually calling AH=CDh.  Implementing
it also needs a HAL hook, because the register is per-machine —
host-bridge cfg 0x66/0x67 on the Xa7, 0x70-0x73 on the RvII26's
Champion — and the RvII26 side is unmeasured.

### Permanently out of scope

Reimplementing the D8000 module itself.  It is **packaging, not ABI**:
BANK7 ran out of room, so NEC paged the PCI/PnP code into a 16 KiB
window behind port 063Ch and a host-bridge cfg 0x67 dance.  Measured,
it writes nothing below 0x20000 — no IVT entry, no work-area byte, no
published pointer — so no guest can observe its absence.  Everything
with a consumer outside the firmware is reachable as a vector: BIOS32
`_32_`, real-mode INT 1Fh AH=CC/CD/CE, INT 1Ah AH=B1/B4, and the `$PnP`
structure.  Implement the vectors; skip the module.

Specifically out: the RAM agent / resource arbitrator, the 063Ch window
paging and the F560 dispatch thunk, the 11-entry jump table, the C-bus
option-ROM walkers (+0x0F/+0x12/+0x15), and the BANK2 staging helper
(+0x21).

## 8. Open ends

* **`$PnP` far-call functions 0 through 9 answer 0x82** on both the
  reference and ours; function 40h (Get PnP ISA Configuration
  Structure) is the one the reference implements and is transcribed
  faithfully — but no OS has ever been observed calling it.  Windows
  asks this entry for exactly one function, 05h, once, and both
  firmwares answer 0x82.
* **INT 1Fh AH=CEh** carries the PC-98 spelling of the PnP services and
  its answers are constants for AL=00/01.  AL=02/03 are called once
  each by the real-mode PC-98 support during a Windows 98 startup; the
  reference answers success and writes nothing the caller reads, so we
  do the same.  AL=04 and up trace and fail on purpose: their contract
  has not been measured, and a plausible-looking wrong answer is worse
  than a clear "no".  On the RvII26 `hal_pnp_ce()` returns NULL
  deliberately — its CE answers are not measured there.
* **F8EBF reads 0x21** on the reference and the semantics are not
  known.  It is reproduced because it is measured; `pc=c0381ebf` reads
  that byte specifically on a live Windows 98 boot.  The same applies
  to the F8EC0 far-callable routine next to it.
* **The RvII26's D8000 window** — the README's bank 3 note asserts the
  same window bank 0's note makes conditional on an Xa7 host-bridge
  register.  Plausible that both are right (different chipsets,
  different registers, same address window), but it has not been
  checked against the Champion chipset code.
* **The emulator is a dependency, not a constant.**  About one boot in
  four used to escape through an application processor coming up in
  real mode at 0000:0064 from a SIPI with vector 0.  It was not a
  firmware bug: the QEMU pc98 machine had no reset method, so
  `x86_cpu_after_reset()` never ran, the local APICs were never reset,
  `wait_for_sipi` stayed clear and the firmware-side wait-for-SIPI
  guard was inert.  Fixed in QEMU `a6ad068cde`.  Both of our reset
  entries still park an AP on IA32_APIC_BASE bit 8, because a slipped
  AP running POST underneath the boot processor is worth guarding
  against on its own account.
