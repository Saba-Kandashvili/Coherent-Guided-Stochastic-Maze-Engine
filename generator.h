#ifndef __GENERATOR__
#define __GENERATOR__
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

void collapseTile(uint16_t *tile, float *rates, uint32_t *rng);
uint16_t ***generateGrid(uint32_t width, uint32_t length, uint32_t height, uint32_t seed, uint32_t fulness);

void freeGrid(uint16_t ***grid, uint32_t width, uint32_t length, uint32_t height);


typedef struct layerGenerationArgs
{
    uint16_t **gridLayer;
    uint32_t width;
    uint32_t length;
    int32_t startX;
    int32_t startY;
    int32_t endX;
    int32_t endY;
    uint32_t seed;
    uint8_t fulness;
    uint8_t layerIndex;
} layerGenerationArgs;





#endif