#ifdef cgsme_DEBUG

#include "cgsme_debug.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <string.h>
#include "tinycthread/tinycthread.h"

#ifdef _WIN32
#include <windows.h>
#endif

static FILE *g_logfile = NULL;
static mtx_t g_logmtx;
static int g_logmtx_active = 0;
static int g_enabled = 0; // 0 or 1

// Profiling aggregates
typedef struct ProfileEntry
{
    char *name;
    uint64_t count;
    uint64_t total_us;
    uint64_t min_us;
    uint64_t max_us;
    uint64_t total_cycles;
    uint64_t min_cycles;
    uint64_t max_cycles;
} ProfileEntry;

static ProfileEntry *g_profiles = NULL;
static size_t g_profile_count = 0;
static size_t g_profile_capacity = 0;
static uint64_t g_profile_threshold_us = 1000ULL; // default 1ms
static uint64_t g_profile_threshold_cycles = 0ULL;
static double g_profile_warning_percent = 5.0; // percent of total time to mark as hot

// Run info (set by generator)
static struct
{
    uint32_t layers;
    uint32_t width;
    uint32_t length;
    uint32_t seed;
    uint32_t fulness;
    int valid;
} g_runinfo = {0, 0, 0, 0, 0, 0};

// Quick mode flag (minimize overhead when benchmarking)
static int g_quick_mode = 0;

void cgsme_set_quick_mode(bool enabled)
{
    g_quick_mode = enabled ? 1 : 0;
}

bool cgsme_quick_mode_enabled(void)
{
    return g_quick_mode != 0;
}

void cgsme_init_debug(void)
{
    if (g_logmtx_active)
        return; // already init

    if (mtx_init(&g_logmtx, mtx_plain) != thrd_success)
    {
        g_logmtx_active = 0;
        return;
    }

    g_logmtx_active = 1;

    // open a logfile next to running binary
    g_logfile = fopen("cgsme_debug.log", "a");
    if (!g_logfile)
    {
        // fallback to stdout
        g_logfile = stdout;
    }

    // default to enabled = false; caller must opt in
    g_enabled = 0;
}

void cgsme_profile_set_thresholds(uint64_t us_threshold, uint64_t cycles_threshold)
{
    if (!g_logmtx_active)
        cgsme_init_debug();
    if (!g_logmtx_active)
        return;
    mtx_lock(&g_logmtx);
    g_profile_threshold_us = us_threshold;
    g_profile_threshold_cycles = cycles_threshold;
    mtx_unlock(&g_logmtx);
}

void cgsme_profile_set_warning_percent(double pct)
{
    if (!g_logmtx_active)
        cgsme_init_debug();
    if (!g_logmtx_active)
        return;
    mtx_lock(&g_logmtx);
    g_profile_warning_percent = pct;
    mtx_unlock(&g_logmtx);
}

void cgsme_profile_set_runinfo(uint32_t layers, uint32_t width, uint32_t length, uint32_t seed, uint32_t fulness)
{
    if (!g_logmtx_active)
        cgsme_init_debug();
    if (!g_logmtx_active)
        return;
    mtx_lock(&g_logmtx);
    g_runinfo.layers = layers;
    g_runinfo.width = width;
    g_runinfo.length = length;
    g_runinfo.seed = seed;
    g_runinfo.fulness = fulness;
    g_runinfo.valid = 1;
    mtx_unlock(&g_logmtx);
}

