#ifndef CGSME_SOLVER_H
#define CGSME_SOLVER_H
#include <stdint.h>
#include <stdbool.h>
#include "threadRandom.h"
#include "cgsme_utils.h"
#include "tiles.h"

// gauss config

// how smooth the transition is , higher values means more smooth
static const float GAUSS_WIDTH = 2.0f; // this seems good

// 0.0 = No change (Grid-like)
// 1.0 = Moderate spacing
// 2.0 = Very twisty/long corridors between intersections
static const float CONNECTOR_BOOST = 2.5f; // nice amount of twists

/// Collapse a tile bitmask down to a single selected variant.
///
/// Parameters:
///     tile  - pointer to a tile bitmask where each set bit represents a valid tile
///             variant. On return *tile will contain exactly one set bit representing
///             the chosen variant. If *tile == 0 the function does nothing.
///     rates - pointer to an array of `NUM_TILE_TYPES` floats representing spawn
///             weights for each tile category. Each valid bit in the bitmask is
///             mapped to a category via `BIT_TO_CATEGORY` and selection is weighted
///             by `rates[category]`. The function only considers bits that are set
///             in the input mask.
///
/// Behavior:
///     - Chooses one of the currently-valid bits according to the provided
///       category weights. If the sum of weights for valid options is effectively
///       zero, the function falls back to uniform random selection among set bits.
///     - Uses `nextRandom(rng)` as the RNG source.
///
/// Notes / Safety:
///     - Expects `rates` to point to at least `NUM_TILE_TYPES` floats.
///     - Handles empty masks safely (no-op when *tile == 0).
///     - See `BIT_TO_CATEGORY` for how bits map to categories used by `rates`.
void collapseTile(uint16_t *tile, float *rates, uint32_t *rng);

/// Update neighbors' possible states by constraining them to match the tile at (x,y).
///
/// Parameters:
///     gridLayer - pointer to the grid layer (2D array indexed as [y][x]).
///     width     - width of the grid (number of columns).
///     length    - length of the grid (number of rows).
///     x, y      - coordinates of the tile whose neighbors will be updated.
///
/// Behavior / Notes:
///     - Selects directional masks (northMask, eastMask, southMask, westMask)
///       based on the value of `gridLayer[y][x]` (see `tiles.h` masks such as
///       `North_Closed_Mask`, `North_Open_Mask`, etc.).
///     - Applies each mask to the corresponding neighbor using bitwise AND:
///         neighbor &= <mask>;
///       but only when the neighbor is inside the grid bounds and not already
///       collapsed (i.e. __builtin_popcount(neighbor) != 1).
///     - For unrecognized tile values the function uses a full-mask (no restriction).
///     - Mutates `gridLayer` in-place.
///
/// Safety:
///     - Does bounds checks before touching neighbors.
///     - Expects `gridLayer[y][x]` to be a valid tile value from the known set,
///       but tolerates other values by applying no restriction (full-mask).
void updateNeighbours(uint16_t **gridLayer, uint32_t width, uint32_t length, uint32_t x, uint32_t y, MinHeap *heap, float **distMap, uint32_t *rng);

/// Recalculate tile spawn rates using a Gaussian model and connector boost.
///
/// Parameters:
///     rates            - caller-provided array of `NUM_TILE_TYPES` floats to fill.
///     current_collapsed - number of tiles already collapsed.
///     target_collapsed  - desired number of collapsed tiles (based on `fulness`).
///
/// Behavior:
///     - Computes a progress ratio and generates Gaussian weights over tile
///       categories (`TILE_POSITIONS`, `GAUSS_WIDTH`), applies a connector
///       boost favoring L and I tiles early, then normalizes into probabilities.
///     - If progress >= 1.0, forces dead-ends (D) to 100%.
void update_spawnrates(float rates[], int current_collapsed, int target_collapsed);

/// Update the possible-state mask for the tile at (x,y) based on collapsed
/// neighbors.
///
/// Parameters:
///     gridLayer - pointer to the layer.
///     width, length - layer dimensions.
///     x, y - coordinates of the tile to update.
///
/// Behavior:
///     - Constructs directional masks (north/east/south/west) based on the
///       presence of collapsed neighbors and applies them to the tile via
///       bitwise AND to reduce the set of valid variants.
void updateTileEntropy(uint16_t **gridLayer, uint32_t width, uint32_t length, uint32_t x, uint32_t y);

/// @brief Check if a tile is required (has any valid neighbor connections).
/// @param grid Pointer to the grid layer.
/// @param width Grid width.
/// @param length Grid length.
/// @param x X coordinate.
/// @param y Y coordinate.
/// @return true if tile is required, false otherwise.
bool isTileRequired(uint16_t **grid, uint32_t width, uint32_t length, uint32_t x, uint32_t y);

/// @brief Find the best seed location for next collapse using distance map.
/// @param grid Pointer to the grid layer.
/// @param width Grid width.
/// @param length Grid length.
/// @param distMap Distance map.
/// @param outX Output X coordinate.
/// @param outY Output Y coordinate.
/// @param rng Pointer to random state.
/// @return true if a valid location was found, false otherwise.
bool findBestSeedLocation(uint16_t **grid, uint32_t width, uint32_t length, float **distMap, uint32_t *outX, uint32_t *outY, uint32_t *rng);

/// @brief Calculate the score for a tile based on entropy, distance, and noise.
/// @param grid Pointer to the grid layer.
/// @param x X coordinate.
/// @param y Y coordinate.
/// @param distMap Distance map.
/// @param rng Pointer to random state.
/// @return Calculated score.
float calculateScore(uint16_t **grid, uint32_t x, uint32_t y, float **distMap, uint32_t *rng);

// 0: X, 1: T, 2: L, 3: I, 4: D
static const int BIT_TO_CATEGORY[16] = {
	2, 2, 2, 2, // Bits 0-3:   L Corners      (Index 2)
	3, 3,		// Bits 4-5:   I Straights    (Index 3)
	1, 1, 1, 1, // Bits 6-9:   T Junctions    (Index 1)
	0, 5,		// Bit 10: Normal X (Cat 0), Bit 11: Special X (Cat 5)
	4, 4, 4, 4	// Bits 12-15: Dead Ends      (Index 4)
};

// position of each tile type on the probability curve:
//  0 (X), 1 (T), 2 (L), 3 (I), 4 (D)
static const float TILE_POSITIONS[NUM_TILE_TYPES] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 0.0f}; // special X (wont be spawned by natural wfc)

#endif // cgsme_SOLVER_H