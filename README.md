# CGSME (Coherent Guided Stochastic Maze Engine)

Native C backend for **Escape from Ganivi**. Handles procedural map generation, topology analysis, and liminal space synthesis.

Built for speed and precise control.

## Pipeline

Standard WFC fills a square grid until it crashes. CGSME implements a **"Mask-First"** architecture to sculpt organic layouts with exact density control.

### 1. Mask Generation (The Shape)
Instead of simple noise thresholding (which creates islands), the engine uses **Ridged Multifractal Noise** combined with **Percentile Thresholding**.
*   **Ridged Noise:** Generates organic "veins" and "tiger stripes" rather than blobs.
*   **Domain Warping:** Twists the grid coordinates to force parallel ridges to touch/connect.
*   **Percentile Sort:** Pixels are sorted by score, selecting exactly the top `N%` to meet the target density.

### 2. Sanitize & Rescue
Math creates islands. The generator fixes them before the logic starts.
*   **Sanitize:** Uses an Iterative BFS (Heap-allocated to prevent stack overflow) to identify the largest connected continent. Deletes all smaller floating islands.
*   **Rescue:** If pruning islands drops the map below the target tile count, the main continent is **Dilated** (grown) pixel-by-pixel until the target density is hit exactly.

### 3. The Architect (Verticality)
Once the mask is valid, the Architect runs.
*   Places **Vertical Anchors** (Stairs Up/Receiver Holes) only on valid mask tiles.
*   Guarantees vertical connectivity across Z-layers.
*   Uses exclusion logic to prevent stacking stairs directly on top of each other.

### 4. The Solver (Lifeguard WFC)
Custom bitmask-based WFC implementation optimized with a Min-Heap.
*   **Constraint Propagation:** Updates only immediate neighbors ($O(1)$).
*   **The "Lifeguard":** In a masked map, tiles can suffocate (hit 0 entropy) due to neighbor constraints. Standard WFC would crash. CGSME detects dead tiles, resets them to `All_Possible`, and allows the Reseeder to try again.
*   **Aggressive Reseeding:** If the heap runs dry, the system scans the grid for *any* valid uncollapsed mask tile and forces a collapse, ignoring neighbor rules. **The mask MUST be filled.**

### 5. Post-Processing (The German Welder)
Once the maze is filled, the engine runs a Kruskal’s Algorithm pass.
*   Identifies disjoint regions.
*   Punches holes between them to guarantee 100% traversability.
*   *Why German?* Because it is precise and efficient.

## Integration & Usage

CGSME is designed to be compiled as a shared library (`.dll` or `.so`) and called from a host application (Unity, Unreal, Godot, or custom C++ engines).

### C API
The engine exposes a direct entry point for generation and a cleanup function to manage memory.

```c
#include "generator.h"

// 1. Configuration
uint32_t width = 50;
uint32_t length = 50;
uint32_t height = 3;
uint32_t seed = 12345;
uint32_t fullness = 70; // 70% density

// 2. Generate
// Returns a 3D array: grid[z][y][x]
// Each uint16_t is a bitmask representing tile connectivity.
uint16_t ***map = generateGrid(width, length, height, seed, fullness);

if (map) {
    // Access data (Layer 0, Row 10, Col 10)
    uint16_t tileMask = map[0][10][10];
    
    // ... Process/Render the map ...

    // 3. Cleanup
    // MANDATORY: The grid uses malloc internally. You must free it.
    freeGrid(map, width, length, height);
}
```

### Data Interpretation
The returned `uint16_t` is a bitmask. See `tiles.h` for specific flag definitions (e.g., `North_Open`, `East_Open`). 
*   `0`: Empty/Void (No tile).
*   `>0`: Valid tile. Check bits to determine wall/door orientation.

### Data Dictionary

The grid contains `uint16_t` values. `0` represents **Void** (Empty Space). Any other number corresponds to a specific tile shape defined in `tiles.h`.

| Value | Name | Shape / Connectivity |
| :--- | :--- | :--- |
| **0** | `Empty_Tile` | Void / Hole |
| **1** | `North_East` | ╚ Corner (Connects N+E) |
| **2** | `South_East` | ╔ Corner (Connects S+E) |
| **4** | `South_West` | ╗ Corner (Connects S+W) |
| **8** | `North_West` | ╝ Corner (Connects N+W) |
| **16** | `North_South` | ║ Straight (Vertical) |
| **32** | `West_East` | ═ Straight (Horizontal) |
| **64** | `North_T` | ╩ T-Junction (Points N) |
| **128** | `East_T` | ╠ T-Junction (Points E) |
| **256** | `South_T` | ╦ T-Junction (Points S) |
| **512** | `West_T` | ╣ T-Junction (Points W) |
| **1024** | `Normal_X` | ╬ 4-Way Intersection |
| **2048** | `Special_X` | ╬ Vertical Connector (Stairs Up) |
| **4096** | `North_D` | ╨ Dead End (Open N) |
| **8192** | `East_D` | ╞ Dead End (Open E) |
| **16384** | `South_D` | ╥ Dead End (Open S) |
| **32768** | `West_D` | ╡ Dead End (Open W) |

## Build

Built using CMake. This will generate the shared library file.

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Performance
*   **25x25x5 (Runtime Chunk):** ~1.07ms
*   **200x200x5 (Full Map, 70% Density):** ~16.08ms

## Dependencies
*   `tinycthread` (included) for multithreading.
*   Standard C11 libraries.

## Project Context

This engine was developed as the backend for a university project (*Escape from Ganivi*). It was built to explore the technical limits of procedural generation, specifically focusing on combining geometric noise generation with topological graph theory to solve the "isolated island" problem inherent in standard Wave Function Collapse algorithms.
