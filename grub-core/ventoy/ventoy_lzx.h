/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 */

#ifndef GRUB_VENTOY_LZX_H
#define GRUB_VENTOY_LZX_H 1

#include "ventoy_huffman.h"

#define LZX_ALIGNOFFSET_CODES 8
#define LZX_ALIGNOFFSET_BITS 3
#define LZX_PRETREE_CODES 20
#define LZX_PRETREE_BITS 4
#define LZX_MAIN_LIT_CODES 256
#define LZX_POSITION_SLOTS 30
#define LZX_MAIN_CODES (LZX_MAIN_LIT_CODES + (8 * LZX_POSITION_SLOTS))
#define LZX_LENGTH_CODES 249
#define LZX_BLOCK_TYPE_BITS 3
#define LZX_DEFAULT_BLOCK_LEN 32768
#define LZX_REPEATED_OFFSETS 3
#define LZX_WIM_MAGIC_FILESIZE 12000000

enum lzx_block_type
{
  LZX_BLOCK_VERBATIM = 1,
  LZX_BLOCK_ALIGNOFFSET = 2,
  LZX_BLOCK_UNCOMPRESSED = 3,
};

struct lzx_input_stream
{
  const uint8_t *data;
  size_t len;
  size_t offset;
};

struct lzx_output_stream
{
  uint8_t *data;
  size_t offset;
  size_t threshold;
};

struct lzx
{
  struct lzx_input_stream input;
  struct lzx_output_stream output;
  uint32_t accumulator;
  unsigned int bits;
  enum lzx_block_type block_type;
  unsigned int repeated_offset[LZX_REPEATED_OFFSETS];

  struct huffman_alphabet alignoffset;
  huffman_raw_symbol_t alignoffset_raw[LZX_ALIGNOFFSET_CODES];
  uint8_t alignoffset_lengths[LZX_ALIGNOFFSET_CODES];

  struct huffman_alphabet pretree;
  huffman_raw_symbol_t pretree_raw[LZX_PRETREE_CODES];
  uint8_t pretree_lengths[LZX_PRETREE_CODES];

  struct huffman_alphabet main;
  huffman_raw_symbol_t main_raw[LZX_MAIN_CODES];
  struct
  {
    uint8_t literals[LZX_MAIN_LIT_CODES];
    uint8_t remainder[LZX_MAIN_CODES - LZX_MAIN_LIT_CODES];
  } __attribute__ ((packed)) main_lengths;

  struct huffman_alphabet length;
  huffman_raw_symbol_t length_raw[LZX_LENGTH_CODES];
  uint8_t length_lengths[LZX_LENGTH_CODES];
};

static inline unsigned int
lzx_footer_bits (unsigned int position_slot)
{
  if (position_slot < 2)
    return 0;
  else if (position_slot < 38)
    return ((position_slot / 2) - 1);
  else
    return 17;
}

grub_ssize_t ventoy_lzx_decompress (const void *data, grub_size_t len, void *buf);

#endif
