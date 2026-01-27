#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/param.h>
#include <math.h>

#include "threadRandom.h"
#include "generator.h"
#include "cgsme_debug.h"
#include "tiles.h"
#include "tinycthread/tinycthread.h"
#include "cgsme_utils.h"
#include "cgsme_noise.h"
#include "cgsme_topology.h"
#include "cgsme_solver.h"

#ifndef __linux__
#define MAX(a, b) ((a) > (b) ? a : b)
#define MIN(a, b) ((a) < (b) ? a : b)
#endif

uint16_t ***globalGrid;
uint32_t w, l;

/// Generate a 3D grid using Wave Function Collapse (WFC).
///
/// Parameters:
///     width   - number of columns per layer.
///     length  - number of rows per layer.
///     height  - number of layers.
///     seed    - RNG seed used to initialize per-thread RNGs.
///     fulness - target percentage (0-100) of tiles to collapse; a minimum
///               collapsed count is enforced by the implementation.
///
/// Returns:
///     Pointer to a newly-allocated 3D grid. The return value is an array of
///     `height` pointers to layers; each layer is an array of `length` row
///     pointers and each row contains `width` `uint16_t` entries. The caller
///     owns the returned memory and must free it with `freeGrid(grid, width,
///     length, height)`.
///
/// Behavior / Notes:
///     - Allocates memory with `malloc` for the top-level array, each layer,
///       and each row, then spawns one thread per layer via `thrd_create` to
///       run `generateLayerThread`.
///     - The function currently does not perform robust malloc error handling;
///       partial allocation may lead to undefined behavior if memory allocation
///       fails.
uint16_t ***generateGrid(uint32_t width, uint32_t length, uint32_t height, uint32_t seed, uint32_t fulness);

/// Free a grid previously returned by `generateGrid`.
///
/// Parameters:
///     grid   - pointer returned by `generateGrid`.
///     width  - width used when allocating.
///     length - length used when allocating.
///     height - height used when allocating.
///
/// Behavior / Notes:
///     - Frees row arrays, then layer arrays, then the top-level grid pointer.
///     - Expects the same dimensions used to allocate; passing a NULL pointer
///       or mismatched dimensions is undefined (the function does not check
///       for NULL in the current implementation).
void freeGrid(uint16_t ***grid, uint32_t width, uint32_t length, uint32_t height);
// Helper executed on a worker thread to generate a single layer.
// Parameters:
//     args - pointer to a `layerGenerationArgs` struct (defined in generator.h).
// Returns:
//     thrd-compatible int (thread return code). Populates the provided
//     `gridLayer` inside `args` and uses a per-thread RNG seeded from
//     `args->seed`.
int generateLayerThread(void *args);

/// Clean reachable tiles starting from (x,y) inside a single layer.
///
/// Parameters:
///     gridLayer - pointer to the layer (2D array indexed as [y][x]).
///     width     - number of columns.
///     length    - number of rows.
///     x, y      - starting coordinates for the cleaning flood-fill.
///
/// Behavior:
///     - Performs a recursive flood-fill over tiles reachable from (x,y) by
///       following open connections (uses directional masks such as
///       `North_Open_Mask`).
///     - Marks visited tiles by inverting their bits in-place (the
///       implementation flips bits to indicate visitation).
///     - Performs bounds checks and stops at tiles that are already
///       multi-state (popcount > 1) or outside the grid.
void cleanGrid(uint16_t **gridLayer, uint32_t width, uint32_t length, uint32_t x, uint32_t y);

void printProgressBar(int percentage)
{
    CGSME_PROFILE_FUNC(); // bugg
    int barWidth = 70;    // Width of the progress bar
    printf("[");
    int pos = barWidth * percentage / 100;
    for (int i = 0; i < barWidth; ++i)
    {
        if (i < pos)
            printf("=");
        else if (i == pos)
            printf(">");
        else
            printf(" ");
    }

    printf("] %d %%\r", percentage);
    fflush(stdout);
}

// EDGE FIXUP LOGIC

// Internal flags for direction manipulation (N=1, E=2, S=4, W=8)
#define DIR_N 1
#define DIR_E 2
#define DIR_S 4
#define DIR_W 8

