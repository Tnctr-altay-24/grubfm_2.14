/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 */

#ifndef GRUB_VENTOY_HUFFMAN_H
#define GRUB_VENTOY_HUFFMAN_H 1

#include "ventoy_wimboot.h"

#define HUFFMAN_BITS 16

typedef uint16_t huffman_raw_symbol_t;

#define HUFFMAN_QL_BITS 7
#define HUFFMAN_QL_SHIFT (HUFFMAN_BITS - HUFFMAN_QL_BITS)

struct huffman_symbols
{
  uint8_t bits;
  uint8_t shift;
  uint16_t freq;
  uint32_t start;
  huffman_raw_symbol_t *raw;
};

struct huffman_alphabet
{
  struct huffman_symbols huf[HUFFMAN_BITS];
  uint8_t lookup[1 << HUFFMAN_QL_BITS];
  huffman_raw_symbol_t raw[0];
};

static inline __attribute__ ((always_inline)) unsigned int
huffman_len (struct huffman_symbols *sym)
{
  return sym->bits;
}

static inline __attribute__ ((always_inline)) huffman_raw_symbol_t
huffman_raw (struct huffman_symbols *sym, unsigned int huf)
{
  return sym->raw[huf >> sym->shift];
}

int ventoy_huffman_alphabet (struct huffman_alphabet *alphabet,
                             uint8_t *lengths,
                             unsigned int count);

struct huffman_symbols *ventoy_huffman_sym (struct huffman_alphabet *alphabet,
                                            unsigned int huf);

#endif
