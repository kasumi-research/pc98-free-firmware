/*
 * Keyboard translate tables: eight 0x60-byte tables indexed by shift
 * state (0 normal, 1 shift, 2 caps, 3 caps+shift, 4/5 kana, 6 ctrl,
 * 7 graph).  Values are NEC BIOS key codes; 0xFF means "no code".
 *
 * These are published: DOS input methods follow WA_KB_CODE_SEG/OFF to
 * this array and index it themselves, so the stride and the shift-state
 * order are interface even though the address is ours.
 *
 * Scancodes 0x35 (XFER), 0x3E (HOME) and 0x51 (NFER) and the function
 * keys produce a two-byte code (table value << 8); everything else
 * produces (scancode << 8) | value.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>

/* 0x30..0x5F: punctuation, space, editing keys, keypad, function keys */
#define KEYROW_COMMON \
    ',', '.', '/', 0xff, ' ', 0x35, 0x00, 0x00,                 \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x00,             \
    '-', '/', '7', '8', '9', '*', '4', '5',                     \
    '6', '+', '1', '2', '3', '=', '0', ',',                     \
    '.', 0x51, 0xff, 0xff, 0xff, 0xff, 0x62, 0x63,              \
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b

const u8 keytable[8][0x60] = {
    /* 0: normal */
    {
        0x1b, '1', '2', '3', '4', '5', '6', '7',
        '8', '9', '0', '-', '^', 0x5c, 0x08, 0x09,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
        'o', 'p', '@', '[', 0x0d, 'a', 's', 'd',
        'f', 'g', 'h', 'j', 'k', 'l', ';', ':',
        ']', 'z', 'x', 'c', 'v', 'b', 'n', 'm',
        KEYROW_COMMON
    },
    /* 1: shift */
    {
        0x1b, '!', 0x22, '#', '$', '%', '&', 0x27,
        '(', ')', 0xff, '=', '~', '|', 0x08, 0x09,
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
        'O', 'P', '`', '{', 0x0d, 'A', 'S', 'D',
        'F', 'G', 'H', 'J', 'K', 'L', '+', '*',
        '}', 'Z', 'X', 'C', 'V', 'B', 'N', 'M',
        '<', '>', '?', '_', ' ', 0x35, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x00,
        '-', '/', '7', '8', '9', '*', '4', '5',
        '6', '+', '1', '2', '3', '=', '0', ',',
        '.', 0x51, 0xff, 0xff, 0xff, 0xff, 0x62, 0x63,
        0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b
    },
    /* 2: caps */
    {
        0x1b, '1', '2', '3', '4', '5', '6', '7',
        '8', '9', '0', '-', '^', 0x5c, 0x08, 0x09,
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
        'O', 'P', '@', '[', 0x0d, 'A', 'S', 'D',
        'F', 'G', 'H', 'J', 'K', 'L', ';', ':',
        ']', 'Z', 'X', 'C', 'V', 'B', 'N', 'M',
        KEYROW_COMMON
    },
    /* 3: caps + shift */
    {
        0x1b, '!', 0x22, '#', '$', '%', '&', 0x27,
        '(', ')', 0xff, '=', '~', '|', 0x08, 0x09,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
        'o', 'p', '`', '{', 0x0d, 'a', 's', 'd',
        'f', 'g', 'h', 'j', 'k', 'l', '+', '*',
        '}', 'z', 'x', 'c', 'v', 'b', 'n', 'm',
        KEYROW_COMMON
    },
    /* 4, 5: kana -- not populated */
    { [0 ... 0x5f] = 0xff },
    { [0 ... 0x5f] = 0xff },
    /* 6: ctrl */
    {
        0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0x1e, 0x1c, 0x08, 0x09,
        0x11, 0x17, 0x05, 0x12, 0x14, 0x19, 0x15, 0x09,
        0x0f, 0x10, 0x00, 0x1b, 0x0d, 0x01, 0x13, 0x04,
        0x06, 0x07, 0x08, 0x0a, 0x0b, 0x0c, 0xff, 0xff,
        0x1d, 0x1a, 0x18, 0x03, 0x16, 0x02, 0x0e, 0x0d,
        KEYROW_COMMON
    },
    /* 7: graph -- not populated */
    { [0 ... 0x5f] = 0xff },
};
