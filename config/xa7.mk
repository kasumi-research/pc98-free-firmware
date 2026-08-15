# PC-9821Xa7 — Pentium, VLSI "Wildcat" chipset, IDE, optional Trident WAB.
# Target: MS-DOS, Windows 95/98.
#
# The chipset and the disk transport are selected by HAL_DIR, not by a
# define; only capabilities the sources actually test appear here.
#
# SPDX-License-Identifier: MIT

CONFIG += CONFIG_MODEL_XA7

# No SMP, no IOAPIC/MPS, no SMM on this machine.

HAL_DIR   := xa7
ROM_NAME  := pc98-free-xa7.bin

# -march: the Xa7 is a Pentium, but nothing here needs it.  Staying at
# i386 keeps one instruction set across every model.
ARCH      := i386
