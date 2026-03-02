#ifndef GRUB_LOOPBACK_FILE_HEADER
#define GRUB_LOOPBACK_FILE_HEADER 1

#include <grub/file.h>

struct grub_loopback_file_options
{
  int mem;
  int blocklist;
};

typedef grub_file_t (*grub_loopback_file_open_t) (const char *name,
                                                  const struct grub_loopback_file_options *options,
                                                  enum grub_file_type type);
typedef void (*grub_loopback_file_close_t) (grub_file_t file);
typedef grub_err_t (*grub_loopback_file_write_t) (grub_file_t file,
                                                  const void *buf,
                                                  grub_size_t len,
                                                  grub_off_t offset);

struct grub_loopback_file_provider
{
  const char *name;
  grub_loopback_file_open_t open;
  grub_loopback_file_close_t close;
  grub_loopback_file_write_t write;
};

int grub_loopback_file_is_mem_name (const char *name);
const struct grub_loopback_file_provider *grub_loopback_file_default_provider (void);
grub_file_t grub_loopback_file_open_with (const struct grub_loopback_file_provider *provider,
                                          const char *name,
                                          const struct grub_loopback_file_options *options,
                                          enum grub_file_type type);
void grub_loopback_file_close_with (const struct grub_loopback_file_provider *provider,
                                    grub_file_t file);
grub_err_t grub_loopback_file_write_with (const struct grub_loopback_file_provider *provider,
                                          grub_file_t file,
                                          const void *buf,
                                          grub_size_t len,
                                          grub_off_t offset);
grub_file_t grub_loopback_file_open (const char *name, int mem, int bl,
                                     enum grub_file_type type);
void grub_loopback_file_close (grub_file_t file);
grub_err_t grub_loopback_file_write (grub_file_t file, const void *buf,
                                     grub_size_t len, grub_off_t offset);

#endif
