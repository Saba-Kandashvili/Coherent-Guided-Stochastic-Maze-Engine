#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "generator.h"
#include "cgsme_debug.h"
#include <time.h>

int main(int argc, char **argv)
{
    // 1. Setup Debugging
    setbuf(stdout, NULL);
    bool cgsme_debug_enabled = false;
    bool cgsme_quick = false;
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] && (strcmp(argv[i], "--cgsme-debug") == 0 || strcmp(argv[i], "-d") == 0))
            cgsme_debug_enabled = true;
        if (argv[i] && strcmp(argv[i], "--cgsme-debug-quick") == 0)
            cgsme_quick = true;
    }

    if (cgsme_quick)
    {
        cgsme_set_quick_mode(true);
    }
    else
    {
        cgsme_init_debug();
        if (cgsme_debug_enabled)
            cgsme_set_enabled(true);
    }

    // Optional profiling thresholds via CLI
    uint64_t cgsme_profile_us_th = 0;
    uint64_t cgsme_profile_cycles_th = 0;
    for (int i = 1; i < argc; ++i)
    {
        const char *a = argv[i];
        if (strncmp(a, "--cgsme-profile-threshold-us=", 27) == 0)
        {
            cgsme_profile_us_th = (uint64_t)strtoull(a + 27, NULL, 10);
        }
        else if (strncmp(a, "--cgsme-profile-threshold-cycles=", 33) == 0)
        {
            cgsme_profile_cycles_th = (uint64_t)strtoull(a + 33, NULL, 10);
        }
    }
    if (cgsme_profile_us_th || cgsme_profile_cycles_th)
        cgsme_profile_set_thresholds(cgsme_profile_us_th, cgsme_profile_cycles_th);

    for (int i = 1; i < argc; ++i)
    {
        if (strncmp(argv[i], "--cgsme-profile-warning-pct=", 27) == 0)
        {
            double pct = strtod(argv[i] + 27, NULL);
            if (pct > 0.0)
                cgsme_profile_set_warning_percent(pct);
        }
    }

    // 2. Configuration
    uint32_t width = 100;
    uint32_t length = 100;
    uint32_t height = 3;
    // uint32_t seed = time(NULL);
    uint32_t seed = 5;
    uint32_t fulness = 70;

    printf("Generating %dx%dx%d Maze (Seed: %u, Fullness: %u%%)...\n", width, length, height, seed, fulness);

    // 3. Generate & Benchmark
    if (cgsme_quick)
    {
        uint64_t start_us = cgsme_now_us();
        uint16_t ***grid = generateGrid(width, length, height, seed, fulness);
        uint64_t end_us = cgsme_now_us();

        if (grid)
        {
            double seconds = (double)(end_us - start_us) / 1000000.0;

            // --- VERIFICATION: COUNT FILLED TILES ---
            uint64_t filled_tiles = 0;
            uint64_t total_tiles = width * length * height;

            for (uint32_t z = 0; z < height; z++)
            {
                for (uint32_t y = 0; y < length; y++)
                {
                    for (uint32_t x = 0; x < width; x++)
                    {
                        if (grid[z][y][x] != 0)
                            filled_tiles++;
                    }
                }
            }

            printf("BENCH: generateGrid elapsed=%llu us (%.6f s)\n", (unsigned long long)(end_us - start_us), seconds);
            printf("STATS: Filled %llu / %llu tiles (%.1f%%)\n", filled_tiles, total_tiles, ((float)filled_tiles / total_tiles) * 100.0f);

            freeGrid(grid, width, length, height);
        }
        else
        {
            printf("BENCH: generateGrid FAILED (Returned NULL)\n");
        }
        return 0;
    }
    else
    {
        uint16_t ***grid = generateGrid(width, length, height, seed, fulness);

        if (grid)
        {
            printf("Generation Complete.\n");

            // --- OUTPUT ALL LAYERS ---
            // We write to "maze.txt" (or maze_packed.txt if you prefer)
            FILE *f = fopen("maze.txt", "w");
            if (f)
            {
                // Header: Width, Length, Height
                fprintf(f, "%d,%d,%d\n", width, length, height);

                // Body: Iterate Z (Layers), then Y, then X
                for (int z = 0; z < height; z++)
                {
                    for (int y = 0; y < length; y++)
                    {
                        for (int x = 0; x < width; x++)
                        {
                            fprintf(f, "%hu", grid[z][y][x]);
                            if (x < width - 1)
                                fprintf(f, ",");
                        }
                        fprintf(f, "\n");
                    }
                    // Optional: You could add a separator here if you wanted,
                    // but the parser can just calculate rows based on length.
                }
                fclose(f);
                printf("Wrote %d layers to maze.txt\n", height);
            }

            freeGrid(grid, width, length, height);
        }
        else
        {
            printf("Generation Failed.\n");
        }

        cgsme_shutdown_debug();
        return 0;
    }
}