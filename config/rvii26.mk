# PC-9821RvII26 — dual Pentium II, ServerWorks (RCC) Champion CNB20-LE,
# IOAPIC + MPS, Adaptec AIC-7860 SCSI.  Target: Windows 2000.
#
# The firmware requires BOTH CPUs: the emulated machine only works with
# -smp 2, and the NEC ROM sprays over its POST module if CPU1 never
# answers.  CONFIG_SMP is not optional here.
#
# Only capabilities the sources actually test appear here.  A knob no
# source reads is not configuration, it is a comment that looks like
# code -- the chipset and the disk transport are selected by HAL_DIR
# below, not by a define.
#
# SPDX-License-Identifier: MIT

CONFIG += CONFIG_MODEL_RVII26
CONFIG += CONFIG_SMP
CONFIG += CONFIG_MPS
CONFIG += CONFIG_SMM

HAL_DIR   := rvii26
ROM_NAME  := pc98-free-rvii26.bin

ARCH      := i386