// ARCHITECT LOGIC (pre-seeding)
// runs purely on the main thread before any threads are spawned
void runArchitect(uint16_t ***grid, uint32_t width, uint32_t length, uint32_t height, uint32_t fulness, uint32_t seed)
{
    CGSME_PROFILE_FUNC();

    // because of calloc the grid is already initialized to Empty_Tile (0)

    // generate Mask (Ridged Noise + Sanitize + Rescue)
    if (fulness < 100)
    {
        // the generator will write All_Possible_State (65535)
        // to valid locations. everything else stays 0
        generateRidgedMask(grid, width, length, height, fulness, seed);
    }
    else
    {
        for (uint32_t z = 0; z < height; z++)
            for (uint32_t y = 0; y < length; y++)
                for (uint32_t x = 0; x < width; x++)
                    grid[z][y][x] = All_Possible_State;
    }

    // place stairs
    int stairsPerLayer = (width * length) / 400;
    if (stairsPerLayer < 2)
        stairsPerLayer = 2;

    for (uint32_t z = 0; z < height - 1; z++)
    {
        int placedCount = 0;
        int attempts = 0;
        int maxAttempts = stairsPerLayer * 20;

        while (placedCount < stairsPerLayer && attempts < maxAttempts)
        {
            attempts++;
            int x = rand() % width;
            int y = rand() % length;

            // bounds
            if (x < 1 || y < 1 || x >= width - 1 || y >= length - 1)
                continue;

            // VOID CHECK must be on valid land
            if (grid[z][y][x] == Empty_Tile)
                continue;

            // occupancy
            if (grid[z][y][x] != All_Possible_State)
                continue;

            // anti-stacking
            if (z > 0 && grid[z - 1][y][x] == Special_X_Corridor)
                continue;

            grid[z][y][x] = Special_X_Corridor;    // stairs up
            grid[z + 1][y][x] = Normal_X_Corridor; // receiver hole
            placedCount++;
        }
    }
}

