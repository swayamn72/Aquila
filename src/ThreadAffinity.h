#pragma once
// ============================================================
// ThreadAffinity.h — CPU Core Pinning
//
// WHY PIN THREADS TO CORES?
//
//   By default, the OS scheduler migrates threads between cores
//   whenever it decides another core is less loaded. Each migration:
//     1. Invalidates the thread's L1 cache (32KB, ~4ns access) —
//        the new core starts cold. First few thousand memory accesses
//        take ~100ns (L2/L3) instead of ~4ns (L1).
//     2. Invalidates TLB entries — page table walks resume.
//     3. Adds context-switch overhead: save/restore registers (~1μs).
//
//   For a latency-sensitive engine processing 1M ticks, scheduling
//   jitter corrupts latency measurements and adds unpredictable spikes.
//
// PRODUCTION USAGE:
//   In real HFT systems, dedicated cores are isolated from the OS
//   scheduler entirely using the Linux kernel boot parameter
//   `isolcpus=N,M`. Aquila pins to a core; if you also set isolcpus,
//   that core becomes exclusively yours — sub-microsecond jitter.
//
// WINDOWS NOTE:
//   SetThreadAffinityMask takes a bitmask — bit N = core N.
//   Does not require elevated privileges (unlike huge pages).
// ============================================================
#include <cstdint>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace Aquila::sys {

// Pin the calling thread to a specific logical CPU core.
// Returns true on success, false on failure or unsupported platform.
// core_id = -1 is a no-op (returns false immediately).
inline bool pin_to_core(int core_id) noexcept {
    if (core_id < 0) return false;

#ifdef _WIN32
    const DWORD_PTR mask = static_cast<DWORD_PTR>(1ULL) << core_id;
    if (SetThreadAffinityMask(GetCurrentThread(), mask) == 0) {
        std::cerr << "[sys] SetThreadAffinityMask failed (error "
                  << GetLastError() << ")\n";
        return false;
    }
    return true;

#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0) {
        std::cerr << "[sys] pthread_setaffinity_np failed\n";
        return false;
    }
    return true;

#else
    (void)core_id;
    std::cerr << "[sys] CPU affinity not supported on this platform\n";
    return false;
#endif
}

// Returns the number of logical processors available.
inline int logical_core_count() noexcept {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return static_cast<int>(info.dwNumberOfProcessors);
#elif defined(__linux__)
    return static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
#else
    return -1;
#endif
}

} // namespace Aquila::sys
