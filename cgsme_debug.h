#ifndef __cgsme_DEBUG_H__
#define __cgsme_DEBUG_H__

#include <stdint.h>
#include <stdbool.h>

// Public API for optional timing + debug logging for the cgsme generator.
// This file intentionally provides no-op inline implementations when
// cgsme_DEBUG is not defined so production builds don't pay any cost.

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef _WIN32
#ifdef cgsme_BUILD_DLL
#define CGSME_API __declspec(dllexport)
#else
// For non-DLL builds (e.g., debug runner / static exe) don't force dllimport.
#define CGSME_API
#endif
#else
#define CGSME_API
#endif

#ifdef cgsme_DEBUG
    // Initialize debug subsystem (creates log + mutex). Safe to call multiple times.
    CGSME_API void cgsme_init_debug(void);
    // Shutdown debug subsystem and flush logs.
    CGSME_API void cgsme_shutdown_debug(void);
    // Enable/Disable runtime debug logging & timing (fast-check).
    CGSME_API void cgsme_set_enabled(bool enabled);
    CGSME_API bool cgsme_get_enabled(void);

    // High-resolution timestamp in microseconds.
    CGSME_API uint64_t cgsme_now_us(void);

    // High-resolution timestamp in CPU cycles (rdtsc) when available (0 on unsupported targets).
    CGSME_API uint64_t cgsme_now_cycles(void);

    // Profiling record: aggregate per-name stats. Records elapsed microseconds and cycles.
    CGSME_API void cgsme_profile_record(const char *name, uint64_t elapsed_us, uint64_t elapsed_cycles);

    // Configure per-invocation logging thresholds (microseconds, cycles). Set 0 to disable per-invocation logs.
    CGSME_API void cgsme_profile_set_thresholds(uint64_t us_threshold, uint64_t cycles_threshold);

    // Control what percentage of total time flags an entry as "hot" in the summary print (default 5.0)
    CGSME_API void cgsme_profile_set_warning_percent(double pct);

    // Record the run configuration used for the most recent generator run.
    // This will be printed in a decorated final line at shutdown.
    CGSME_API void cgsme_profile_set_runinfo(uint32_t layers, uint32_t width, uint32_t length, uint32_t seed, uint32_t fulness);

    // Quick benchmark mode: when enabled, scope profiling becomes a no-op to minimize overhead
    CGSME_API void cgsme_set_quick_mode(bool enabled);
    CGSME_API bool cgsme_quick_mode_enabled(void);

// Scope-based profiling macro: the preferred implementation uses GCC/Clang
// cleanup attribute to automatically record on scope exit. If cleanup is not
// available we fall back to a simple begin-only macro (note: functions with
// early returns won't auto-record in that fallback case unless you call
// cgsme_profile_record manually at those returns).
#ifdef __GNUC__
    typedef struct
    {
        const char *name;
        uint64_t start_us;
        uint64_t start_cycles;
    } __cgsme_profile_scope_t;
    static inline void __cgsme_profile_scope_end(__cgsme_profile_scope_t *p)
    {
        if (p && p->name)
            cgsme_profile_record(p->name, cgsme_now_us() - p->start_us, cgsme_now_cycles() - p->start_cycles);
    }
    /* In quick mode we avoid recording by clearing the name; cleanup handler checks name.
       We still initialize the struct but only set start timestamps when quick mode is not enabled. */
    static inline void __cgsme_profile_scope_init(__cgsme_profile_scope_t *p)
    {
        p->name = __func__;
        p->start_us = 0;
        p->start_cycles = 0;
        if (!cgsme_quick_mode_enabled())
        {
            p->start_us = cgsme_now_us();
            p->start_cycles = cgsme_now_cycles();
        }
        else
        {
            p->name = NULL;
        }
    }
#define CGSME_PROFILE_FUNC()                                                                                 \
    __cgsme_profile_scope_t __cgsme_prof __attribute__((cleanup(__cgsme_profile_scope_end))) = {NULL, 0, 0}; \
    __cgsme_profile_scope_init(&__cgsme_prof);
#else
/* Fallback: in non-gcc we do a cheap runtime check to avoid timestamps in quick mode */
#define CGSME_PROFILE_FUNC()                                                               \
    uint64_t __cgsme_profile_start_us = (cgsme_quick_mode_enabled() ? 0 : cgsme_now_us()); \
    uint64_t __cgsme_profile_start_cycles = (cgsme_quick_mode_enabled() ? 0 : cgsme_now_cycles());
#endif

    // Thread-safe printf style logging (only when debug enabled).
    CGSME_API void cgsme_log(const char *fmt, ...);

#else
// Ensure profiling macro is a no-op in non-debug builds
#ifndef CGSME_PROFILE_FUNC
#define CGSME_PROFILE_FUNC() ((void)0)
#endif

// No-op inline implementations for release builds
static inline void cgsme_init_debug(void) {}
static inline void cgsme_shutdown_debug(void) {}
static inline void cgsme_set_enabled(bool enabled) { (void)enabled; }
static inline bool cgsme_get_enabled(void) { return false; }
static inline uint64_t cgsme_now_us(void) { return 0; }
static inline uint64_t cgsme_now_cycles(void) { return 0; }
static inline void cgsme_profile_record(const char *name, uint64_t elapsed_us, uint64_t elapsed_cycles)
{
    (void)name;
    (void)elapsed_us;
    (void)elapsed_cycles;
}
static inline void cgsme_profile_set_thresholds(uint64_t us_threshold, uint64_t cycles_threshold)
{
    (void)us_threshold;
    (void)cycles_threshold;
}
static inline void cgsme_profile_set_warning_percent(double pct) { (void)pct; }
static inline void cgsme_profile_set_runinfo(uint32_t layers, uint32_t width, uint32_t length, uint32_t seed, uint32_t fulness)
{
    (void)layers;
    (void)width;
    (void)length;
    (void)seed;
    (void)fulness;
}
static inline void cgsme_log(const char *fmt, ...) { (void)fmt; }

#endif // cgsme_DEBUG

#ifdef __cplusplus
}
#endif

#endif // __cgsme_DEBUG_H__
