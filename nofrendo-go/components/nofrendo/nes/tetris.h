#ifndef _TETRIS_H_
#define _TETRIS_H_

#define MAX_REPLAY_LENGTH 1024


#define TETRIS_REPLAY_SCHEME "https" // should be https
#define TETRIS_REPLAY_PORT ""  // could be :8443
#define TETRIS_REPLAY_HOST "trochr.github.io" // should be a public host, ie. github 
#define TETRIS_REPLAY_PATH "ntr/i"
// Final URL : https://trochr.github.io/ntr/i

#define IMG_H 32
#define IMG_W 32

typedef struct tetris_drop_s 
{
    unsigned char row;
    unsigned char col;
    unsigned char shape;
} tetris_drop_t;


void compactSequence(int *bitsArray, unsigned char start_level, unsigned int pieceCount, tetris_drop_t *sequence);


void  SetBit(int bitsArray[],  int k );
void  ClearBit(int bitsArray[],  int k );                
int TestBit(int bitsArray[],  int k );
void showArray(int *bitsArray, int arrayLength);
void clearArray(int *bitsArray, int arrayLength);
unsigned char reverse(unsigned char b);
void qr_draw_img(pixel_t *fb, const uint8_t *img, uint16_t x, uint16_t y, int qrWidth);
void qr_draw_rectangle(pixel_t *fb, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void qr_draw_clear_rectangle(pixel_t *fb, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void qr_ingame_overlay(uint8_t *qr_img, uint16_t x, uint16_t y, uint16_t w);

#endif