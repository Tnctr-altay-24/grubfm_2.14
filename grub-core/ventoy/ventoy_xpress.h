/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 */

#ifndef GRUB_VENTOY_XPRESS_H
#define GRUB_VENTOY_XPRESS_H 1

#include "ventoy_huffman.h"

#define XCA_CODES 512

struct xca
{
  struct huffman_alphabet alphabet;
  huffman_raw_symbol_t raw[XCA_CODES];
  uint8_t lengths[XCA_CODES];
};

struct xca_huf_len
{
  uint8_t nibbles[XCA_CODES / 2];
} __attribute__ ((packed));

static inline unsigned int
xca_huf_len (const struct xca_huf_len *lengths, unsigned int symbol)
{
  return (((lengths->nibbles[symbol / 2]) >> (4 * (symbol % 2))) & 0x0f);
}

#define XCA_GET16(src) ({                \
  const uint16_t *src16 = src;           \
  src = (uint8_t *) src + sizeof (*src16); \
  *src16; })

#define XCA_GET8(src) ({                  \
  const uint8_t *src8 = src;             \
  src = (uint8_t *) src + sizeof (*src8); \
  *src8; })

#define XCA_END_MARKER 256
#define XCA_BLOCK_SIZE (64 * 1024)

grub_ssize_t ventoy_xca_decompress (const void *data, grub_size_t len, void *buf);

#endif
