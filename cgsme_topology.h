#ifndef CGSME_TOPOLOGY_H
#define CGSME_TOPOLOGY_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

// STRUCTS FOR REGION CONNECTING
typedef struct
{
    uint16_t regionA;
    uint16_t regionB;
    uint32_t x; // coordinate of Tile A
    uint32_t y;
    uint8_t dir; // direction from A to B (DIR_N, DIR_E)
} Bridge;

// unionfind for kruskal
typedef struct
{
    uint16_t *parent;
    uint32_t count;
} UnionFind;

// internal representation of a node in topology graph
typedef struct
{
    uint32_t x, y;
} TopoNode;

/// @brief Create a Union-Find structure for the given size.
/// @param size Number of elements.
/// @return Pointer to the newly allocated UnionFind.
UnionFind *createUnionFind(uint32_t size);

/// @brief Find the set representative for element i (with path compression).
/// @param uf Pointer to the UnionFind structure.
/// @param i Element index.
/// @return Set representative.
uint16_t findSet(UnionFind *uf, uint16_t i);

/// @brief Union two sets together.
/// @param uf Pointer to the UnionFind structure.
/// @param i First element.
/// @param j Second element.
void unionSets(UnionFind *uf, uint16_t i, uint16_t j);

/// @brief Free the Union-Find structure.
/// @param uf Pointer to the UnionFind structure to free.
void destroyUnionFind(UnionFind *uf);

/// @brief Identify connected regions within the layer grid in-place.
/// @param grid Pointer to the 2D grid layer.
/// @param width Number of columns per layer.
/// @param length Number of rows per layer.
void findConnectedRegionsInPlace(uint16_t **grid, uint32_t width, uint32_t length);

/// @brief Connects the disconnected but valid regions together.
/// @param grid Pointer to the 2D grid layer.
/// @param width Number of columns.
/// @param length Number of rows.
/// @param rng Pointer to random state.
/// it is german cause it is precise and efficient
void germanWelderInPlace(uint16_t **grid, uint32_t width, uint32_t length, uint32_t *rng);

/// @brief Seal maze edges by filling void tiles adjacent to open corridors.
/// @param gridLayer Pointer to the grid layer.
/// @param width Grid width.
/// @param length Grid length.
void sealMazeEdges(uint16_t **gridLayer, uint32_t width, uint32_t length);

/// @brief Fix edges at grid boundaries by removing out-of-bounds connections.
/// @param gridLayer Pointer to the grid layer.
/// @param width Grid width.
/// @param length Grid length.
void fixupEdges(uint16_t **gridLayer, uint32_t width, uint32_t length);

/// @brief Convert a tile ID to internal directional flags.
/// @param tile The tile ID.
/// @return Directional flags (DIR_N, DIR_E, DIR_S, DIR_W).
uint8_t getTileFlags(uint16_t tile);

/// @brief Convert internal directional flags to a tile ID.
/// @param flags Directional flags.
/// @return The corresponding tile ID.
uint16_t getTileFromFlags(uint8_t flags);

/// @brief Recursively mark connected tiles in a packed grid format using a Region ID.
/// @param grid Packed grid where each cell is [RegionID (12 bits) | TileIndex (4 bits)].
/// @param width Grid width.
/// @param length Grid length.
/// @param regionID Region identifier to write into top bits.
/// @param x Starting X coordinate.
/// @param y Starting Y coordinate.
void regionMarkerPacked(uint16_t **grid, uint32_t width, uint32_t length, uint16_t regionID, uint32_t x, uint32_t y);

/// @brief Flood-fill into a separate region map for un-packed grids.
/// @param gridLayer The grid layer (tile masks).
/// @param regionMap Map to write region IDs into (non-zero means visited).
/// @param width Grid width.
/// @param length Grid length.
/// @param regionID Region identifier to write to `regionMap`.
/// @param x Starting X coordinate.
/// @param y Starting Y coordinate.
void regionMarker(uint16_t **gridLayer, uint16_t **regionMap, uint32_t width, uint32_t length, uint16_t regionID, uint32_t x, uint32_t y);

/// @brief Open a wall in the packed grid representation at (x,y).
/// @param grid Packed grid where each cell is [RegionID (12 bits) | TileIndex (4 bits)].
/// @param x X coordinate.
/// @param y Y coordinate.
/// @param directionFlag One of DIR_N/DIR_E/DIR_S/DIR_W to open.
void openWallPacked(uint16_t **grid, uint32_t x, uint32_t y, uint8_t directionFlag);

/// TODO: document
void regionMarkerIterative(uint16_t **grid, uint32_t width, uint32_t length, uint16_t regionID, uint32_t startX, uint32_t startY);

#endif // cgsme_TOPOLOGY_H