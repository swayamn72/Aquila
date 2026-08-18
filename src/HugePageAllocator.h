#pragma once
// ============================================================
// HugePageAllocator.h — 2MB Huge Page Custom Allocator
//
// THE TLB PRESSURE PROBLEM:
//   Standard virtual memory uses 4KB pages. The CPU's data TLB
//   (dTLB) has ~32-64 entries. For a 1M-bar dataset:
//     close[] = 1M * 8 bytes = 8MB
//     8MB / 4KB = 2048 TLB entries needed
//     With only 64 dTLB entries: ~97% TLB miss rate
//     Each TLB miss = ~100ns page table walk
//     2048 unique pages * 100ns = 204μs lost to TLB walks per scan
//
//   With 2MB huge pages:
//     8MB / 2MB = 4 TLB entries needed
//     ~100% TLB hit rate → near-zero TLB pressure
//
// COMPATIBILITY:
//   Windows: VirtualAlloc with MEM_LARGE_PAGES requires
//            SeLockMemoryPrivilege. If unavailable, falls back
//            to regular VirtualAlloc transparently.
//   Linux:   mmap with MAP_HUGETLB. Falls back to standard mmap.
//   Other:   Falls back to std::aligned_alloc.
//
// USAGE (as std::vector allocator):
//   std::vector<double, HugePageAllocator<double>> close_prices;
// ============================================================
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sys/mman.h>
#ifndef MAP_HUGE_2MB
#define MAP_HUGE_2MB (21 << MAP_HUGE_SHIFT)
#endif
#endif

namespace Aquila {

template<typename T>
struct HugePageAllocator {
    using value_type      = T;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;

    HugePageAllocator() = default;
    template<typename U>
    constexpr explicit HugePageAllocator(const HugePageAllocator<U>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n) {
        if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::bad_alloc();

        const std::size_t bytes = n * sizeof(T);
        void* ptr = nullptr;

#ifdef _WIN32
        // Attempt large-page allocation (2MB pages)
        ptr = VirtualAlloc(nullptr, bytes,
                           MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES,
                           PAGE_READWRITE);
        if (!ptr) {
            // Fallback: regular committed memory (still better than malloc)
            ptr = VirtualAlloc(nullptr, bytes,
                               MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        }
#elif defined(__linux__)
        // Attempt 2MB hugetlb mapping
        ptr = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB,
                   -1, 0);
        if (ptr == MAP_FAILED) {
            // Fallback: standard anonymous mmap (still avoids malloc overhead)
            ptr = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (ptr == MAP_FAILED) ptr = nullptr;
        }
#else
        // Portable fallback with natural alignment
        ptr = std::aligned_alloc(4096, bytes);
#endif

        if (!ptr) throw std::bad_alloc();
        return static_cast<T*>(ptr);
    }

    void deallocate(T* p, std::size_t n) noexcept {
#ifdef _WIN32
        VirtualFree(p, 0, MEM_RELEASE);
#elif defined(__linux__)
        munmap(p, n * sizeof(T));
#else
        std::free(p);
#endif
        (void)n;
    }

    template<typename U>
    bool operator==(const HugePageAllocator<U>&) const noexcept { return true; }
    template<typename U>
    bool operator!=(const HugePageAllocator<U>&) const noexcept { return false; }
};

// Convenience aliases for common SoA field types
using HugeDoubleVec  = std::vector<double,   HugePageAllocator<double>>;
using HugeUint64Vec  = std::vector<uint64_t, HugePageAllocator<uint64_t>>;

} // namespace Aquila
