#include "gw_lcd.h"
#include "tetris.h"
#include <string.h>
#include <stdio.h>

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)       \
    (byte & 0x80 ? '1' : '0'),     \
        (byte & 0x40 ? '1' : '0'), \
        (byte & 0x20 ? '1' : '0'), \
        (byte & 0x10 ? '1' : '0'), \
        (byte & 0x08 ? '1' : '0'), \
        (byte & 0x04 ? '1' : '0'), \
        (byte & 0x02 ? '1' : '0'), \
        (byte & 0x01 ? '1' : '0')

/* TODO
+detect start game
+detect end game
+store the start level
+store the sequence of drops
+convert to 14 bits per drop
+convert to base64
+compute the QR code matrix
+ produce the qr to a bmp file
+ have this thing only enabled for tetris Roms
display the QR code (no filesystem on the G&W)
evaluate reasonable values for MAX_REPLAY_LENGTH

ToFix
+ some strange looking QR are produced
+ correct QR don't produce the real game in the HTML viewer
+Hack to ROM with PB
@2D77 : 14 2B 2B 2B 2B 2B // T (instead of HOWARD)
@2DA7 : 48 87 80  // High score (Personal Best)
@2DBF : 13 // // Level 19 in hex



Made with the precious knowledge from https://meatfighter.com/nintendotetrisai/
*/

// hexDump("cpu",nes.mem,128,16);

unsigned char reverse(unsigned char b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

void SetBit(int bitsArray[], int k)
{
    bitsArray[k / 32] |= 1 << (k % 32); // Set the bit at the k-th position in bitsArray[i]
}

void ClearBit(int bitsArray[], int k)
{
    bitsArray[k / 32] &= ~(1 << (k % 32));
}

int TestBit(int bitsArray[], int k)
{
    return ((bitsArray[k / 32] & (1 << (k % 32))) != 0);
}

#define DARKEN_MASK_565 0x7BEF // Mask off the MSb of each color
#define DARKEN_ADD_565 0x2104  // value of 4-red, 8-green, 4-blue to add back in a little gray, especially on black backgrounds
#define OVERLAY_COLOR_565 0xFFFF

static inline void clear_pixel(pixel_t *p)
{
    *p = 0;
}

void qr_draw_img(pixel_t *fb, const uint8_t *img, uint16_t x, uint16_t y, int qrWidth)
{
    uint16_t idx = 0;
    for (uint8_t i = 0; i < qrWidth; i++)
    {
        for (uint8_t j = 0; j < qrWidth; j++)
        {
            if (img[idx / 8] & (1 << (7 - idx % 8)))
            {
                fb[x + j + GW_LCD_WIDTH * (y + i)] = OVERLAY_COLOR_565;
            }
            idx++;
        }
    }
}

void qr_draw_rectangle(pixel_t *fb, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    for (uint16_t i = y1; i < y2; i++)
    {
        for (uint16_t j = x1; j < x2; j++)
        {
            fb[j + GW_LCD_WIDTH * i] = OVERLAY_COLOR_565;
        }
    }
}

void qr_draw_clear_rectangle(pixel_t *fb, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    for (uint16_t i = y1; i < y2; i++)
    {
        for (uint16_t j = x1; j < x2; j++)
        {
            clear_pixel(&fb[j + GW_LCD_WIDTH * i]);
        }
    }
}

void showArray(int *bitsArray, int arrayLength)
{
    for (int i = 0; i < sizeof(int) * 8 * arrayLength; i++)
    {
        if (TestBit(bitsArray, i) == 0)
            printf("0");
        else
        {
            printf("1");
        }
    }
}

void clearArray(int *bitsArray, int arrayLength)
{
    for (int i = 0; i < sizeof(int) * 8 * arrayLength; i++)
    {
        ClearBit(bitsArray, i);
    }
}

void compactSequence(int *bitsArray, unsigned char start_level, unsigned int pieceCount, tetris_drop_t *sequence)
{
    int arrayLength = 1 + (pieceCount * 14) / 32;
    int bitsArrayPos = 0;

    clearArray(bitsArray, arrayLength);

    for (int i = 0; i < pieceCount; i++)
    {
        // printf("%d , col:%d, row:%d, shape:%d\n",i, sequence[i].col,sequence[i].row,sequence[i].shape);
        // first 4 bits are the cols

        //    printf("Binary col : "BYTE_TO_BINARY_PATTERN"\n", BYTE_TO_BINARY(sequence[i].col));
        //    printf("Binary row : "BYTE_TO_BINARY_PATTERN"\n", BYTE_TO_BINARY(sequence[i].row));
        //    printf("Binary shape : "BYTE_TO_BINARY_PATTERN"\n", BYTE_TO_BINARY(sequence[i].shape));

        for (int j = 3; j >= 0; j--)
        {
            if ((sequence[i].col >> j) % 2 == 0)
            {
                ClearBit(bitsArray, bitsArrayPos);
            }
            else
            {
                SetBit(bitsArray, bitsArrayPos);
            }
            bitsArrayPos += 1;
        }

        for (int j = 4; j >= 0; j--)
        {
            if ((sequence[i].row >> j) % 2 == 0)
            {
                ClearBit(bitsArray, bitsArrayPos);
            }
            else
            {
                SetBit(bitsArray, bitsArrayPos);
            }
            bitsArrayPos += 1;
        }

        for (int j = 4; j >= 0; j--)
        {
            if ((sequence[i].shape >> j) % 2 == 0)
            {
                ClearBit(bitsArray, bitsArrayPos);
            }
            else
            {
                SetBit(bitsArray, bitsArrayPos);
            }
            bitsArrayPos += 1;
        }

        // printf("bitsArray current state:\n");
        // showArray(bitsArray,arrayLength);
        // printf("\n");
    }
    // inverting the bits orders, (because we obviously have a wrong order)
    for (int i = 0; i < arrayLength; i++)
    {
        unsigned char bytes[4];
        unsigned long n = bitsArray[i];

        bytes[0] = reverse((n >> 24) & 0xFF);
        bytes[1] = reverse((n >> 16) & 0xFF);
        bytes[2] = reverse((n >> 8) & 0xFF);
        bytes[3] = reverse(n & 0xFF);

        bitsArray[i] = (bytes[0] << 24) + (bytes[1] << 16) + (bytes[2] << 8) + bytes[3];
    }
}
