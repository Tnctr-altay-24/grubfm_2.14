#ifndef GRUB_VENTOY_MINIZ_H
#define GRUB_VENTOY_MINIZ_H

#include <grub/types.h>

typedef unsigned long mz_ulong;

typedef void *(*mz_alloc_func)(void *opaque, grub_size_t items, grub_size_t size);
typedef void (*mz_free_func)(void *opaque, void *address);
typedef void *(*mz_realloc_func)(void *opaque, void *address, grub_size_t items, grub_size_t size);

struct mz_internal_state;

typedef struct mz_stream_s
{
  const unsigned char *next_in;
  unsigned int avail_in;
  mz_ulong total_in;

  unsigned char *next_out;
  unsigned int avail_out;
  mz_ulong total_out;

  char *msg;
  struct mz_internal_state *state;

  mz_alloc_func zalloc;
  mz_free_func zfree;
  void *opaque;

  int data_type;
  mz_ulong adler;
  mz_ulong crc32;
  mz_ulong reserved;
} mz_stream;

typedef mz_stream *mz_streamp;

enum
{
  MZ_DEFAULT_STRATEGY = 0,
  MZ_FILTERED = 1,
  MZ_HUFFMAN_ONLY = 2,
  MZ_RLE = 3,
  MZ_FIXED = 4
};

#define MZ_DEFLATED 8
#define MZ_DEFAULT_WINDOW_BITS 15

enum
{
  MZ_NO_FLUSH = 0,
  MZ_PARTIAL_FLUSH = 1,
  MZ_SYNC_FLUSH = 2,
  MZ_FULL_FLUSH = 3,
  MZ_FINISH = 4,
  MZ_BLOCK = 5
};

enum
{
  MZ_OK = 0,
  MZ_STREAM_END = 1,
  MZ_NEED_DICT = 2,
  MZ_ERRNO = -1,
  MZ_STREAM_ERROR = -2,
  MZ_DATA_ERROR = -3,
  MZ_MEM_ERROR = -4,
  MZ_BUF_ERROR = -5,
  MZ_VERSION_ERROR = -6,
  MZ_PARAM_ERROR = -10000
};

int mz_deflateInit2 (mz_streamp pStream, int level, int method,
                     int window_bits, int mem_level, int strategy);
int mz_deflate (mz_streamp pStream, int flush);
int mz_deflateEnd (mz_streamp pStream);

#endif
