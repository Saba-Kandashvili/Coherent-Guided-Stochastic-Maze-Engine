#ifndef __TILES__
#define __TILES__

#include <stdint.h>

/*naming convention


	2 openning Corridors are namded based on the position of their opennings on the cardinal directions

	Vertical Direction comes first , then horizontal direction

	so for instance a corridor like this would be named

	 @
	 @@

	 North_East_Corridor

	 in the case of T Corridors it will be named on the differring openning direction name
	 so for instance

	  @
	 @@@

	 will be named North_T_Corridor


	int the case of 4 openning X corridor it can be either Normal or sending

	Normal
	 @
	@@@
	 @

	Sending

	 @!
	@@@
	 @

	deadEnds Will be named based on their openning direction

	for instance

	@@

	will be Named West_DeadEnd

	 each Corridor has its own asscoicated bit
	 a cell will be considered collapsed when there is only 1 bit left in it
*/

#define Empty_Tile (uint16_t)0

// Internal flags for direction manipulation (N=1, E=2, S=4, W=8)
#define DIR_N 1
#define DIR_E 2
#define DIR_S 4
#define DIR_W 8

// L type Corridors

/*
 @
 @@

*/

#define North_East_Corridor 1

/*

 @@
 @
*/

#define South_East_Corridor 1 << 1

/*

@@
 @
*/

#define South_West_Corridor 1 << 2

/*
 @
@@

*/

#define North_West_Corridor 1 << 3

// I type Corridors

/*
 @
 @
 @
*/

#define North_South_Corridor 1 << 4

/*

@@@

*/

#define West_East_Corridor 1 << 5

// T type Corridors

/*
 @
@@@

*/

#define North_T_Corridor 1 << 6

/*
 @
 @@
 @
*/

#define East_T_Corridor 1 << 7

/*

@@@
 @
*/

#define South_T_Corridor 1 << 8

/*
 @
@@
 @
*/

#define West_T_Corridor 1 << 9

// X corridors

/*
 @
@@@
 @
*/

#define Normal_X_Corridor 1 << 10

/*
 @!
@@@
 @
*/

#define Special_X_Corridor 1 << 11

// deadends

/*
 @
 @

*/

#define North_DeadEnd 1 << 12

/*

 @@

*/

#define East_DeadEnd 1 << 13

/*

 @
 @
*/

#define South_DeadEnd 1 << 14

/*

@@

*/

#define West_DeadEnd 1 << 15

// 1000000000000000
// 000000000000000 are free to use for reguion marking

/*
	Bit masks for collapsing

	we will have 8 bitmasks in total , 2 per direction , one open and one closed

	Naming convention will be Direction_Open/Closed_Mask
*/

#define North_Open_Mask (uint16_t)(South_East_Corridor | North_South_Corridor | South_West_Corridor | South_DeadEnd | Normal_X_Corridor | Special_X_Corridor | South_T_Corridor | East_T_Corridor | West_T_Corridor)

#define East_Open_Mask (uint16_t)(North_West_Corridor | South_West_Corridor | West_East_Corridor | West_DeadEnd | Normal_X_Corridor | Special_X_Corridor | South_T_Corridor | West_T_Corridor | North_T_Corridor)

#define South_Open_Mask (uint16_t)(North_West_Corridor | North_South_Corridor | North_East_Corridor | North_DeadEnd | Normal_X_Corridor | Special_X_Corridor | North_T_Corridor | West_T_Corridor | East_T_Corridor)

#define West_Open_Mask (uint16_t)(North_East_Corridor | South_East_Corridor | West_East_Corridor | East_DeadEnd | Normal_X_Corridor | Special_X_Corridor | East_T_Corridor | South_T_Corridor | North_T_Corridor)

#define North_Closed_Mask (uint16_t)(~North_Open_Mask)

#define East_Closed_Mask (uint16_t)(~East_Open_Mask)

#define South_Closed_Mask (uint16_t)(~South_Open_Mask)

#define West_Closed_Mask (uint16_t)(~West_Open_Mask)

// intial state

#define All_Possible_State (uint16_t)-1

// valid tiles for forced conncetions
// West_East_Corridor | South_T_Corridor | North_T_Corridor | Special_X_Corridor | Normal_X_Corridor;

#define West_East_Valid_Tiles (uint16_t)(Normal_X_Corridor | Special_X_Corridor | South_T_Corridor | North_T_Corridor | West_East_Corridor)
#define North_South_Valid_Tiles (uint16_t)(Normal_X_Corridor | Special_X_Corridor | East_T_Corridor | West_T_Corridor | North_South_Corridor)

// gauss dist configuration
#define NUM_TILE_TYPES 6

static inline bool isValidSingleTile(uint16_t t)
{
	if (t == Empty_Tile)
		return false;
	// mask containing all defined single-bit tiles
	const uint16_t ALL_TILES =
		North_East_Corridor | South_East_Corridor | South_West_Corridor | North_West_Corridor |
		North_South_Corridor | West_East_Corridor |
		North_T_Corridor | East_T_Corridor | South_T_Corridor | West_T_Corridor |
		Normal_X_Corridor | Special_X_Corridor |
		North_DeadEnd | East_DeadEnd | South_DeadEnd | West_DeadEnd;
	// ensure it's a single bit and belongs to the known set
	return (t & (t - 1)) == 0 && (t & ALL_TILES) != 0;
}

//  (0-15) -> bitmask
// map the 16 valid single-tile types to numbers 0-15.
static const uint16_t TILE_INDEX_TO_MASK[16] = {
	North_East_Corridor, South_East_Corridor, South_West_Corridor, North_West_Corridor, // 0-3
	North_South_Corridor, West_East_Corridor,											// 4-5
	North_T_Corridor, East_T_Corridor, South_T_Corridor, West_T_Corridor,				// 6-9
	Normal_X_Corridor, Special_X_Corridor,												// 10-11
	North_DeadEnd, East_DeadEnd, South_DeadEnd, West_DeadEnd							// 12-15
};

// mask -> index
// This uses a switch for safety, but the compiler optimizes it to a lookup.
static inline uint8_t maskToIndex(uint16_t mask)
{
	switch (mask)
	{
	case North_East_Corridor:
		return 0;
	case South_East_Corridor:
		return 1;
	case South_West_Corridor:
		return 2;
	case North_West_Corridor:
		return 3;
	case North_South_Corridor:
		return 4;
	case West_East_Corridor:
		return 5;
	case North_T_Corridor:
		return 6;
	case East_T_Corridor:
		return 7;
	case South_T_Corridor:
		return 8;
	case West_T_Corridor:
		return 9;
	case Normal_X_Corridor:
		return 10;
	case Special_X_Corridor:
		return 11;
	case North_DeadEnd:
		return 12;
	case East_DeadEnd:
		return 13;
	case South_DeadEnd:
		return 14;
	case West_DeadEnd:
		return 15;
	default:
		return 0; // should handle EMPTY elsewhere
	}
}

static inline uint16_t indexToMask(uint8_t index)
{
	return TILE_INDEX_TO_MASK[index & 0xF];
}

#endif
