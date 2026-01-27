#include "cgsme_solver.h"
#include "cgsme_debug.h"
#include "cgsme_utils.h"
#include <math.h>

// returns true if any collapsed neighbor has an open connection pointing to this tile
bool isTileRequired(uint16_t **grid, uint32_t width, uint32_t length, uint32_t x, uint32_t y)
{
	CGSME_PROFILE_FUNC();
	// check NNORTH neighbor (y-1). points down (SOUTH)
	// 'North_Open_Mask' in tiles.h defines tiles that have a SOUTH port
	if (y > 0 && __builtin_popcount(grid[y - 1][x]) == 1)
	{
		if (grid[y - 1][x] & North_Open_Mask)
			return true;
	}

	// check SOUTH neighbor (y+1). It points up (NORTH).
	// 'South_Open_Mask' defines tiles that have a NORTH
	if (y < length - 1 && __builtin_popcount(grid[y + 1][x]) == 1)
	{
		if (grid[y + 1][x] & South_Open_Mask)
			return true;
	}

	if (x < width - 1 && __builtin_popcount(grid[y][x + 1]) == 1)
	{
		if (grid[y][x + 1] & East_Open_Mask)
			return true;
	}

	if (x > 0 && __builtin_popcount(grid[y][x - 1]) == 1)
	{
		if (grid[y][x - 1] & West_Open_Mask)
			return true;
	}

	return false;
}

// new version oc collapseTile with spawnrates (smooth gaussian model that changes over time)
void collapseTile(uint16_t *tile, float *rates, uint32_t *rng)
{
#ifdef cgsme_DEBUG
	uint64_t __cgsme_collapse_start = cgsme_now_us();
	uint64_t __cgsme_collapse_start_cycles = cgsme_now_cycles();
#endif
	if (*tile == 0)
		return;

	// 1. Calculate Total Weight of VALID options only
	float total_weight = 0.0f;
	int valid_options_count = 0;

	for (int i = 0; i < 16; i++)
	{
		// Check if the i-th bit is set (is this tile valid?)
		if ((*tile >> i) & 1)
		{
			int category_index = BIT_TO_CATEGORY[i];
			total_weight += rates[category_index];
			valid_options_count++;
		}
	}

	// EDGE CASE: If total_weight is 0 (e.g., we want DeadEnds only, but only T-junctions
	// are valid here), or if something went wrong, fallback to standard Uniform Random.
	if (total_weight <= 0.0001f || valid_options_count == 0)
	{
		// OLD LOGIC FALLBACK
		uint32_t pop_count = __builtin_popcount(*tile);
		if (pop_count == 0)
		{
#ifdef cgsme_DEBUG
			if (cgsme_get_enabled())
			{
				uint64_t __cgsme_collapse_end = cgsme_now_us();
				uint64_t __cgsme_collapse_end_cycles = cgsme_now_cycles();
				cgsme_profile_record("collapseTile", (unsigned long long)(__cgsme_collapse_end - __cgsme_collapse_start),
									 (unsigned long long)(__cgsme_collapse_end_cycles - __cgsme_collapse_start_cycles));
			}
#endif
			return;
		}
		uint32_t r = nextRandom(rng) % pop_count;
		uint32_t set_bits_found = 0;
		for (int i = 0; i < 16; i++)
		{
			if ((*tile >> i) & 1)
			{
				if (set_bits_found == r)
				{
					*tile = (1U << i);
#ifdef cgsme_DEBUG
					if (cgsme_get_enabled())
					{
						uint64_t __cgsme_collapse_end = cgsme_now_us();
						uint64_t __cgsme_collapse_end_cycles = cgsme_now_cycles();
						cgsme_profile_record("collapseTile", (unsigned long long)(__cgsme_collapse_end - __cgsme_collapse_start),
											 (unsigned long long)(__cgsme_collapse_end_cycles - __cgsme_collapse_start_cycles));
					}
#endif
					return;
				}
				set_bits_found++;
			}
		}
		do
		{
#ifdef cgsme_DEBUG
			if (cgsme_get_enabled())
			{
				uint64_t __cgsme_collapse_end = cgsme_now_us();
				uint64_t __cgsme_collapse_end_cycles = cgsme_now_cycles();
				cgsme_profile_record("collapseTile", (unsigned long long)(__cgsme_collapse_end - __cgsme_collapse_start),
									 (unsigned long long)(__cgsme_collapse_end_cycles - __cgsme_collapse_start_cycles));
			}
#endif
		} while (0);
		return;
	}

	// 2. Pick a random value within the total weight range
	// threadRandom returns uint32. Divide by max to get 0.0-1.0 float.
	// 4294967296.0 is 2^32.
	float random_01 = (float)nextRandom(rng) / 4294967296.0f;
	float random_val = random_01 * total_weight;

	// 3. find the winner
	for (int i = 0; i < 16; i++)
	{
		if ((*tile >> i) & 1)
		{
			int category_index = BIT_TO_CATEGORY[i];

			// subtract the weight of this option
			random_val -= rates[category_index];

			// dropped below zero, this is the one that should be picked
			if (random_val <= 0)
			{
				*tile = (1U << i);
#ifdef cgsme_DEBUG
				if (cgsme_get_enabled())
				{
					uint64_t __cgsme_collapse_end = cgsme_now_us();
					uint64_t __cgsme_collapse_end_cycles = cgsme_now_cycles();
					cgsme_profile_record("collapseTile", (unsigned long long)(__cgsme_collapse_end - __cgsme_collapse_start),
										 (unsigned long long)(__cgsme_collapse_end_cycles - __cgsme_collapse_start_cycles));
				}
#endif
				return;
			}
		}
	}

	// Fallback: Rounding errors might make the loop finish without picking.
	// Just pick the last valid bit we found.
	for (int i = 15; i >= 0; i--)
	{
		if ((*tile >> i) & 1)
		{
			*tile = (1U << i);
#ifdef cgsme_DEBUG
			if (cgsme_get_enabled())
			{
				uint64_t __cgsme_collapse_end = cgsme_now_us();
				cgsme_log("collapseTile() fallback-last-bit(%d) elapsed=%llu us", i, (unsigned long long)(__cgsme_collapse_end - __cgsme_collapse_start));
			}
#endif
			return;
		}
	}
}