// Helper: strdup fallback
static char *cgsme_strdup(const char *s)
{
    if (!s)
        return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

// Find or create profile entry under mutex
static ProfileEntry *cgsme_profile_find_or_create(const char *name)
{
    // assume mutex is held
    for (size_t i = 0; i < g_profile_count; ++i)
    {
        if (strcmp(g_profiles[i].name, name) == 0)
            return &g_profiles[i];
    }
    // create
    if (g_profile_count == g_profile_capacity)
    {
        size_t newcap = (g_profile_capacity == 0) ? 16 : g_profile_capacity * 2;
        ProfileEntry *newarr = realloc(g_profiles, sizeof(ProfileEntry) * newcap);
        if (!newarr)
            return NULL;
        g_profiles = newarr;
        g_profile_capacity = newcap;
    }
    ProfileEntry *p = &g_profiles[g_profile_count++];
    p->name = cgsme_strdup(name);
    p->count = 0;
    p->total_us = 0;
    p->min_us = (uint64_t)-1;
    p->max_us = 0;
    p->total_cycles = 0;
    p->min_cycles = (uint64_t)-1;
    p->max_cycles = 0;
    return p;
}

void cgsme_profile_record(const char *name, uint64_t elapsed_us, uint64_t elapsed_cycles)
{
    if (!g_logmtx_active)
        cgsme_init_debug();
    if (!g_logmtx_active)
        return;

    // Fast-path: if debug disabled, skip
    if (!g_enabled)
        return;

    // Previously we skipped zero measurements; record everything so counts and distributions
    // include very fast functions (min_us may be 0). This makes it easier to see how time
    // is spread across all instrumented functions.

    mtx_lock(&g_logmtx);
    ProfileEntry *p = cgsme_profile_find_or_create(name);
    if (p)
    {
        p->count++;
        p->total_us += elapsed_us;
        if (elapsed_us < p->min_us)
            p->min_us = elapsed_us;
        if (elapsed_us > p->max_us)
            p->max_us = elapsed_us;

        p->total_cycles += elapsed_cycles;
        if (elapsed_cycles < p->min_cycles)
            p->min_cycles = elapsed_cycles;
        if (elapsed_cycles > p->max_cycles)
            p->max_cycles = elapsed_cycles;

        // Per-invocation logging if above thresholds
        if ((g_profile_threshold_us && elapsed_us >= g_profile_threshold_us) ||
            (g_profile_threshold_cycles && elapsed_cycles >= g_profile_threshold_cycles))
        {
            if (g_logfile)
            {
                uint64_t ms = cgsme_now_us() / 1000ULL;
                fprintf(g_logfile, "[cgsme %llu ms] [WARNING] %s elapsed=%llu us cycles=%llu\n", (unsigned long long)ms,
                        name, (unsigned long long)elapsed_us, (unsigned long long)elapsed_cycles);
                fflush(g_logfile);
            }
        }
    }
    mtx_unlock(&g_logmtx);
}

void cgsme_shutdown_debug(void)
{
    if (!g_logmtx_active)
        return;

    // Print profiling summary if present
    mtx_lock(&g_logmtx);
    if (g_profile_count > 0 && g_logfile)
    {
        fprintf(g_logfile, "\n[cgsme] Profiling summary (%zu entries):\n", g_profile_count);
        uint64_t total_us = 0;
        uint64_t total_cycles = 0;
        for (size_t i = 0; i < g_profile_count; ++i)
        {
            total_us += g_profiles[i].total_us;
            total_cycles += g_profiles[i].total_cycles;
        }

        // copy and sort
        ProfileEntry *arr = malloc(sizeof(ProfileEntry) * g_profile_count);
        if (arr)
        {
            for (size_t i = 0; i < g_profile_count; ++i)
                arr[i] = g_profiles[i];
            int cmp(const void *a, const void *b)
            {
                const ProfileEntry *pa = (const ProfileEntry *)a;
                const ProfileEntry *pb = (const ProfileEntry *)b;
                if (pa->total_cycles != pb->total_cycles)
                    return (pa->total_cycles < pb->total_cycles) ? 1 : -1;
                if (pa->total_us != pb->total_us)
                    return (pa->total_us < pb->total_us) ? 1 : -1;
                return 0;
            }
            qsort(arr, g_profile_count, sizeof(ProfileEntry), cmp);

            // Human-friendly table header
            fprintf(g_logfile, "%-30s %10s %12s %8s %8s %8s %14s %12s %12s %12s %8s\n",
                    "name", "count", "total_us", "avg_us", "min_us", "max_us",
                    "total_cycles", "avg_cyc", "min_cyc", "max_cyc", "%tot");

            for (size_t i = 0; i < g_profile_count; ++i)
            {
                ProfileEntry *p = &arr[i];
                double pct = (total_us > 0) ? (100.0 * (double)p->total_us / (double)total_us) : 0.0;
                double avg_us = (p->count > 0) ? (double)p->total_us / (double)p->count : 0.0;
                double avg_cycles = (p->count > 0) ? (double)p->total_cycles / (double)p->count : 0.0;
                int hot = (pct >= g_profile_warning_percent) ? 1 : 0;

                fprintf(g_logfile, "%-30s %10llu %12llu %8.2f %8llu %8llu %14llu %12.2f %12llu %12llu %6.2f%% %s\n",
                        p->name,
                        (unsigned long long)p->count,
                        (unsigned long long)p->total_us,
                        avg_us,
                        (unsigned long long)p->min_us,
                        (unsigned long long)p->max_us,
                        (unsigned long long)p->total_cycles,
                        avg_cycles,
                        (unsigned long long)p->min_cycles,
                        (unsigned long long)p->max_cycles,
                        pct,
                        hot ? "[HOT]" : "");
            }

            // Also print a machine-friendly CSV for ingestion
            fprintf(g_logfile, "\nCSV summary:\n");
            fprintf(g_logfile, "name,count,total_us,avg_us,min_us,max_us,total_cycles,avg_cycles,min_cycles,max_cycles,%%total_us\n");
            for (size_t i = 0; i < g_profile_count; ++i)
            {
                ProfileEntry *p = &arr[i];
                double pct = (total_us > 0) ? (100.0 * (double)p->total_us / (double)total_us) : 0.0;
                double avg_us = (p->count > 0) ? (double)p->total_us / (double)p->count : 0.0;
                double avg_cycles = (p->count > 0) ? (double)p->total_cycles / (double)p->count : 0.0;
                fprintf(g_logfile, "%s,%llu,%llu,%.2f,%llu,%llu,%llu,%.2f,%llu,%llu,%.2f%%\n",
                        p->name,
                        (unsigned long long)p->count,
                        (unsigned long long)p->total_us,
                        avg_us,
                        (unsigned long long)p->min_us,
                        (unsigned long long)p->max_us,
                        (unsigned long long)p->total_cycles,
                        avg_cycles,
                        (unsigned long long)p->min_cycles,
                        (unsigned long long)p->max_cycles,
                        pct);
            }

            free(arr);
        }

        // Print decorated run summary (if set)
        if (g_runinfo.valid && g_logfile)
        {
            fprintf(g_logfile, "\n=============================================================\n");
            fprintf(g_logfile, "RUN SUMMARY: %u layers x %u x %u seed=%u fulness=%u\n", g_runinfo.layers, g_runinfo.width, g_runinfo.length, g_runinfo.seed, g_runinfo.fulness);

            // Try to locate generateGrid entry (this represents wall-clock elapsed for the run)
            ProfileEntry *gen = NULL;
            for (size_t i = 0; i < g_profile_count; ++i)
            {
                if (strcmp(g_profiles[i].name, "generateGrid") == 0)
                {
                    gen = &g_profiles[i];
                    break;
                }
            }

            if (gen)
            {
                // Wall-clock elapsed is best represented by generateGrid's total_us (one run)
                fprintf(g_logfile, "Wall-clock (generateGrid): %llu us (%.6f s)\n", (unsigned long long)gen->total_us, (double)gen->total_us / 1000000.0);
                fprintf(g_logfile, "Summed measured time (sum of per-function totals): %llu us\n", (unsigned long long)total_us);
                fprintf(g_logfile, "Total cycles (sum of per-function cycles): %llu\n", (unsigned long long)total_cycles);
                if (gen->total_us > 0)
                {
                    double ratio = (double)total_us / (double)gen->total_us * 100.0;
                    fprintf(g_logfile, "Measured time / wall-clock = %.2f%% (values >100%% indicate nested calls or multithreaded CPU time)\n", ratio);
                }
            }
            else
            {
                // Fallback: no generateGrid entry; print sums
                fprintf(g_logfile, "Total time: %llu us (%.6f s)\n", (unsigned long long)total_us, (double)total_us / 1000000.0);
                fprintf(g_logfile, "Total cycles: %llu\n", (unsigned long long)total_cycles);
            }

            fprintf(g_logfile, "Note: per-function totals include nested calls and/or CPU time across threads; their sum may exceed the wall-clock elapsed time.\n");
            fprintf(g_logfile, "=============================================================\n\n");
        }
    }

    // free names and profile storage
    for (size_t i = 0; i < g_profile_count; ++i)
        free(g_profiles[i].name);
    free(g_profiles);
    g_profiles = NULL;
    g_profile_count = 0;
    g_profile_capacity = 0;

    mtx_unlock(&g_logmtx);

    if (g_logfile && g_logfile != stdout)
    {
        fflush(g_logfile);
        fclose(g_logfile);
        g_logfile = NULL;
    }

    mtx_destroy(&g_logmtx);
    g_logmtx_active = 0;
}

void cgsme_set_enabled(bool enabled)
{
    if (!g_logmtx_active)
        cgsme_init_debug();

    if (!g_logmtx_active)
        return;

    mtx_lock(&g_logmtx);
    g_enabled = enabled ? 1 : 0;
    mtx_unlock(&g_logmtx);
}

bool cgsme_get_enabled(void)
{
    if (!g_logmtx_active)
        return false;
    int val;
    mtx_lock(&g_logmtx);
    val = g_enabled;
    mtx_unlock(&g_logmtx);
    return val != 0;
}

uint64_t cgsme_now_us(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER now;
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64_t)((now.QuadPart * 1000000ULL) / freq.QuadPart);
#else
    struct timespec ts;
    // CLOCK_MONOTONIC is appropriate for elapsed timings
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000ULL);
#endif
}

uint64_t cgsme_now_cycles(void)
{
#if defined(__i386__) || defined(__x86_64__)
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    // Not supported on this architecture
    return 0;
#endif
}

void cgsme_log(const char *fmt, ...)
{
    if (!g_logmtx_active) // quick exit if not initialised
    {
        cgsme_init_debug();
        if (!g_logmtx_active)
            return;
    }

    // fast check for enabled flag (not strictly atomic but protected by mutex soon)
    if (!g_enabled)
        return;

    mtx_lock(&g_logmtx);

    if (!g_logfile)
        g_logfile = stdout;

    // print timestamp prefix
    uint64_t now = cgsme_now_us();
    uint64_t ms = now / 1000ULL;
    fprintf(g_logfile, "[cgsme %llu ms] ", (unsigned long long)ms);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_logfile, fmt, ap);
    va_end(ap);

    fprintf(g_logfile, "\n");
    fflush(g_logfile);

    mtx_unlock(&g_logmtx);
}

#endif // cgsme_DEBUG