int generateLayerThread(void *args)
{
    CGSME_PROFILE_FUNC();
    layerGenerationArgs *arg = (layerGenerationArgs *)args;
    uint16_t **gridLayer = arg->gridLayer;
    uint32_t width = arg->width;
    uint32_t length = arg->length;
    int32_t startX = arg->startX;
    int32_t startY = arg->startY;
    uint32_t seed = arg->seed;
    uint32_t fulness = arg->fulness;

    uint32_t rngState = seed;

    // --- WEIGHTS CONFIGURATION ---
    float current_spawnrates[NUM_TILE_TYPES];
    if (fulness < 100)
    {
        // MASK MODE: Prioritize connectivity (L, T, I) over Dead Ends
        current_spawnrates[0] = 0.05f; // Normal X
        current_spawnrates[1] = 0.20f; // T
        current_spawnrates[2] = 0.40f; // L
        current_spawnrates[3] = 0.30f; // I
        current_spawnrates[4] = 0.05f; // D (Very Low)
        current_spawnrates[5] = 0.0f;  // Special X
    }
    else
    {
        // OCEAN MODE: Uniform start
        for (int i = 0; i < NUM_TILE_TYPES; ++i)
            current_spawnrates[i] = 1.0f / (float)NUM_TILE_TYPES;
    }

    // --- EXACT TARGET COUNTING ---
    // Count exact mask size
    int target_collapsed_count = 0;
    for (uint32_t y = 0; y < length; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            if (gridLayer[y][x] != Empty_Tile)
                target_collapsed_count++;
        }
    }

    int valid_collapsed_count = 0;

    // 1. DISTANCE MAP (Initialized to 0 if Mask Mode to prevent bias, or standard calculation)
    float **distMap = malloc(sizeof(float *) * length);
    for (uint32_t i = 0; i < length; i++)
    {
        distMap[i] = malloc(sizeof(float) * width);
        for (uint32_t j = 0; j < width; j++)
        {
            // If Mask Mode, we don't want center-bias, we want mask-bias.
            // If Ocean Mode, we use center bias.
            if (fulness < 100)
                distMap[i][j] = 0.0f;
            else
                distMap[i][j] = sqrtf(powf((float)j - startX, 2) + powf((float)i - startY, 2));
        }
    }

    MinHeap *heap = initHeap(width, length);

    // 2. INIT & CONSTRAINT PROPAGATION
    for (uint32_t i = 0; i < length; i++)
    {
        for (uint32_t j = 0; j < width; j++)
        {
            if (gridLayer[i][j] == Empty_Tile)
            {
                // Mask Void: Tell neighbors "I am a wall"
                updateNeighbours(gridLayer, width, length, j, i, heap, distMap, &rngState);
            }
            else if (__builtin_popcount(gridLayer[i][j]) == 1)
            {
                // Pre-placed Stairs: Propagate constraints
                valid_collapsed_count++;
                updateNeighbours(gridLayer, width, length, j, i, heap, distMap, &rngState);
            }
        }
    }

    // 3. SEED CENTER (If valid)
    if (gridLayer[startY][startX] != Empty_Tile && __builtin_popcount(gridLayer[startY][startX]) > 1)
    {
        gridLayer[startY][startX] = Normal_X_Corridor;
        updateNeighbours(gridLayer, width, length, startX, startY, heap, distMap, &rngState);
        valid_collapsed_count++;

        // Add neighbors to heap to kickstart
        if (startY > 0)
            heapInsertOrUpdate(heap, gridLayer, startX, startY - 1, distMap, &rngState);
        if (startY < length - 1)
            heapInsertOrUpdate(heap, gridLayer, startX, startY + 1, distMap, &rngState);
        if (startX > 0)
            heapInsertOrUpdate(heap, gridLayer, startX - 1, startY, distMap, &rngState);
        if (startX < width - 1)
            heapInsertOrUpdate(heap, gridLayer, startX + 1, startY, distMap, &rngState);
    }

    // High safety limit for complex masks
    int max_iter = width * length * 50;
    int iter = 0;

    // 4. MAIN LOOP
    while (valid_collapsed_count < target_collapsed_count && iter < max_iter)
    {
        iter++;

        // Only use dynamic pacing if NOT in mask mode
        if (fulness >= 100 && (iter % 10 == 0 || valid_collapsed_count < 50))
        {
            update_spawnrates(current_spawnrates, valid_collapsed_count, target_collapsed_count);
        }

        uint32_t cx, cy;
        bool found = heapPop(heap, gridLayer, &cx, &cy);

        if (!found)
        {
            // HEAP EMPTY: Reseed using AGGRESSIVE finder
            // This will pick any tile inside the mask that isn't solved yet
            if (findBestSeedLocation(gridLayer, width, length, distMap, &cx, &cy, &rngState))
            {
                found = true;

                // Force seed a tile type (Normal X is flexible)
                if (__builtin_popcount(gridLayer[cy][cx]) > 1)
                {
                    gridLayer[cy][cx] = Normal_X_Corridor;
                    updateNeighbours(gridLayer, width, length, cx, cy, heap, distMap, &rngState);
                    valid_collapsed_count++;

                    // Add neighbors
                    if (cy > 0)
                        heapInsertOrUpdate(heap, gridLayer, cx, cy - 1, distMap, &rngState);
                    if (cy < length - 1)
                        heapInsertOrUpdate(heap, gridLayer, cx, cy + 1, distMap, &rngState);
                    if (cx > 0)
                        heapInsertOrUpdate(heap, gridLayer, cx - 1, cy, distMap, &rngState);
                    if (cx < width - 1)
                        heapInsertOrUpdate(heap, gridLayer, cx + 1, cy, distMap, &rngState);

                    continue; // Skip the collapse step for this iteration
                }
            }
            else
            {
                // Truly no more tiles left to fill
                break;
            }
        }

        // Collapse
        if (__builtin_popcount(gridLayer[cy][cx]) > 1)
        {
            collapseTile(&gridLayer[cy][cx], current_spawnrates, &rngState);
            updateNeighbours(gridLayer, width, length, cx, cy, heap, distMap, &rngState);

            // Note: Because updateNeighbours now revives dead tiles, gridLayer will never be Empty_Tile
            // unless it was Mask Void. It will be All_Possible if it failed.
            if (gridLayer[cy][cx] != Empty_Tile && __builtin_popcount(gridLayer[cy][cx]) == 1)
                valid_collapsed_count++;
        }

        // Add neighbors to heap
        // Only add if they are still uncollapsed candidates
        if (cy > 0 && __builtin_popcount(gridLayer[cy - 1][cx]) > 1)
            heapInsertOrUpdate(heap, gridLayer, cx, cy - 1, distMap, &rngState);
        if (cy < length - 1 && __builtin_popcount(gridLayer[cy + 1][cx]) > 1)
            heapInsertOrUpdate(heap, gridLayer, cx, cy + 1, distMap, &rngState);
        if (cx > 0 && __builtin_popcount(gridLayer[cy][cx - 1]) > 1)
            heapInsertOrUpdate(heap, gridLayer, cx - 1, cy, distMap, &rngState);
        if (cx < width - 1 && __builtin_popcount(gridLayer[cy][cx + 1]) > 1)
            heapInsertOrUpdate(heap, gridLayer, cx + 1, cy, distMap, &rngState);

        // VOID LOGIC (Only for Ocean Mode)
        // If we hit target count in non-masked mode, start deleting unnecessary tiles
        if (fulness >= 100 && valid_collapsed_count >= target_collapsed_count)
        {
            if (!isTileRequired(gridLayer, width, length, cx, cy))
            {
                gridLayer[cy][cx] = Empty_Tile;
                updateNeighbours(gridLayer, width, length, cx, cy, heap, distMap, &rngState);
                valid_collapsed_count--; // Adjust count
            }
        }
    }

    // 5. CLEANUP & WELDING
    for (uint32_t i = 0; i < length; i++)
    {
        for (uint32_t j = 0; j < width; j++)
        {
            // If anything is still superposition (shouldn't be), kill it
            if (__builtin_popcount(gridLayer[i][j]) > 1)
                gridLayer[i][j] = Empty_Tile;
        }
    }

    sealMazeEdges(gridLayer, width, length);
    fixupEdges(gridLayer, width, length);
    findConnectedRegionsInPlace(gridLayer, width, length);
    germanWelderInPlace(gridLayer, width, length, &rngState);

    // Free memory
    freeHeap(heap);
    for (uint32_t i = 0; i < length; i++)
        free(distMap[i]);
    free(distMap);

    // Unpack Regions
    for (uint32_t i = 0; i < length; i++)
    {
        for (uint32_t j = 0; j < width; j++)
        {
            if (gridLayer[i][j] == 0xFFFF)
                gridLayer[i][j] = Empty_Tile;
            else
            {
                uint8_t index = gridLayer[i][j] & 0xF;
                gridLayer[i][j] = indexToMask(index);
            }
        }
    }

    return 0;
}