void updateNeighbours(uint16_t **gridLayer, uint32_t width, uint32_t length, uint32_t x, uint32_t y, MinHeap *heap, float **distMap, uint32_t *rng)
{
#ifdef cgsme_DEBUG
	uint64_t __cgsme_neigh_start = cgsme_now_us();
	uint64_t __cgsme_neigh_start_cycles = cgsme_now_cycles();
#endif

	uint16_t northMask, eastMask, southMask, westMask;

	switch (gridLayer[y][x])
	{
	case Empty_Tile:
		northMask = North_Closed_Mask;
		eastMask = East_Closed_Mask;
		southMask = South_Closed_Mask;
		westMask = West_Closed_Mask;
		break;
	case Normal_X_Corridor:
	case Special_X_Corridor:
		northMask = North_Open_Mask;
		eastMask = East_Open_Mask;
		southMask = South_Open_Mask;
		westMask = West_Open_Mask;
		break;
	case North_East_Corridor:
		northMask = North_Open_Mask;
		eastMask = East_Open_Mask;
		southMask = South_Closed_Mask;
		westMask = West_Closed_Mask;
		break;
	case South_East_Corridor:
		northMask = North_Closed_Mask;
		eastMask = East_Open_Mask;
		southMask = South_Open_Mask;
		westMask = West_Closed_Mask;
		break;
	case South_West_Corridor:
		northMask = North_Closed_Mask;
		eastMask = East_Closed_Mask;
		southMask = South_Open_Mask;
		westMask = West_Open_Mask;
		break;
	case North_West_Corridor:
		northMask = North_Open_Mask;
		eastMask = East_Closed_Mask;
		southMask = South_Closed_Mask;
		westMask = West_Open_Mask;
		break;
	case North_South_Corridor:
		northMask = North_Open_Mask;
		eastMask = East_Closed_Mask;
		southMask = South_Open_Mask;
		westMask = West_Closed_Mask;
		break;
	case West_East_Corridor:
		northMask = North_Closed_Mask;
		eastMask = East_Open_Mask;
		southMask = South_Closed_Mask;
		westMask = West_Open_Mask;
		break;
	case North_T_Corridor:
		northMask = North_Open_Mask;
		eastMask = East_Open_Mask;
		southMask = South_Closed_Mask;
		westMask = West_Open_Mask;
		break;
	case East_T_Corridor:
		northMask = North_Open_Mask;
		eastMask = East_Open_Mask;
		southMask = South_Open_Mask;
		westMask = West_Closed_Mask;
		break;
	case South_T_Corridor:
		northMask = North_Closed_Mask;
		eastMask = East_Open_Mask;
		southMask = South_Open_Mask;
		westMask = West_Open_Mask;
		break;
	case West_T_Corridor:
		northMask = North_Open_Mask;
		eastMask = East_Closed_Mask;
		southMask = South_Open_Mask;
		westMask = West_Open_Mask;
		break;
	case North_DeadEnd:
		northMask = North_Open_Mask;
		eastMask = East_Closed_Mask;
		southMask = South_Closed_Mask;
		westMask = West_Closed_Mask;
		break;
	case East_DeadEnd:
		northMask = North_Closed_Mask;
		eastMask = East_Open_Mask;
		southMask = South_Closed_Mask;
		westMask = West_Closed_Mask;
		break;
	case South_DeadEnd:
		northMask = North_Closed_Mask;
		eastMask = East_Closed_Mask;
		southMask = South_Open_Mask;
		westMask = West_Closed_Mask;
		break;
	case West_DeadEnd:
		northMask = North_Closed_Mask;
		eastMask = East_Closed_Mask;
		southMask = South_Closed_Mask;
		westMask = West_Open_Mask;
		break;
	default:
		northMask = (uint16_t)-1;
		eastMask = (uint16_t)-1;
		southMask = (uint16_t)-1;
		westMask = (uint16_t)-1;
		break;
	}

	// apply masks & check changes
	// ONLY access Heap if heap != NULL (allows to use this function in cleanup phases too)

	// WEST (x-1)
	if ((int32_t)x - 1 >= 0 && __builtin_popcount(gridLayer[y][x - 1]) > 1)
	{
		uint16_t oldVal = gridLayer[y][x - 1];
		gridLayer[y][x - 1] &= westMask;

		// REVIVAL CHECK: If it became 0, it was a contradiction. Reset it.
		if (gridLayer[y][x - 1] == 0)
			gridLayer[y][x - 1] = All_Possible_State;

		if (heap && gridLayer[y][x - 1] != oldVal)
		{
			heapInsertOrUpdate(heap, gridLayer, x - 1, y, distMap, rng);
		}
	}

	// EAST (x+1)
	if ((int32_t)x + 1 < width && __builtin_popcount(gridLayer[y][x + 1]) > 1)
	{
		uint16_t oldVal = gridLayer[y][x + 1];
		gridLayer[y][x + 1] &= eastMask;

		if (gridLayer[y][x + 1] == 0)
			gridLayer[y][x + 1] = All_Possible_State;

		if (heap && gridLayer[y][x + 1] != oldVal)
		{
			heapInsertOrUpdate(heap, gridLayer, x + 1, y, distMap, rng);
		}
	}

	// NORTH (y-1)
	if ((int32_t)y - 1 >= 0 && __builtin_popcount(gridLayer[y - 1][x]) > 1)
	{
		uint16_t oldVal = gridLayer[y - 1][x];
		gridLayer[y - 1][x] &= northMask;

		if (gridLayer[y - 1][x] == 0)
			gridLayer[y - 1][x] = All_Possible_State;

		if (heap && gridLayer[y - 1][x] != oldVal)
		{
			heapInsertOrUpdate(heap, gridLayer, x, y - 1, distMap, rng);
		}
	}

	// SOUTH (y+1)
	if ((int32_t)y + 1 < length && __builtin_popcount(gridLayer[y + 1][x]) > 1)
	{
		uint16_t oldVal = gridLayer[y + 1][x];
		gridLayer[y + 1][x] &= southMask;

		if (gridLayer[y + 1][x] == 0)
			gridLayer[y + 1][x] = All_Possible_State;

		if (heap && gridLayer[y + 1][x] != oldVal)
		{
			heapInsertOrUpdate(heap, gridLayer, x, y + 1, distMap, rng);
		}
	}

#ifdef cgsme_DEBUG
	if (cgsme_get_enabled())
	{
		uint64_t __cgsme_neigh_end = cgsme_now_us();
		uint64_t __cgsme_neigh_end_cycles = cgsme_now_cycles();
		cgsme_profile_record("updateNeighbours", (unsigned long long)(__cgsme_neigh_end - __cgsme_neigh_start),
							 (unsigned long long)(__cgsme_neigh_end_cycles - __cgsme_neigh_start_cycles));
	}
#endif
}

