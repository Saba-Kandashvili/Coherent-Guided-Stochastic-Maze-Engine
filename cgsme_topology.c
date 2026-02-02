#include "cgsme_topology.h"
#include "tiles.h"
#include "cgsme_utils.h"
#include "threadRandom.h"

// welding logic using union find data structure
// --- Union-Find Helper Functions ---
UnionFind *createUnionFind(uint32_t size)
{
	CGSME_PROFILE_FUNC();
	UnionFind *uf = malloc(sizeof(UnionFind));
	uf->count = size;
	uf->parent = malloc(sizeof(uint16_t) * (size + 1));
	for (uint32_t i = 0; i <= size; i++)
		uf->parent[i] = i;
	return uf;
}

uint16_t findSet(UnionFind *uf, uint16_t i)
{
	CGSME_PROFILE_FUNC();
	if (uf->parent[i] == i)
		return i;
	uf->parent[i] = findSet(uf, uf->parent[i]); // Path compression
	return uf->parent[i];
}

void unionSets(UnionFind *uf, uint16_t i, uint16_t j)
{
	CGSME_PROFILE_FUNC();
	uint16_t root_i = findSet(uf, i);
	uint16_t root_j = findSet(uf, j);
	if (root_i != root_j)
	{
		uf->parent[root_i] = root_j;
	}
}

void destroyUnionFind(UnionFind *uf)
{
	CGSME_PROFILE_FUNC();
	free(uf->parent);
	free(uf);
}

void germanWelderInPlace(uint16_t **grid, uint32_t width, uint32_t length, uint32_t *rng)
{
	CGSME_PROFILE_FUNC();
	// find max Region ID (scan packed data)
	// check the upper 12 bits of every cell.
	uint16_t maxRegionID = 0;
	for (uint32_t i = 0; i < length; i++)
	{
		for (uint32_t j = 0; j < width; j++)
		{
			if (grid[i][j] == 0xFFFF)
				continue; // skip void
			uint16_t r = grid[i][j] >> 4;
			if (r > maxRegionID)
				maxRegionID = r;
		}
	}

	if (maxRegionID <= 1)
		return; // only 1 region exists, nothing to weld.

	// collect bridges
	// allocating upfront is faster. Max theoretical edges ~ width*length*2
	uint32_t maxBridges = width * length * 2;
	Bridge *bridges = malloc(sizeof(Bridge) * maxBridges);
	uint32_t count = 0;

	for (uint32_t y = 0; y < length; y++)
	{
		for (uint32_t x = 0; x < width; x++)
		{

			if (grid[y][x] == 0xFFFF)
				continue;

			uint16_t rA = grid[y][x] >> 4; // extract region A

			// check EAST (x+1)
			if (x < width - 1 && grid[y][x + 1] != 0xFFFF)
			{
				uint16_t rB = grid[y][x + 1] >> 4; // extract region B
				if (rA != rB)
				{
					bridges[count].regionA = rA;
					bridges[count].regionB = rB;
					bridges[count].x = x;
					bridges[count].y = y;
					bridges[count].dir = DIR_E;
					count++;
				}
			}

			// check SOUTH (y+1)
			if (y < length - 1 && grid[y + 1][x] != 0xFFFF)
			{
				uint16_t rB = grid[y + 1][x] >> 4; // extract region B
				if (rA != rB)
				{
					bridges[count].regionA = rA;
					bridges[count].regionB = rB;
					bridges[count].x = x;
					bridges[count].y = y;
					bridges[count].dir = DIR_S;
					count++;
				}
			}
		}
	}

	if (count == 0)
	{
		free(bridges);
		return;
	}

	// shuffle bridges (Fisher-Yates)
	for (uint32_t i = 0; i < count; i++)
	{
		uint32_t swapIdx = nextRandom(rng) % count;
		Bridge temp = bridges[i];
		bridges[i] = bridges[swapIdx];
		bridges[swapIdx] = temp;
	}

	// kruskal's algorithm
	UnionFind *uf = createUnionFind(maxRegionID);

	for (uint32_t i = 0; i < count; i++)
	{
		Bridge b = bridges[i];

		// if sets are disjoint, connect them
		if (findSet(uf, b.regionA) != findSet(uf, b.regionB))
		{
			unionSets(uf, b.regionA, b.regionB);

			// open wall on A
			openWallPacked(grid, b.x, b.y, b.dir);

			// open wall on B (opposite direction)
			uint32_t nx = b.x + (b.dir == DIR_E ? 1 : 0);
			uint32_t ny = b.y + (b.dir == DIR_S ? 1 : 0);
			uint8_t oppositeDir = (b.dir == DIR_E) ? DIR_W : DIR_N;

			openWallPacked(grid, nx, ny, oppositeDir);
		}
	}

	destroyUnionFind(uf);
	free(bridges);
}