uint16_t ***generateGrid(uint32_t width, uint32_t length, uint32_t height, uint32_t seed, uint32_t fulness)
{
    CGSME_PROFILE_FUNC();

    // SAFETY CHECK
    // WFC cannot mathematically operate on maps smaller than 3x3 (needs center + neighbors).
    // German Welder needs space to exist.
    if (width < 4 || length < 4 || height < 1)
    {
        return NULL; // fail instead of crashing
    }

    /* overall generator instrumentation */
#ifdef cgsme_DEBUG
    uint64_t __cgsme_grid_start = cgsme_now_us();
    uint64_t __cgsme_grid_start_cycles = cgsme_now_cycles();
#endif

    uint16_t ***grid = malloc(sizeof(uint16_t **) * height);

    for (int32_t i = 0; i < height; i++)
    {
        // USE CALLOC
        // ensures the grid is 00000000.
        // can check "if grid[i][j] == 0" later
        grid[i] = calloc(length, sizeof(uint16_t *));
        for (int j = 0; j < length; j++)
            grid[i][j] = calloc(width, sizeof(uint16_t));
    }

    srand(seed);

    // ARCHITECT PHASE
    runArchitect(grid, width, length, height, fulness, seed);

    // LAYER GENERATION PHASE (MULTI-THREADING)
    thrd_t *threads = malloc(sizeof(thrd_t) * height);
    layerGenerationArgs *args = malloc(sizeof(layerGenerationArgs) * height);

    // standard start point is center
    int32_t centerX = width / 2;
    int32_t centerY = length / 2;

    for (uint32_t i = 0; i < height; i++)
    {
        // esvery layer attempts to start seeding from the center (and the Architect seeds)

        args[i].gridLayer = grid[i];
        args[i].width = width;
        args[i].length = length;

        args[i].startX = centerX;
        args[i].startY = centerY;

        args[i].endX = centerX;
        args[i].endY = centerY;

        args[i].seed = nextRandom(&seed); // Use deterministic derivative seeds
        args[i].fulness = fulness;
        args[i].layerIndex = i;

        thrd_create(&threads[i], generateLayerThread, (void *)&args[i]);
    }

    // wait for threads
    for (uint32_t i = 0; i < height; i++)
        thrd_join(threads[i], NULL);

    free((void *)threads);
    free((void *)args);

#ifdef cgsme_DEBUG
    uint64_t __cgsme_grid_end = cgsme_now_us();
    uint64_t __cgsme_grid_end_cycles = cgsme_now_cycles();
    cgsme_profile_record("generateGrid", (unsigned long long)(__cgsme_grid_end - __cgsme_grid_start), (unsigned long long)(__cgsme_grid_end_cycles - __cgsme_grid_start_cycles));
    cgsme_profile_set_runinfo(height, width, length, seed, fulness);
#endif

    return grid;
}