// The scoring logic extracted to a helper
float calculateScore(uint16_t **grid, uint32_t x, uint32_t y, float **distMap, uint32_t *rng)
{
	CGSME_PROFILE_FUNC();
	uint32_t bitNum = __builtin_popcount(grid[y][x]);
	(void)distMap; // Distance bias removed - mask defines shape now

	// Entropy only - lower entropy = higher priority
	float score = (float)bitNum;

	// Tiny random noise to break ties
	float noise = ((float)nextRandom(rng) / 4294967296.0f) * 0.01f;
	return score + noise;
}

// Finds the best spot to start a new island (Closest to center that is still unvisited)
bool findBestSeedLocation(uint16_t **grid, uint32_t width, uint32_t length, float **distMap, uint32_t *outX, uint32_t *outY, uint32_t *rng)
{
	CGSME_PROFILE_FUNC();
	float minScore = 1e9;
	bool found = false;

	for (uint32_t i = 0; i < length; i++)
	{
		for (uint32_t j = 0; j < width; j++)
		{
			uint16_t tile = grid[i][j];

			// Skip Empty (void) tiles
			if (tile == Empty_Tile)
				continue;

			int popcount = __builtin_popcount(tile);

			// Skip already-collapsed tiles
			if (popcount == 1)
				continue;

			// CRITICAL FIX: If popcount is 0, this tile hit a contradiction.
			// Reset it AND immediately apply neighbor constraints.
			if (popcount == 0)
			{
				grid[i][j] = All_Possible_State;

				// Apply constraints from all collapsed neighbors
				// Use the same logic as updateNeighbours but in reverse

				// Check North neighbor (y-1, which is i-1)
				// If neighbor is collapsed, what mask should it have applied to us?
				if (i > 0)
				{
					uint16_t neighbor = grid[i - 1][j];
					if (neighbor == Empty_Tile)
					{
						// Void neighbor = we must close that direction
						grid[i][j] &= North_Closed_Mask;
					}
					else if (__builtin_popcount(neighbor) == 1)
					{
						// Get what mask this neighbor would apply to its South (us)
						// If neighbor opens South -> we need North_Open_Mask
						// If neighbor closes South -> we need North_Closed_Mask
						if (neighbor & North_Open_Mask) // neighbor has south opening
							grid[i][j] &= South_Open_Mask;
						else
							grid[i][j] &= South_Closed_Mask;
					}
				}

				// Check South neighbor (y+1, which is i+1)
				if (i < length - 1)
				{
					uint16_t neighbor = grid[i + 1][j];
					if (neighbor == Empty_Tile)
					{
						grid[i][j] &= South_Closed_Mask;
					}
					else if (__builtin_popcount(neighbor) == 1)
					{
						if (neighbor & South_Open_Mask) // neighbor has north opening
							grid[i][j] &= North_Open_Mask;
						else
							grid[i][j] &= North_Closed_Mask;
					}
				}

				// Check West neighbor (x-1, which is j-1)
				if (j > 0)
				{
					uint16_t neighbor = grid[i][j - 1];
					if (neighbor == Empty_Tile)
					{
						grid[i][j] &= West_Closed_Mask;
					}
					else if (__builtin_popcount(neighbor) == 1)
					{
						if (neighbor & West_Open_Mask) // neighbor has east opening
							grid[i][j] &= East_Open_Mask;
						else
							grid[i][j] &= East_Closed_Mask;
					}
				}

				// Check East neighbor (x+1, which is j+1)
				if (j < width - 1)
				{
					uint16_t neighbor = grid[i][j + 1];
					if (neighbor == Empty_Tile)
					{
						grid[i][j] &= East_Closed_Mask;
					}
					else if (__builtin_popcount(neighbor) == 1)
					{
						if (neighbor & East_Open_Mask) // neighbor has west opening
							grid[i][j] &= West_Open_Mask;
						else
							grid[i][j] &= West_Closed_Mask;
					}
				}

				// After applying constraints, check if still valid
				popcount = __builtin_popcount(grid[i][j]);
				if (popcount == 0)
				{
					// Still contradicted after applying constraints!
					// This is a valid masked tile that MUST be filled.
					// Force it to All_Possible_State and return it as a seed.
					// The main loop will collapse it, which might create a connection issue
					// that germanWelder will fix later.
					grid[i][j] = All_Possible_State;
					*outX = j;
					*outY = i;
					return true;
				}
				if (popcount == 1)
				{
					// Auto-collapsed to a single valid tile!
					// This is actually a GOOD candidate - return it so the main loop
					// can call updateNeighbours and count it properly.
					*outX = j;
					*outY = i;
					return true;
				}
			}

			// Valid candidate (popcount > 1)
			// Use only random noise for seed selection - no distance preference
			// This ensures isolated mask regions get seeded regardless of location
			float noise = (float)(((i * width + j) ^ nextRandom(rng)) & 0xFFFF) * 0.001f;

			if (noise < minScore)
			{
				minScore = noise;
				*outX = j;
				*outY = i;
				found = true;
			}
		}
	}

	return found;
}

