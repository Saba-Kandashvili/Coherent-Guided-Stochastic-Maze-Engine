***

# CGSME (Coherent Guided Stochastic Maze Engine)

Native C backend for **Escape from Ganivi**. Handles procedural map generation, topology analysis, and liminal space synthesis.

Built for speed (sub-2ms for runtime chunks) and presice control.

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
*   Identifies disjoint regions (e.g., a room that generated with no doors).
*   Punches holes between them to guarantee 100% traversability.
*   *Why German?* Because it is precise and efficient.

## Build

Built using CMake. 

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

Outputs a shared library (`.dll` / `.so`) for hot-loading into Unity.

## Performance
*   **25x25x5 (Runtime Chunk):** ~1.07ms
*   **200x200x5 (Full Map, 70% Density):** ~16.08ms

## Dependencies
*   `tinycthread` (included) for multithreading.
*   Standard C11 libraries.
