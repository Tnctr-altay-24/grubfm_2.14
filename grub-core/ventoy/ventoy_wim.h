/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2026  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef GRUB_VENTOY_WIM_H
#define GRUB_VENTOY_WIM_H 1

#include <stddef.h>
#include <grub/types.h>
#include "ventoy_vfat.h"

struct wim_resource_header
{
  grub_uint64_t zlen__flags;
  grub_uint64_t offset;
  grub_uint64_t len;
} GRUB_PACKED;

#define WIM_RESHDR_ZLEN_MASK 0x00ffffffffffffffULL

enum wim_resource_header_flags
{
  WIM_RESHDR_METADATA = (0x02ULL << 56),
  WIM_RESHDR_COMPRESSED = (0x04ULL << 56),
  WIM_RESHDR_PACKED_STREAMS = (0x10ULL << 56),
};

struct ventoy_wim_header
{
  grub_uint8_t signature[8];
  grub_uint32_t header_len;
  grub_uint32_t version;
  grub_uint32_t flags;
  grub_uint32_t chunk_len;
  grub_uint8_t guid[16];
  grub_uint16_t part;
  grub_uint16_t parts;
  grub_uint32_t images;
  struct wim_resource_header lookup;
  struct wim_resource_header xml;
  struct wim_resource_header boot;
  grub_uint32_t boot_index;
  struct wim_resource_header integrity;
  grub_uint8_t reserved[60];
} GRUB_PACKED;

enum wim_header_flags
{
  WIM_HDR_COMPRESS_RESERVED = 0x00010000,
  WIM_HDR_XPRESS = 0x00020000,
  WIM_HDR_LZX = 0x00040000,
  WIM_HDR_LZMS = 0x00080000,
};

struct wim_hash
{
  grub_uint8_t sha1[20];
} GRUB_PACKED;

struct wim_lookup_entry
{
  struct wim_resource_header resource;
  grub_uint16_t part;
  grub_uint32_t refcnt;
  struct wim_hash hash;
} GRUB_PACKED;

#define WIM_CHUNK_LEN 32768

struct wim_security_header
{
  grub_uint32_t len;
  grub_uint32_t count;
} GRUB_PACKED;

struct wim_directory_entry
{
  grub_uint64_t len;
  grub_uint32_t attributes;
  grub_uint32_t security;
  grub_uint64_t subdir;
  grub_uint8_t reserved1[16];
  grub_uint64_t created;
  grub_uint64_t accessed;
  grub_uint64_t written;
  struct wim_hash hash;
  grub_uint8_t reserved2[12];
  grub_uint16_t streams;
  grub_uint16_t short_name_len;
  grub_uint16_t name_len;
} GRUB_PACKED;

#define WIM_ATTR_NORMAL 0x00000080UL
#define WIM_NO_SECURITY 0xffffffffUL
#define WIM_MAGIC_TIME 0x1a7b83d2ad93000ULL

int
ventoy_wim_header (struct vfat_file *file, struct ventoy_wim_header *header);
int
ventoy_wim_count (struct vfat_file *file, struct ventoy_wim_header *header, unsigned int *count);
int
ventoy_wim_metadata (struct vfat_file *file, struct ventoy_wim_header *header,
              unsigned int index, struct wim_resource_header *meta);
int
ventoy_wim_read (struct vfat_file *file, struct ventoy_wim_header *header,
          struct wim_resource_header *resource, void *data,
          size_t offset, size_t len);
int
ventoy_wim_path (struct vfat_file *file, struct ventoy_wim_header *header,
          struct wim_resource_header *meta, const wchar_t *path,
          size_t *offset, struct wim_directory_entry *direntry);
int
ventoy_wim_file (struct vfat_file *file, struct ventoy_wim_header *header,
          struct wim_resource_header *meta, const wchar_t *path,
          struct wim_resource_header *resource);
int
ventoy_wim_dir_len (struct vfat_file *file, struct ventoy_wim_header *header,
             struct wim_resource_header *meta, size_t offset, size_t *len);

#endif