void freeGrid(uint16_t ***grid, uint32_t width, uint32_t length, uint32_t height)
{
    CGSME_PROFILE_FUNC();
    for (int32_t i = 0; i < height; i++)
    {
        for (int32_t j = 0; j < length; j++)
        {
            free((void *)grid[i][j]);
        }
        free((void *)grid[i]);
    }
    free((void *)grid);
}

void cleanGrid(uint16_t **gridLayer, uint32_t width, uint32_t length, uint32_t x, uint32_t y)
{
    CGSME_PROFILE_FUNC();

    // recursive algorithm where we flip all the bits to mark it as visited

    // base case if we leave the grid
    //  __builtin_popcount(gridLayer[y][x]) > 1
    //  all the already collapsed 16 corridors can be 0 1 10 100 1000 10000 and so on...
    //  so if the popcount is more than 1 it means its not collapsed yet, skip that (should not happen but just in case)
    if (x >= width || x < 0 || y >= length || y < 0 || __builtin_popcount((unsigned)gridLayer[y][x]) > 1)
        return;

    uint16_t tmp = gridLayer[y][x];
    gridLayer[y][x] = ~gridLayer[y][x];

    // if the direction is open then we recursively keep going else return
    if (tmp & South_Open_Mask)
        cleanGrid(gridLayer, width, length, x, y - 1);
    if (tmp & North_Open_Mask)
        cleanGrid(gridLayer, width, length, x, y + 1);
    if (tmp & West_Open_Mask)
        cleanGrid(gridLayer, width, length, x + 1, y);
    if (tmp & East_Open_Mask)
        cleanGrid(gridLayer, width, length, x - 1, y);
}

void updateTileEntropy(uint16_t **gridLayer, uint32_t width, uint32_t length, uint32_t x, uint32_t y)
{
    CGSME_PROFILE_FUNC();

    uint16_t northMask = All_Possible_State;
    uint16_t eastMask = All_Possible_State;
    uint16_t southMask = All_Possible_State;
    uint16_t westMask = All_Possible_State;

    if (y != 0 && __builtin_popcount(gridLayer[y - 1][x]) == 1)
        northMask = South_Open_Mask;
    if (y != length - 1 && __builtin_popcount(gridLayer[y + 1][x]) == 1)
        southMask = North_Open_Mask;
    if (x != 0 && __builtin_popcount(gridLayer[y][x - 1]) == 1)
        westMask = East_Open_Mask;
    if (x != width - 1 && __builtin_popcount(gridLayer[y][x + 1]) == 1)
        eastMask = West_Open_Mask;

    gridLayer[y][x] &= northMask & southMask & eastMask & westMask;
}
