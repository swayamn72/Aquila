#pragma once
// ============================================================
// MarketDataStore.h — Structure-of-Arrays Market Data Layout
//
// PROBLEM: Array-of-Structs (AoS) — what std::vector<MarketData> is:
//   Memory layout: [ts|o|h|l|c|v|id|sym][ts|o|h|l|c|v|id|sym]...
//   Each 64-byte cache line holds ONE complete bar (8 fields).
//
//   To compute SMA(20) we need only the 'close' field.
//   With AoS, loading bar[0].close also loads open/high/low/volume —
//   87.5% of each cache line is wasted bandwidth.
//   For 1M bars: 1M * 64B loaded, 1M * 8B actually used = 8x waste.
//
// SOLUTION: Structure-of-Arrays (SoA)
//   Memory layout: [c0][c1][c2][c3][c4][c5][c6][c7] (8 closes per line)
//   SMA(20) loads only 3 cache lines for 20 closing prices.
//   Cache waste: ~0% when accessing a single field.
//
// VECTORIZATION BENEFIT:
//   The inner SMA sum loop: for (i = start; i < end; ++i) sum += close[i];
//   With a contiguous double[], the compiler (GCC/Clang -O3 -march=native,
//   MSVC /O2 /arch:AVX2) generates AVX2 VADDPD instructions, processing
//   4 doubles per clock cycle instead of 1. Theoretical 4x throughput.
//
// HUGE PAGES:
//   close[], open[], etc. use HugePageAllocator — 2MB pages instead
//   of 4KB. For 1M doubles (8MB), this reduces TLB entries from
//   2048 to 4. Near-zero TLB miss rate on repeated scans.
// ============================================================
#include "MarketData.h"
#include "HugePageAllocator.h"
#include <vector>
#include <cstddef>

namespace Aquila {

struct MarketDataStore {
    // Each field is a separate contiguous array (SoA layout)
    // All arrays backed by HugePageAllocator for minimal TLB pressure
    HugeDoubleVec  open, high, low, close;
    HugeUint64Vec  volume, timestamp;
    std::size_t    count = 0;

    // Convert from the AoS layout used by CSVReader.
    // Called once at load time; the hot path always uses SoA.
    static MarketDataStore from_aos(const std::vector<MarketData>& aos) {
        MarketDataStore store;
        store.count = aos.size();
        store.open.reserve(aos.size());
        store.high.reserve(aos.size());
        store.low.reserve(aos.size());
        store.close.reserve(aos.size());
        store.volume.reserve(aos.size());
        store.timestamp.reserve(aos.size());

        for (const auto& bar : aos) {
            store.open.push_back(bar.open);
            store.high.push_back(bar.high);
            store.low.push_back(bar.low);
            store.close.push_back(bar.close);
            store.volume.push_back(bar.volume);
            store.timestamp.push_back(bar.timestamp);
        }
        return store;
    }

    // Batch SMA computation on SoA layout.
    // The compiler auto-vectorizes this loop with AVX2 under -O3 -march=native.
    // Significantly faster than the same loop on AoS due to cache efficiency.
    [[nodiscard]] double batch_sma(std::size_t end, std::size_t window) const noexcept {
        if (end < window || window == 0) return 0.0;
        double sum = 0.0;
        const std::size_t start = end - window;
        // This loop over a contiguous double[] is the canonical SIMD target.
        // With AVX2: 4 doubles/cycle; with SSE2: 2 doubles/cycle.
        for (std::size_t i = start; i < end; ++i) {
            sum += close[i];
        }
        return sum / static_cast<double>(window);
    }

    [[nodiscard]] std::size_t size()  const noexcept { return count; }
    [[nodiscard]] bool        empty() const noexcept { return count == 0; }
};

} // namespace Aquila
