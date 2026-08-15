/*
 * Fixed-width types.  Freestanding: no libc headers exist here.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_TYPES_H
#define PC98_TYPES_H

typedef unsigned char       u8;
typedef signed char         s8;
typedef unsigned short      u16;
typedef signed short        s16;
typedef unsigned int        u32;    /* -m16 still has 32-bit int */
typedef signed int          s32;
typedef unsigned long long  u64;
typedef signed long long    s64;

typedef unsigned int        uint;
typedef _Bool               bool;
#define true    1
#define false   0
#define NULL    ((void *)0)

#endif