// 1. COMPRESS -> 2. IDENTIFY -> 3. RETURN VOID (data is in grid)
void findConnectedRegionsInPlace(uint16_t **grid, uint32_t width, uint32_t length)
{
	CGSME_PROFILE_FUNC();
	// COMPRESSION
	// turn 16-bit masks into 4-bit indices (0-15)
	// mark EMPTY tiles as 0xFFFF
	for (uint32_t i = 0; i < length; i++)
	{
		for (uint32_t j = 0; j < width; j++)
		{
			if (grid[i][j] == Empty_Tile)
			{
				grid[i][j] = 0xFFFF;
			}
			else
			{
				// grid[i][j] = (uint16_t)maskToIndex(grid[i][j]);
				grid[i][j] = __builtin_ctz(grid[i][j]);
			}
		}
	}

	// IDENTIFICATION
	// any tile <= 15 is unvisited (first 12 bits are empty). assign it a Region ID
	uint16_t regionID = 1;
	for (uint32_t i = 0; i < length; i++)
	{
		for (uint32_t j = 0; j < width; j++)
		{
			if (grid[i][j] != 0xFFFF && grid[i][j] <= 15)
			{
				// determine if we ran out of regions (Max 4095)
				if (regionID < 4095)
				{
					// regionMarkerPacked(grid, width, length, regionID, j, i);
					regionMarkerIterative(grid, width, length, regionID, j, i);
					regionID++;
				}
			}
		}
	}
}

// helper to modify walls inside the packed format
void openWallPacked(uint16_t **grid, uint32_t x, uint32_t y, uint8_t directionFlag)
{
	CGSME_PROFILE_FUNC();
	if (grid[y][x] == 0xFFFF)
		return;

	uint16_t packed = grid[y][x];
	uint16_t region = packed >> 4; // top 12 bits
	uint8_t index = packed & 0xF;  // bottom 4 bits

	// unpack to get geometry
	uint16_t mask = indexToMask(index);
	uint8_t flags = getTileFlags(mask);

	// modify geometry
	flags |= directionFlag;

	// repack
	uint16_t newMask = getTileFromFlags(flags);
	uint8_t newIndex = maskToIndex(newMask);

	grid[y][x] = (region << 4) | newIndex;
}

// packedTile format: [ RegionID (12 bits) | TileIndex (4 bits) ]
void regionMarkerPacked(uint16_t **grid, uint32_t width, uint32_t length, uint16_t regionID, uint32_t x, uint32_t y)
{
	CGSME_PROFILE_FUNC();
	if (x >= width || y >= length)
		return;

	// IF it's void (0xFFFF) or already has a Region ID (> 15), stop.
	if (grid[y][x] == 0xFFFF || grid[y][x] > 15)
		return;

	// get current geometry
	uint8_t index = (uint8_t)grid[y][x]; // Currently just 0-15
	uint16_t mask = indexToMask(index);

	// pack RegionID into the upper 12 bits
	grid[y][x] = (regionID << 4) | index;

	// recurse
	if (mask & South_Open_Mask)
		regionMarkerPacked(grid, width, length, regionID, x, y - 1);
	if (mask & North_Open_Mask)
		regionMarkerPacked(grid, width, length, regionID, x, y + 1);
	if (mask & West_Open_Mask)
		regionMarkerPacked(grid, width, length, regionID, x + 1, y);
	if (mask & East_Open_Mask)
		regionMarkerPacked(grid, width, length, regionID, x - 1, y);
}

