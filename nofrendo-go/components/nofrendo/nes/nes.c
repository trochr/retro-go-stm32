/*
** Nofrendo (c) 1998-2000 Matthew Conte (matt@conte.com)
**
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of version 2 of the GNU Library General
** Public License as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.  To obtain a
** copy of the GNU Library General Public License, write to the Free
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
**
**
** nes.c
**
** NES hardware related routines
** $Id: nes.c,v 1.2 2001/04/27 14:37:11 neil Exp $
*/

#include <string.h>
#include <nofrendo.h>
#include <nes_input.h>
#include <osd.h>
#include <nes.h>

#include "gw_lcd.h"

#include "tetris.h"
#include "base64.h"
#include "qrcodegen.h"

#define NES_OVERDRAW (8)

static uint8_t bitmap_data[2][sizeof(bitmap_t) + (sizeof(uint8 *) * NES_SCREEN_HEIGHT)];
static bitmap_t *framebuffers[2];
static nes_t nes;


nes_t *nes_getptr(void)
{
   return &nes;
}

/* Emulate one frame */
INLINE void renderframe()
{
   int elapsed_cycles;
   mapintf_t *mapintf = nes.mmc->intf;

   while (nes.scanline < nes.scanlines_per_frame)
   {
      nes.cycles += nes.cycles_per_line;

      ppu_scanline(nes.vidbuf, nes.scanline, nes.drawframe);

      if (nes.scanline == 241)
      {
         /* 7-9 cycle delay between when VINT flag goes up and NMI is taken */
         elapsed_cycles = nes6502_execute(7);
         nes.cycles -= elapsed_cycles;

         if (nes.ppu->ctrl0 & PPU_CTRL0F_NMI)
            nes6502_nmi();

         if (mapintf->vblank)
            mapintf->vblank();
      }

      if (mapintf->hblank)
         mapintf->hblank(nes.scanline);

      elapsed_cycles = nes6502_execute(nes.cycles);
      apu_fc_advance(elapsed_cycles);
      nes.cycles -= elapsed_cycles;

      ppu_endscanline();
      nes.scanline++;
   }

   nes.scanline = 0;
}