void update_spawnrates(float rates[], int current_collapsed, int target_collapsed)
{
	CGSME_PROFILE_FUNC();

	// Progress = how much of the mask is filled (0.0 to 1.0)
	// This drives the Gaussian peak position and connector boost fade
	float progress = (target_collapsed > 0) ? ((float)current_collapsed / (float)target_collapsed) : 0.0f;
	if (progress > 1.0f)
		progress = 1.0f;

	// 1. Calculate Standard Gaussian Weights
	// Peak position moves from 0 (X tiles) to 4 (D tiles) as progress increases
	float peak_position = TILE_POSITIONS[NUM_TILE_TYPES - 1] * progress;
	float raw_weights[NUM_TILE_TYPES];
	float total_weight = 0.0f;

	for (int i = 0; i < NUM_TILE_TYPES; ++i)
	{
		// category 5 (special X) is forbidden from natural spawning
		if (i == 5)
		{
			raw_weights[i] = 0.0f;
			continue;
		}

		float tile_pos = TILE_POSITIONS[i];
		float distance_sq = pow(tile_pos - peak_position, 2);
		float exponent = -distance_sq / (2 * pow(GAUSS_WIDTH, 2));

		raw_weights[i] = exp(exponent);
	}

	// 2. APPLY CONNECTOR BOOST
	// boost L (Index 2) and I (Index 3).
	// The boost is strongest at start (1.0 - progress) and fades to 0.
	// multiply by raw_weights[0] (the X weight) or just add to it to ensure
	// it competes with the X tile which is dominant at the start.

	float current_boost = CONNECTOR_BOOST * (1.0f - progress);

	// Index 2 is L, Index 3 is I.
	// add raw value to them. Since exp() maxes at 1.0, adding 1.5 makes them very likely.
	raw_weights[2] += current_boost;
	raw_weights[3] += current_boost;

	// 3. Normalize
	for (int i = 0; i < NUM_TILE_TYPES; ++i)
	{
		total_weight += raw_weights[i];
	}

	if (total_weight > 0.0f)
	{
		for (int i = 0; i < NUM_TILE_TYPES; ++i)
		{
			rates[i] = raw_weights[i] / total_weight;
		}
	}
}