// ITERATIVE replacement for regionMarkerPacked
void regionMarkerIterative(uint16_t **grid, uint32_t width, uint32_t length, uint16_t regionID, uint32_t startX, uint32_t startY)
{
	TopoNode *queue = malloc(sizeof(TopoNode) * width * length);
	if (!queue)
		return;

	int head = 0, tail = 0;
	queue[tail++] = (TopoNode){startX, startY};

	// Mark Start
	uint8_t sIdx = (uint8_t)grid[startY][startX];
	grid[startY][startX] = (regionID << 4) | sIdx;

	while (head < tail)
	{
		TopoNode c = queue[head++];
		uint32_t cx = c.x;
		uint32_t cy = c.y;

		uint16_t packed = grid[cy][cx];
		uint8_t idx = packed & 0xF;
		uint16_t mask = indexToMask(idx);

		// LOGIC CLONED FROM RECURSIVE FUNCTION:

		// South -> y-1
		if (mask & South_Open_Mask)
		{
			if ((int32_t)cy - 1 >= 0 && grid[cy - 1][cx] <= 15 && grid[cy - 1][cx] != 0xFFFF)
			{
				uint8_t nIdx = (uint8_t)grid[cy - 1][cx];
				grid[cy - 1][cx] = (regionID << 4) | nIdx;
				queue[tail++] = (TopoNode){cx, cy - 1};
			}
		}

		// North -> y+1
		if (mask & North_Open_Mask)
		{
			if (cy + 1 < length && grid[cy + 1][cx] <= 15 && grid[cy + 1][cx] != 0xFFFF)
			{
				uint8_t nIdx = (uint8_t)grid[cy + 1][cx];
				grid[cy + 1][cx] = (regionID << 4) | nIdx;
				queue[tail++] = (TopoNode){cx, cy + 1};
			}
		}

		// West -> x+1 (Wait, really?)
		// Recursive: if (mask & West) ... x+1
		if (mask & West_Open_Mask)
		{
			if (cx + 1 < width && grid[cy][cx + 1] <= 15 && grid[cy][cx + 1] != 0xFFFF)
			{
				uint8_t nIdx = (uint8_t)grid[cy][cx + 1];
				grid[cy][cx + 1] = (regionID << 4) | nIdx;
				queue[tail++] = (TopoNode){cx + 1, cy};
			}
		}

		// East -> x-1
		// Recursive: if (mask & East) ... x-1
		if (mask & East_Open_Mask)
		{
			if ((int32_t)cx - 1 >= 0 && grid[cy][cx - 1] <= 15 && grid[cy][cx - 1] != 0xFFFF)
			{
				uint8_t nIdx = (uint8_t)grid[cy][cx - 1];
				grid[cy][cx - 1] = (regionID << 4) | nIdx;
				queue[tail++] = (TopoNode){cx - 1, cy};
			}
		}
	}
	free(queue);
}

void sealMazeEdges(uint16_t **gridLayer, uint32_t width, uint32_t length)
{
	CGSME_PROFILE_FUNC();
	for (uint32_t y = 0; y < length; y++)
	{
		for (uint32_t x = 0; x < width; x++)
		{
			// only process Void tiles
			if (gridLayer[y][x] != Empty_Tile)
				continue;

			uint8_t flags = 0;

			// 1. Check North Neighbor (y-1)
			// We need to know if they point DOWN (South) to us.
			// Tiles with South ports are in North_Open_Mask.
			if (y > 0 && gridLayer[y - 1][x] != Empty_Tile)
			{
				if (gridLayer[y - 1][x] & North_Open_Mask)
					flags |= DIR_N;
			}

			// 2. Check South Neighbor (y+1)
			// We need to know if they point UP (North) to us.
			// Tiles with North ports are in South_Open_Mask.
			if (y < length - 1 && gridLayer[y + 1][x] != Empty_Tile)
			{
				if (gridLayer[y + 1][x] & South_Open_Mask)
					flags |= DIR_S;
			}

			// 3. Check West Neighbor (x-1)
			// We need to know if they point RIGHT (East) to us.
			// Tiles with East ports are in West_Open_Mask.
			if (x > 0 && gridLayer[y][x - 1] != Empty_Tile)
			{
				if (gridLayer[y][x - 1] & West_Open_Mask)
					flags |= DIR_W;
			}

			// 4. Check East Neighbor (x+1)
			// We need to know if they point LEFT (West) to us.
			// Tiles with West ports are in East_Open_Mask.
			if (x < width - 1 && gridLayer[y][x + 1] != Empty_Tile)
			{
				if (gridLayer[y][x + 1] & East_Open_Mask)
					flags |= DIR_E;
			}

			if (flags != 0)
			{
				gridLayer[y][x] = getTileFromFlags(flags);
			}
		}
	}
}