/* main emulation loop */
void nes_emulate(void)
{
   bool first_iteration = true;

   const unsigned char *pc = (const unsigned char *)nes.mem;
   unsigned char prev_row, prev_col, prev_shape,
       start_level, prev_game_mode;
   tetris_drop_t sequence[MAX_REPLAY_LENGTH];
   unsigned int pieceCount = 0;
   prev_row = 0;
   bool QR_Display = false;
   uint8_t QRbytes[qrcodegen_BUFFER_LEN_MAX];
   uint8_t qr_size, qr_extra_bits;
   int top_margin = 30;
   int qr_posx = 280;

   // unsigned char * buffer;
   uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];

   // unsigned char version;
   start_level = 0;
   prev_col = 0;
   prev_shape = 0;
   prev_game_mode = 03; // 04 is play mode, 03 is menu mode
   pixel_t *fb;
   fb = lcd_get_active_buffer();

   // Discard the garbage frames
   renderframe();
   renderframe();

   while (false == nes.poweroff)
   {
      osd_getinput();
      renderframe();

      if (nes.drawframe)
      {
         osd_blitscreen(nes.vidbuf);
         // nes.vidbuf = (nes.vidbuf == framebuffers[1]) ? framebuffers[0] : framebuffers[1];
      }

      apu_emulate();
      if (nes.ntr)
      {
         if (pc[192] == 0x04 && prev_game_mode == 0x03)
         { // the game started
            QR_Display = false;
            fb = lcd_get_active_buffer();
            qr_draw_clear_rectangle(fb, 0, 0, 320, 280); // clearing some relics of the past QR in the bg
            fb = lcd_get_inactive_buffer();
            qr_draw_clear_rectangle(fb, 0, 0, 320, 280); // same on the inactive buffer

            start_level = pc[103];
            pieceCount = 0;
         }

         if (pc[65] < prev_row)
         { // a piece landed, recording it
            if (pieceCount < MAX_REPLAY_LENGTH)
            { // beyond that the QR generation will become tricky
               sequence[pieceCount].col = prev_col;
               sequence[pieceCount].row = prev_row;
               sequence[pieceCount].shape = prev_shape;
               // printf("We landed a shape:%d\n",prev_shape);
               pieceCount += 1;
            }
         }

         if (pc[192] == 0x03 && prev_game_mode == 0x04)
         { // the game ended

            // Simulating a long game
            bool simu = false;

            if (simu)
            {
               pieceCount = 0;
               start_level = 19;
               int game_length = 350;

               for (int i = 0; i < game_length; i++)
               {
                  int n = rand() % 10; // Rand between 0 and 9
                  sequence[pieceCount].col = n;
                  n = rand() % 10; // Rand between 0 and 9
                  sequence[pieceCount].row = n + 5;
                  n = rand() % 10; // Rand between 0 and 9
                  sequence[pieceCount].shape = n;
                  pieceCount += 1;
               }
            }
            // End of simulation code

            /*
            Todo
            ----

            - Remove flickering

            */

            int arrayLength = 1 + (pieceCount * 14) / 32;

            int bitsArray[arrayLength]; // bitsArray[i] = 0 means false, bitsArray[i] = 1 means true

            compactSequence(bitsArray, start_level, pieceCount, sequence);

            size_t output_size;
            char *b64out;

            b64out = base64_encode(bitsArray, arrayLength * 4, &output_size);

            b64out[output_size - 1] = 0x0;

            char *scheme = TETRIS_REPLAY_SCHEME;
            char *port = TETRIS_REPLAY_PORT;
            char *host = TETRIS_REPLAY_HOST;
            char *path = TETRIS_REPLAY_PATH;
            int urllen = strlen(scheme) + strlen(host) + strlen(port) + strlen(path) + 15 + output_size;
            char url[urllen];
            sprintf(url, "%s://%s%s/%s?%d,%s", scheme, host, port, path, start_level, b64out);

            enum qrcodegen_Ecc errCorLvl = qrcodegen_Ecc_LOW; // Error correction level

            // Make and print the QR Code symbol
            uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];
            bool ok = qrcodegen_encodeText(url, tempBuffer, qrcode, errCorLvl,
                                           qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true);

            if (ok)
            {
               qr_size = qrcodegen_getSize(qrcode);
               int size_padded = ((qr_size) / 8) * 8 + 8;
               qr_extra_bits = size_padded - qr_size;

               int QRbytesCount = size_padded * size_padded / 8;

               int currentBit = 0;
               int currentByte = 0;

               for (int i = 0; i < QRbytesCount; i++)
               {
                  QRbytes[i] = 0x00;
               }
               for (int y = 0; y < qr_size + qr_extra_bits; y++)
               {
                  for (int x = 0; x < qr_size + qr_extra_bits; x++)
                  {
                     if (!qrcodegen_getModule(qrcode, x, y) && x < qr_size && y < qr_size)
                     {
                        QRbytes[currentByte] |= 1;
                     }
                     currentBit++;
                     if (currentBit == 8)
                     {
                        currentByte++;
                        currentBit = 0;
                     }
                     else
                     {
                        QRbytes[currentByte] = QRbytes[currentByte] << 1;
                     }
                  }
               }
               QR_Display = true;
            }
         }

         prev_col = pc[64];
         prev_row = pc[65];
         if (pc[66] != 19)
         { // ignoring the non-shape value 19 (line clears)
            prev_shape = pc[66];
         }
         prev_game_mode = pc[192];

         /* QR Display Code */

         if (QR_Display)
         {
            int final_width = qr_size + qr_extra_bits;

            fb = lcd_get_inactive_buffer();
            qr_draw_clear_rectangle(fb, qr_posx - final_width, top_margin, qr_posx, qr_size + top_margin);
            qr_draw_img(fb, QRbytes, qr_posx - final_width, top_margin, final_width);
            fb = lcd_get_active_buffer();
            qr_draw_clear_rectangle(fb, qr_posx - final_width, top_margin, qr_posx, qr_size + top_margin);
            qr_draw_img(fb, QRbytes, qr_posx - final_width, top_margin, final_width);
         }
      }

      // osd_submit_audio(apu.buffer, apu.samples_per_frame);

      osd_vsync();

      // Some games require one emulation iteration to be performed before being able to load properly
      if (first_iteration) {
         first_iteration = false;
         osd_loadstate();
      }
   }
}

void nes_poweroff(void)
{
   nes.poweroff = true;
}

void nes_togglepause(void)
{
   nes.pause ^= true;
}

void nes_setcompathacks(void)
{
   // Hack to fix many MMC3 games with status bar vertical aligment issues
   // The issue is that the CPU and PPU aren't running in sync
   // if (nes.region == NES_NTSC && nes.rominfo->mapper_number == 4)
   if (nes.rominfo->checksum == 0xD8578BFD || // Zen Intergalactic
       nes.rominfo->checksum == 0x2E6301ED || // Super Mario Bros 3
       nes.rominfo->checksum == 0x5ED6F221 || // Kirby's Adventure
       nes.rominfo->checksum == 0xD273B409)   // Power Blade 2
   {
      nes.cycles_per_line += 2.5;
      MESSAGE_INFO("NES: Enabled MMC3 Timing Hack\n");
   }

   if (nes.rominfo->checksum == 0x1394F57E || // Tetris USA
       nes.rominfo->checksum == 0xAAEE8538 || // Tetris DAS Trainer
       nes.rominfo->checksum == 0x57F946FE || // Tetris DAS Trainer PB 488k (another?)
       nes.rominfo->checksum == 4919 ||       // Tetris DAS Trainer PB 488k (another?)
       nes.rominfo->checksum == 0x2BBC36B2)   // Tetris DAS Trainer PB 488k
   {
      nes.ntr = true;
      MESSAGE_INFO("NES: Enabled Nes tetris replay\n");
   }
   else
   {
      MESSAGE_INFO("NES: ROM not approved for Nes tetris replay \n");
      MESSAGE_INFO("NES: ROM hash: %lu \n", nes.rominfo->checksum);
      exit(0);
   }
}

