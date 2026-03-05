/* CrScreenshotDxe.c

Copyright (c) 2016, Nikolaj Schlej, All rights reserved.

Redistribution and use in source and binary forms, 
with or without modification, are permitted provided that the following conditions are met:
- Redistributions of source code must retain the above copyright notice, 
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice, 
  this list of conditions and the following disclaimer in the documentation 
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "uefi_wrapper.h"
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/dl.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/sfs.h>
#include <grub/efi/graphics_output.h>

#include <stdio.h>

#include "lodepng.h" // PNG encoding library
#include "AppleEventMin.h" // Mac-specific keyboard input

GRUB_MOD_LICENSE ("GPLv3+");
GRUB_MOD_DUAL_LICENSE("BSD 2-Clause");

static EFI_GUID mAppleEventProtocolGuid = APPLE_EVENT_PROTOCOL_GUID;
static EFI_GUID gEfiSimpleFileSystemProtocolGuid =
                    GRUB_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID gEfiGraphicsOutputProtocolGuid = GRUB_EFI_GOP_GUID;
static EFI_GUID gEfiSimpleTextInputExProtocolGuid =
                    GRUB_EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;

static grub_efi_boot_services_t *b;
static grub_efi_runtime_services_t *r;

static CHAR16 utf16_fat_file_name[13];

#define CRS_LOG(fmt, ...) grub_printf ("crscreenshot: " fmt "\n", ##__VA_ARGS__)

static void
refresh_services (void)
{
  if (grub_efi_system_table)
    {
      b = grub_efi_system_table->boot_services;
      r = grub_efi_system_table->runtime_services;
    }
}

typedef EFI_STATUS (EFIAPI *crs_open_volume_t) (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *this,
                                                 EFI_FILE_PROTOCOL **root);
typedef EFI_STATUS (EFIAPI *crs_file_open_t) (EFI_FILE_PROTOCOL *this,
                                              EFI_FILE_PROTOCOL **new_handle,
                                              CHAR16 *file_name,
                                              UINT64 open_mode,
                                              UINT64 attributes);
typedef EFI_STATUS (EFIAPI *crs_file_write_t) (EFI_FILE_PROTOCOL *this,
                                               UINTN *buffer_size,
                                               VOID *buffer);
typedef EFI_STATUS (EFIAPI *crs_file_close_t) (EFI_FILE_PROTOCOL *this);
typedef EFI_STATUS (EFIAPI *crs_file_delete_t) (EFI_FILE_PROTOCOL *this);

static void
utf8_to_utf16 (const char *str)
{
  int i;
  for (i = 0; i < 12; i++)
  {
    utf16_fat_file_name[i] = str[i];
    if (str[i] == '\0')
      break;
  }
  utf16_fat_file_name[12] = 0;
}

static EFI_STATUS
FindWritableFs (EFI_FILE_PROTOCOL **WritableFs)
{
    EFI_HANDLE *HandleBuffer = NULL;
    UINTN      HandleCount;
    UINTN      i;

    refresh_services ();
    if (!b)
      {
        CRS_LOG ("FindWritableFs: boot services unavailable");
        return GRUB_EFI_UNSUPPORTED;
      }
    CRS_LOG ("FindWritableFs: begin");
    CRS_LOG ("FindWritableFs: b=0x%lx locate_handle_buffer=0x%lx",
             (unsigned long) (grub_addr_t) b,
             (unsigned long) (grub_addr_t) b->locate_handle_buffer);
    // Locate all the simple file system devices in the system
    CRS_LOG ("FindWritableFs: locate_handle_buffer(SimpleFileSystem)");
    EFI_STATUS Status = efi_call_5 (b->locate_handle_buffer,
                                    GRUB_EFI_BY_PROTOCOL,
                                    &gEfiSimpleFileSystemProtocolGuid, NULL,
                                    &HandleCount, &HandleBuffer);
    CRS_LOG ("FindWritableFs: locate_handle_buffer status=0x%lx handles=%lu",
             (unsigned long) Status, (unsigned long) HandleCount);
    if (!EFI_ERROR (Status)) {
        EFI_FILE_PROTOCOL *Fs = NULL;
        // For each located volume
        for (i = 0; i < HandleCount; i++) {
            EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SimpleFs = NULL;
            EFI_FILE_PROTOCOL *File = NULL;

            // Get protocol pointer for current volume
            Status = efi_call_3 (b->handle_protocol,
                                 HandleBuffer[i],
                                 &gEfiSimpleFileSystemProtocolGuid,
                                 (VOID **) &SimpleFs);
            if (EFI_ERROR (Status)) {
                grub_dprintf ("crscreenshot",
                    "FindWritableFs: gBS->HandleProtocol returned err\n");
                continue;
            }

            // Open the volume
            CRS_LOG ("FindWritableFs: handle[%lu] open_volume ptr=0x%lx",
                     (unsigned long) i,
                     (unsigned long) (grub_addr_t) SimpleFs->open_volume);
            Status = ((crs_open_volume_t) SimpleFs->open_volume) (SimpleFs, &Fs);
            if (EFI_ERROR (Status)) {
                grub_dprintf ("crscreenshot",
                          "FindWritableFs: SimpleFs->OpenVolume returned err\n");
                continue;
            }

            // Try opening a file for writing
            utf8_to_utf16 ("crsdtest.fil");
            Status = ((crs_file_open_t) Fs->file_open)
              (Fs, &File, utf16_fat_file_name,
               GRUB_EFI_FILE_MODE_CREATE |
               GRUB_EFI_FILE_MODE_READ |
               GRUB_EFI_FILE_MODE_WRITE, 0);
            if (EFI_ERROR (Status)) {
                grub_dprintf ("crscreenshot",
                              "FindWritableFs: Fs->Open returned err\n");
                continue;
            }

            // Writable FS found
            ((crs_file_delete_t) Fs->file_delete) (File);
            *WritableFs = Fs;
            Status = GRUB_EFI_SUCCESS;
            break;
        }
    }

    // Free memory
    if (HandleBuffer) {
        efi_call_1 (b->free_pool, HandleBuffer);
    }

    return Status;
}

static EFI_STATUS
ShowStatus (UINT8 Red, UINT8 Green, UINT8 Blue)
{
    // Determines the size of status square
    #define STATUS_SQUARE_SIDE 5

    UINTN        HandleCount;
    EFI_HANDLE   *HandleBuffer = NULL;
    EFI_GRAPHICS_OUTPUT_PROTOCOL  *GraphicsOutput = NULL;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Square[STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE];
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Backup[STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE];
    UINTN i;

    // Locate all instances of GOP
    EFI_STATUS Status = efi_call_5 (b->locate_handle_buffer,
                                    GRUB_EFI_BY_PROTOCOL,
                                    &gEfiGraphicsOutputProtocolGuid, NULL,
                                    &HandleCount, &HandleBuffer);
    if (EFI_ERROR (Status)) {
        grub_dprintf ("crscreenshot",
                      "ShowStatus: Graphics output protocol not found\n");
        return GRUB_EFI_UNSUPPORTED;
    }

    // Set square color
    for (i = 0 ; i < STATUS_SQUARE_SIDE * STATUS_SQUARE_SIDE; i++) {
        Square[i].blue = Blue;
        Square[i].green = Green;
        Square[i].red = Red;
        Square[i].reserved = 0x00;
    }

    // For each GOP instance
    for (i = 0; i < HandleCount; i ++) {
        // Handle protocol
        Status = efi_call_3 (b->handle_protocol, HandleBuffer[i],
                             &gEfiGraphicsOutputProtocolGuid,
                             (VOID **) &GraphicsOutput);
        if (EFI_ERROR (Status)) {
            grub_dprintf ("crscreenshot",
                          "ShowStatus: gBS->HandleProtocol returned err\n");
            continue;
        }

        // Backup current image
        efi_call_10 (GraphicsOutput->blt, GraphicsOutput, Backup,
                     GRUB_EFI_BLT_VIDEO_TO_BLT_BUFFER, 0, 0, 0, 0,
                     STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);

        // Draw the status square
        efi_call_10 (GraphicsOutput->blt, GraphicsOutput, Square,
                     GRUB_EFI_BLT_BUFFER_TO_VIDEO, 0, 0, 0, 0,
                     STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);

        // Wait 500ms
        efi_call_1 (b->stall, 500*1000);

        // Restore the backup
        efi_call_10 (GraphicsOutput->blt, GraphicsOutput, Backup,
                     GRUB_EFI_BLT_BUFFER_TO_VIDEO, 0, 0, 0, 0,
                     STATUS_SQUARE_SIDE, STATUS_SQUARE_SIDE, 0);
    }

    return GRUB_EFI_SUCCESS;
}

static void
put_u16le (UINT8 *buf, UINTN off, UINT16 val)
{
  buf[off] = (UINT8) (val & 0xFF);
  buf[off + 1] = (UINT8) ((val >> 8) & 0xFF);
}

static void
put_u32le (UINT8 *buf, UINTN off, UINT32 val)
{
  buf[off] = (UINT8) (val & 0xFF);
  buf[off + 1] = (UINT8) ((val >> 8) & 0xFF);
  buf[off + 2] = (UINT8) ((val >> 16) & 0xFF);
  buf[off + 3] = (UINT8) ((val >> 24) & 0xFF);
}

static EFI_STATUS
WriteBmpFile (EFI_FILE_PROTOCOL *File, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Image,
              UINT32 ScreenWidth, UINT32 ScreenHeight)
{
  UINTN RowSize = (((UINTN) ScreenWidth * 3) + 3) & ~((UINTN) 3);
  UINTN DataSize = RowSize * (UINTN) ScreenHeight;
  UINTN FileSize = 54 + DataSize;
  UINT8 Header[54];
  UINT8 *RowBuffer = NULL;
  UINTN x, y, WriteSize;
  EFI_STATUS Status;
  CRS_LOG ("WriteBmpFile: begin %ux%u", ScreenWidth, ScreenHeight);

  if (DataSize > 0xFFFFFFFFU || FileSize > 0xFFFFFFFFU)
    return GRUB_EFI_UNSUPPORTED;

  grub_memset (Header, 0, sizeof (Header));
  Header[0] = 'B';
  Header[1] = 'M';
  put_u32le (Header, 2, (UINT32) FileSize);
  put_u32le (Header, 10, 54);
  put_u32le (Header, 14, 40);
  put_u32le (Header, 18, ScreenWidth);
  put_u32le (Header, 22, ScreenHeight);
  put_u16le (Header, 26, 1);
  put_u16le (Header, 28, 24);
  put_u32le (Header, 34, (UINT32) DataSize);

  WriteSize = sizeof (Header);
  Status = ((crs_file_write_t) File->file_write) (File, &WriteSize, Header);
  if (EFI_ERROR (Status))
    return Status;

  Status = efi_call_3 (b->allocate_pool, GRUB_EFI_BOOT_SERVICES_DATA,
                       RowSize, (VOID **) &RowBuffer);
  if (EFI_ERROR (Status))
    return Status;

  Status = GRUB_EFI_SUCCESS;
  for (y = ScreenHeight; y > 0; y--)
    {
      grub_memset (RowBuffer, 0, RowSize);
      for (x = 0; x < ScreenWidth; x++)
        {
          EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Pixel;
          UINTN base = x * 3;
          Pixel = &Image[((UINTN) (y - 1) * ScreenWidth) + x];
          RowBuffer[base] = Pixel->blue;
          RowBuffer[base + 1] = Pixel->green;
          RowBuffer[base + 2] = Pixel->red;
        }

      WriteSize = RowSize;
      Status = ((crs_file_write_t) File->file_write) (File, &WriteSize, RowBuffer);
      if (EFI_ERROR (Status))
        break;
    }

  efi_call_1 (b->free_pool, RowBuffer);
  CRS_LOG ("WriteBmpFile: done status=0x%lx", (unsigned long) Status);
  return Status;
}


static EFI_STATUS EFIAPI
TakeScreenshot (EFI_KEY_DATA *KeyData)
{
    EFI_FILE_PROTOCOL *Fs = NULL;
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_GRAPHICS_OUTPUT_PROTOCOL  *GraphicsOutput = NULL;
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Image = NULL;
    UINTN      ImageSize;         // Size in pixels
    UINT8      *PngFile = NULL;
    UINTN      PngFileSize = 0;
    EFI_STATUS Status;
    UINTN      HandleCount;
    EFI_HANDLE *HandleBuffer = NULL;
    UINT32     ScreenWidth;
    UINT32     ScreenHeight;
    EFI_TIME   Time;
    UINTN      i, j;
    (VOID)KeyData;
    refresh_services ();
    CRS_LOG ("TakeScreenshot: b=0x%lx r=0x%lx",
             (unsigned long) (grub_addr_t) b,
             (unsigned long) (grub_addr_t) r);
    CRS_LOG ("hotkey captured, taking screenshot");

    // Find writable FS
    CRS_LOG ("TakeScreenshot: calling FindWritableFs");
    Status = FindWritableFs(&Fs);
    if (EFI_ERROR (Status)) {
        grub_dprintf ("crscreenshot", "TakeScreenshot: Can't find writable FS\n");
        CRS_LOG ("failed to find writable FS");
        ShowStatus(0xFF, 0xFF, 0x00); //Yellow
        return GRUB_EFI_SUCCESS;
    }

    // Locate all instances of GOP
    CRS_LOG ("TakeScreenshot: locate_handle_buffer(GOP)");
    Status = efi_call_5 (b->locate_handle_buffer, GRUB_EFI_BY_PROTOCOL,
                         &gEfiGraphicsOutputProtocolGuid,
                         NULL, &HandleCount, &HandleBuffer);
    if (EFI_ERROR (Status)) {
        grub_dprintf ("crscreenshot",
                      "ShowStatus: Graphics output protocol not found\n");
        CRS_LOG ("failed to locate GOP handles");
        return GRUB_EFI_SUCCESS;
    }

    // For each GOP instance
    for (i = 0; i < HandleCount; i++) {
        do { // Break from do used instead of "goto error"
            // Handle protocol
            Status = efi_call_3 (b->handle_protocol, HandleBuffer[i],
                                 &gEfiGraphicsOutputProtocolGuid,
                                 (VOID **) &GraphicsOutput);
            if (EFI_ERROR (Status)) {
                grub_dprintf ("crscreenshot",
                              "ShowStatus: gBS->HandleProtocol returned err\n");
                break;
            }

            // Set screen width, height and image size in pixels
            ScreenWidth  = GraphicsOutput->mode->info->width;
            ScreenHeight = GraphicsOutput->mode->info->height;
            ImageSize = ScreenWidth * ScreenHeight;

            // Get current time
            Status = efi_call_2 (r->get_time, &Time, NULL);
            if (!EFI_ERROR(Status)) {
                // Set file name to current day and time
              char name[13];
              grub_snprintf (name, 13, "%02d%02d%02d%02d.png",
                             Time.day, Time.hour, Time.minute, Time.second);
              utf8_to_utf16 (name);
            }
            else {
                // Set file name to scrnshot.png
              utf8_to_utf16 ("scrnshot.png");
            }
            CRS_LOG ("capturing GOP[%lu], %ux%u", (unsigned long) i,
                     ScreenWidth, ScreenHeight);

            // Allocate memory for screenshot
            Status = efi_call_3 (b->allocate_pool,
                                 GRUB_EFI_BOOT_SERVICES_DATA,
                                 ImageSize * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL),
                                 (VOID **)&Image);
            if (EFI_ERROR(Status)) {
                grub_dprintf ("crscreenshot",
                              "TakeScreenshot: gBS->AllocatePool returned err\n");
                CRS_LOG ("failed to allocate image buffer");
                break;
            }

            // Take screenshot
            Status = efi_call_10 (GraphicsOutput->blt, GraphicsOutput, Image,
                                  GRUB_EFI_BLT_VIDEO_TO_BLT_BUFFER, 0, 0, 0, 0,
                                  ScreenWidth, ScreenHeight, 0);
            if (EFI_ERROR(Status)) {
                grub_dprintf ("crscreenshot",
                          "TakeScreenshot: GraphicsOutput->Blt returned err\n");
                CRS_LOG ("GOP blit failed");
                break;
            }

            // Check for pitch black image (it means we are using a wrong GOP)
            for (j = 0; j < ImageSize; j++) {
                if (Image[j].red != 0x00 ||
                    Image[j].green != 0x00 ||
                    Image[j].blue != 0x00)
                    break;
            }
            if (j == ImageSize) {
                grub_dprintf ("crscreenshot", "TakeScreenshot: GraphicsOutput->Blt returned pitch black image, skipped\n");
                CRS_LOG ("gop[%lu] produced a black frame, skipped", (unsigned long) i);
                ShowStatus(0x00, 0x00, 0xFF); //Blue
                break;
            }

            // Open or create output file
            Status = ((crs_file_open_t) Fs->file_open)
              (Fs, &File, utf16_fat_file_name,
               GRUB_EFI_FILE_MODE_CREATE |
               GRUB_EFI_FILE_MODE_READ |
               GRUB_EFI_FILE_MODE_WRITE, 0);
            if (EFI_ERROR (Status)) {
                grub_dprintf ("crscreenshot",
                      "TakeScreenshot: Fs->Open returned err\n");
                CRS_LOG ("failed to open output file");
                break;
            }

            // Convert BGR to RGBA with Alpha set to 0xFF for lodepng.
            for (j = 0; j < ImageSize; j++) {
                UINT8 Temp = Image[j].blue;
                Image[j].blue = Image[j].red;
                Image[j].red = Temp;
                Image[j].reserved = 0xFF;
            }

            // Primary path: encode and write PNG.
            j = lodepng_encode32 (&PngFile, &PngFileSize,
                                  (const UINT8 *) Image,
                                  ScreenWidth, ScreenHeight);
            if (!j) {
                Status = ((crs_file_write_t) File->file_write) (File, &PngFileSize, PngFile);
                if (EFI_ERROR(Status)) {
                    grub_dprintf ("crscreenshot",
                                  "TakeScreenshot: File->Write returned err\n");
                    CRS_LOG ("file write failed");
                    break;
                }
            } else {
                CRS_LOG ("lodepng_encode32 failed (%lu), fallback to BMP", (unsigned long) j);
                Status = WriteBmpFile (File, Image, ScreenWidth, ScreenHeight);
                if (EFI_ERROR(Status)) {
                    grub_dprintf ("crscreenshot",
                                  "TakeScreenshot: File->Write returned err\n");
                    CRS_LOG ("bmp fallback write failed");
                    break;
                }
            }

            ((crs_file_close_t) File->file_close) (File);
            File = NULL;

            // Show success
            CRS_LOG ("screenshot saved");
            ShowStatus(0x00, 0xFF, 0x00); //Green
        } while(0);

        // Free memory
        if (Image)
            efi_call_1 (b->free_pool, Image);
        if (PngFile)
            grub_free (PngFile);
        if (File)
            ((crs_file_close_t) File->file_close) (File);
        Image = NULL;
        PngFile = NULL;
        PngFileSize = 0;
        File = NULL;
    }

    if (HandleBuffer)
        efi_call_1 (b->free_pool, HandleBuffer);

    // Show error
    if (EFI_ERROR(Status))
        ShowStatus(0xFF, 0x00, 0x00); //Red

    return GRUB_EFI_SUCCESS;
}

static VOID EFIAPI
AppleEventKeyHandler (APPLE_EVENT_INFORMATION *Information, VOID *NotifyContext)
{
    // Mark the context argument as used
    (VOID) NotifyContext;

    // Ignore invalid information if it happened to arrive
    if (Information == NULL || (Information->EventType & APPLE_EVENT_TYPE_KEY_UP) == 0) {
        return;
    }

    // Apple calls ALT key by the name of OPTION key
    if (Information->KeyData->InputKey.scan_code == SCAN_F12 &&
        Information->Modifiers == (APPLE_MODIFIER_LEFT_CONTROL|
                                   APPLE_MODIFIER_LEFT_OPTION)) {
        // Take a screenshot
        CRS_LOG ("AppleEvent hotkey matched");
        TakeScreenshot (NULL);
    }
}

static EFI_STATUS
CrScreenshotDxeEntry (VOID)
{
    EFI_STATUS                        Status;
    UINTN                             HandleCount = 0;
    EFI_HANDLE                        *HandleBuffer = NULL;
    UINTN                             Index;
    EFI_KEY_DATA                      SimpleTextInExKeyStroke;
    EFI_HANDLE                        SimpleTextInExHandle;
    EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *SimpleTextInEx;
    APPLE_EVENT_HANDLE                AppleEventHandle;
    APPLE_EVENT_PROTOCOL              *AppleEvent;
    BOOLEAN                           Installed = FALSE;

    // Set keystroke to be LCtrl+LAlt+F12
    SimpleTextInExKeyStroke.key.scan_code = SCAN_F12;
    SimpleTextInExKeyStroke.key.unicode_char = 0;
    SimpleTextInExKeyStroke.key_state.key_shift_state =
        EFI_SHIFT_STATE_VALID | EFI_LEFT_CONTROL_PRESSED | EFI_LEFT_ALT_PRESSED;
    SimpleTextInExKeyStroke.key_state.key_toggle_state = 0;

    // Locate compatible protocols, firstly try SimpleTextInEx, otherwise use AppleEvent
    Status = efi_call_5 (b->locate_handle_buffer, GRUB_EFI_BY_PROTOCOL,
                         &gEfiSimpleTextInputExProtocolGuid,
                         NULL, &HandleCount, &HandleBuffer);
    if (!EFI_ERROR (Status)) {
        // For each instance
        for (Index = 0; Index < HandleCount; Index++) {
            Status = efi_call_3 (b->handle_protocol, HandleBuffer[Index],
                                 &gEfiSimpleTextInputExProtocolGuid,
                                 (VOID **) &SimpleTextInEx);

            // Get protocol handle
            if (EFI_ERROR (Status)) {
               grub_dprintf ("crscreenshot", "CrScreenshotDxeEntry: gBS->HandleProtocol SimpleTextInputEx returned err\n");
               continue;
            }

            // Register key notification function
            Status = efi_call_4 (SimpleTextInEx->register_key_notify,
                                 SimpleTextInEx,
                                 &SimpleTextInExKeyStroke,
                                 TakeScreenshot,
                                 &SimpleTextInExHandle);
            if (!EFI_ERROR (Status)) {
                Installed = TRUE;
            } else {
                grub_dprintf ("crscreenshot",
        "CrScreenshotDxeEntry: SimpleTextInEx->RegisterKeyNotify returned err\n");
            }
        }
    } else {
        grub_dprintf ("crscreenshot", "CrScreenshotDxeEntry: gBS->LocateHandleBuffer SimpleTextInputEx returned err\n");
        HandleBuffer = NULL;
        Status = efi_call_5 (b->locate_handle_buffer, GRUB_EFI_BY_PROTOCOL,
                             &mAppleEventProtocolGuid,
                             NULL, &HandleCount, &HandleBuffer);
        if (EFI_ERROR (Status)) {
            grub_dprintf ("crscreenshot", "CrScreenshotDxeEntry: gBS->LocateHandleBuffer AppleEvent returned err\n");
            return GRUB_EFI_UNSUPPORTED;
        }

        // Traverse AppleEvent handles similarly to SimpleTextInputEx
        for (Index = 0; Index < HandleCount; Index++) {
            Status = efi_call_3 (b->handle_protocol, HandleBuffer[Index],
                                 &mAppleEventProtocolGuid, (VOID **) &AppleEvent);

            // Get protocol handle
            if (EFI_ERROR (Status)) {
               continue;
            }

            // Check protocol interface compatibility
            if (AppleEvent->Revision < APPLE_EVENT_PROTOCOL_REVISION) {
                continue;
            }

            // Register key handler, which will later determine LCtrl+LAlt+F12 combination
            Status = efi_call_4 (AppleEvent->RegisterHandler,
                                 APPLE_EVENT_TYPE_KEY_UP,
                                 AppleEventKeyHandler,
                                 &AppleEventHandle, NULL);
            if (!EFI_ERROR (Status)) {
                Installed = TRUE;
            } else {
                grub_dprintf ("crscreenshot",
                              "CrScreenshotDxeEntry: AppleEvent->RegisterHandler returned err\n");
            }
        }
    }

    // Free memory used for handle buffer
    if (HandleBuffer) {
        efi_call_1(b->free_pool, HandleBuffer);
    }

    // Show success only when we found at least one working implementation
    if (Installed) {
        ShowStatus(0xFF, 0xFF, 0xFF); //White
    }

    return GRUB_EFI_SUCCESS;
}

GRUB_MOD_INIT(crscreenshot)
{
  b = grub_efi_system_table->boot_services;
  r = grub_efi_system_table->runtime_services;
  CrScreenshotDxeEntry ();
}

GRUB_MOD_FINI(crscreenshot)
{
}