void fixupEdges(uint16_t **gridLayer, uint32_t width, uint32_t length)
{
	CGSME_PROFILE_FUNC();
	// top row (y=0): remove NORTH connections (flag 1)
	for (uint32_t x = 0; x < width; x++)
	{
		uint8_t flags = getTileFlags(gridLayer[0][x]);
		if (flags & DIR_N)
		{
			flags &= ~DIR_N; // Turn off North bit
			gridLayer[0][x] = getTileFromFlags(flags);
		}
	}

	// bottom row (y=length-1): remove SOUTH connections (flag 4)
	for (uint32_t x = 0; x < width; x++)
	{
		uint8_t flags = getTileFlags(gridLayer[length - 1][x]);
		if (flags & DIR_S)
		{
			flags &= ~DIR_S;
			gridLayer[length - 1][x] = getTileFromFlags(flags);
		}
	}

	// left column (x=0): remove WEST connections (flag 8)
	for (uint32_t y = 0; y < length; y++)
	{
		uint8_t flags = getTileFlags(gridLayer[y][0]);
		if (flags & DIR_W)
		{
			flags &= ~DIR_W;
			gridLayer[y][0] = getTileFromFlags(flags);
		}
	}

	// right column (x=width-1): remove EAST connections (flag 2)
	for (uint32_t y = 0; y < length; y++)
	{
		uint8_t flags = getTileFlags(gridLayer[y][width - 1]);
		if (flags & DIR_E)
		{
			flags &= ~DIR_E;
			gridLayer[y][width - 1] = getTileFromFlags(flags);
		}
	}
}

// Helper: Convert a Tile ID to internal directional flags
uint8_t getTileFlags(uint16_t tile)
{
	CGSME_PROFILE_FUNC();
	if (tile == Empty_Tile)
		return 0;

	// Check based on physical openings defined in updateNeighbours logic
	switch (tile)
	{
	case North_DeadEnd:
		return DIR_N;
	case East_DeadEnd:
		return DIR_E;
	case South_DeadEnd:
		return DIR_S;
	case West_DeadEnd:
		return DIR_W;

	case North_East_Corridor:
		return DIR_N | DIR_E;
	case South_East_Corridor:
		return DIR_S | DIR_E;
	case South_West_Corridor:
		return DIR_S | DIR_W;
	case North_West_Corridor:
		return DIR_N | DIR_W;

	case North_South_Corridor:
		return DIR_N | DIR_S;
	case West_East_Corridor:
		return DIR_W | DIR_E;

	case North_T_Corridor:
		return DIR_N | DIR_E | DIR_W; // points N E W
	case East_T_Corridor:
		return DIR_N | DIR_E | DIR_S; // points N E S
	case South_T_Corridor:
		return DIR_E | DIR_S | DIR_W; // points E S W
	case West_T_Corridor:
		return DIR_N | DIR_S | DIR_W; // points N S W

	case Normal_X_Corridor:
		return DIR_N | DIR_E | DIR_S | DIR_W;
	case Special_X_Corridor:
		return DIR_N | DIR_E | DIR_S | DIR_W;
	default:
		return 0;
	}
}

// convert internal flags back to a Tile ID
uint16_t getTileFromFlags(uint8_t flags)
{
	CGSME_PROFILE_FUNC();
	switch (flags)
	{
	case DIR_N:
		return North_DeadEnd;
	case DIR_E:
		return East_DeadEnd;
	case DIR_S:
		return South_DeadEnd;
	case DIR_W:
		return West_DeadEnd;

	case (DIR_N | DIR_E):
		return North_East_Corridor;
	case (DIR_S | DIR_E):
		return South_East_Corridor;
	case (DIR_S | DIR_W):
		return South_West_Corridor;
	case (DIR_N | DIR_W):
		return North_West_Corridor;

	case (DIR_N | DIR_S):
		return North_South_Corridor;
	case (DIR_W | DIR_E):
		return West_East_Corridor;

	case (DIR_N | DIR_E | DIR_W):
		return North_T_Corridor;
	case (DIR_N | DIR_E | DIR_S):
		return East_T_Corridor;
	case (DIR_E | DIR_S | DIR_W):
		return South_T_Corridor;
	case (DIR_N | DIR_S | DIR_W):
		return West_T_Corridor;

	case (DIR_N | DIR_E | DIR_S | DIR_W):
		return Normal_X_Corridor;

	default:
		return Empty_Tile; // 0 connections or invalid combo
	}
}