/* insert a cart into the NES */
int nes_insertcart(const char *filename)
{
   /* rom file */
   nes.rominfo = rom_load(filename);
   if (NULL == nes.rominfo)
      goto _fail;

   /* mapper */
   nes.mmc = mmc_init(nes.rominfo);
   if (NULL == nes.mmc)
      goto _fail;

   nes.mem->mapper = nes.mmc->intf;

   /* if there's VRAM, let the PPU know */
   nes.ppu->vram_present = (NULL != nes.rominfo->vram);

   /* Set NES Tetris replay flag*/
   nes.ntr = false;

   nes_setregion(nes.region);
   nes_setcompathacks();

   nes_reset(HARD_RESET);

   return true;

_fail:
   nes_shutdown();
   return false;
}

/* Reset NES hardware */
void nes_reset(reset_type_t reset_type)
{
   if (nes.rominfo->vram)
   {
      memset(nes.rominfo->vram, 0, 0x2000 * nes.rominfo->vram_banks);
   }

   apu_reset();
   ppu_reset();
   mem_reset();
   mmc_reset();
   nes6502_reset();

   nes.vidbuf = framebuffers[0];
   nes.scanline = 241;
   nes.cycles = 0;

   MESSAGE_INFO("NES: System reset (%s)\n", (SOFT_RESET == reset_type) ? "soft" : "hard");
}

/* Shutdown NES */
void nes_shutdown(void)
{
   mmc_shutdown();
   mem_shutdown();
   ppu_shutdown();
   apu_shutdown();
   nes6502_shutdown();
}

/* Setup region-dependant timings */
void nes_setregion(region_t region)
{
   // https://wiki.nesdev.com/w/index.php/Cycle_reference_chart
   if (region == NES_PAL)
   {
      nes.region = NES_PAL;
      nes.refresh_rate = NES_REFRESH_RATE_PAL;
      nes.scanlines_per_frame = NES_SCANLINES_PAL;
      nes.overscan = 0;
      nes.cycles_per_line = 341.f * 5 / 16;
      MESSAGE_INFO("NES: System region: PAL\n");
   }
   else
   {
      nes.region = NES_NTSC;
      nes.refresh_rate = NES_REFRESH_RATE_NTSC;
      nes.scanlines_per_frame = NES_SCANLINES_NTSC;
      nes.overscan = 8;
      nes.cycles_per_line = 341.f * 4 / 12;
      MESSAGE_INFO("NES: System region: NTSC\n");
   }
}

void bmp_init(bitmap_t *bitmap, int index, int width , int height, int overdraw)
{
   bitmap->data = emulator_framebuffer;
   bitmap->width = NES_SCREEN_WIDTH;
   bitmap->height = NES_SCREEN_HEIGHT;
   bitmap->pitch = NES_SCREEN_WIDTH + (overdraw * 2);

   for (int i = 0; i < height; i++)
      bitmap->line[i] = bitmap->data + (bitmap->pitch * i) + overdraw;
}


/* Initialize NES CPU, hardware, etc. */
int nes_init(region_t region, int sample_rate, bool stereo)
{
   memset(&nes, 0, sizeof(nes_t));

   nes_setregion(region);

   nes.autoframeskip = true;
   nes.poweroff = false;
   nes.pause = false;
   nes.drawframe = true;

   /* Framebuffers */
   framebuffers[0] = (bitmap_t*)bitmap_data[0];
   framebuffers[1] = (bitmap_t*)bitmap_data[1];
   bmp_init(framebuffers[0], 0, NES_SCREEN_WIDTH, NES_SCREEN_HEIGHT, NES_OVERDRAW);
   bmp_init(framebuffers[1], 1, NES_SCREEN_WIDTH, NES_SCREEN_HEIGHT, NES_OVERDRAW);

   /* memory */
   nes.mem = mem_create();
   if (NULL == nes.mem)
      goto _fail;

   /* cpu */
   nes.cpu = nes6502_init(nes.mem);
   if (NULL == nes.cpu)
      goto _fail;

   /* apu */
   nes.apu = apu_init(region, sample_rate, stereo);
   if (NULL == nes.apu)
      goto _fail;

   /* ppu */
   nes.ppu = ppu_init(region);
   if (NULL == nes.ppu)
      goto _fail;

   return true;

_fail:
   nes_shutdown();
   return false;
}
