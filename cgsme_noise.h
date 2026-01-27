#ifndef CGSME_NOISE_H
#define CGSME_NOISE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

typedef struct
{
	uint16_t x;
	uint16_t y;
	float score; // ridged noise value
} PixelData;

/// @brief Compare two PixelData for qsort (descending by score).
/// @param a First pixel.
/// @param b Second pixel.
/// @return Comparison result for qsort.
int comparePixels(const void *a, const void *b);

/// @brief Get value noise at a given coordinate.
/// @param x X coordinate (can be fractional).
/// @param y Y coordinate (can be fractional).
/// @param seed Noise seed.
/// @return Noise value in range [0.0, 1.0].
float getValueNoise(float x, float y, uint32_t seed);

/// @brief Measure the size of a connected region using BFS.
/// @param grid Pointer to the 3D grid.
/// @param visited Visited flag array.
/// @param width Grid width.
/// @param length Grid length.
/// @param startX Starting X coordinate.
/// @param startY Starting Y coordinate.
/// @return Number of cells in the region.
int measureRegionSize(uint16_t ***grid, bool *visited, int width, int length, int startX, int startY);

/// @brief Keep only the largest connected region in the mask.
/// @param grid Pointer to the 3D grid.
/// @param width Grid width.
/// @param length Grid length.
/// @param height Grid height.
/// @param startX Starting X coordinate of largest region.
/// @param startY Starting Y coordinate of largest region.
void keepOnlyLargestMask(uint16_t ***grid, int width, int length, int height, int startX, int startY);

/// @brief Dilate the mask by one pixel.
/// @param grid Pointer to the 3D grid.
/// @param width Grid width.
/// @param length Grid length.
/// @param height Grid height.
/// @return Number of pixels added.
int dilateMask(uint16_t ***grid, int width, int length, int height, int maxToAdd);

/// @brief Generate a ridged noise mask for the grid.
/// @param grid Pointer to the 3D grid.
/// @param width Grid width.
/// @param length Grid length.
/// @param height Grid height.
/// @param targetFullness Target percentage of filled cells.
/// @param seed Noise seed.
void generateRidgedMask(uint16_t ***grid, uint32_t width, uint32_t length, uint32_t height, uint32_t targetFullness, uint32_t seed);

/// @brief Save noise map to debug file.
/// @param noiseMap Pointer to the noise map.
/// @param width Grid width.
/// @param length Grid length.
void saveNoiseDebug(float *noiseMap, uint32_t width, uint32_t length);

/// @brief Save binary mask to debug file.
/// @param grid Pointer to the 3D grid.
/// @param width Grid width.
/// @param length Grid length.
void saveBinaryMaskDebug(uint16_t ***grid, uint32_t width, uint32_t length);

#endif // cgsme_NOISE_H
