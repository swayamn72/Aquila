#pragma once
// ============================================================
// LatencyProfiler.h — TSC-Based Nanosecond Latency Histogram
//
// WHY RDTSC OVER std::chrono::steady_clock?
//
//   steady_clock::now() on Linux calls clock_gettime(CLOCK_MONOTONIC).
//   Even with the vDSO optimization (avoids syscall), this costs
//   20-50ns per call. On Windows, QueryPerformanceCounter costs similar.
//
//   __rdtsc() reads the hardware Time Stamp Counter via a single
//   RDTSC instruction — ~3ns overhead including the serializing fence.
//
//   For sub-100ns latency measurements, chrono's overhead is NOT
//   negligible — it's the same order of magnitude as what you're
//   measuring! RDTSC is the only choice for accurate HFT timing.
//
// SERIALIZING FENCE (_mm_lfence):
//   Modern CPUs execute instructions OUT-OF-ORDER. Without a fence,
//   the CPU may issue RDTSC before earlier instructions complete,
//   or start later instructions before RDTSC, giving wrong readings.
//   _mm_lfence serializes the load pipeline: all preceding loads
//   complete before RDTSC executes. This is cheaper than MFENCE
//   (which also serializes stores) — correct for timing reads.
//
// HISTOGRAM DESIGN:
//   65,536 uint32_t buckets (one per nanosecond, capped at 65535ns).
//   Total size: 256KB — fits in L2 cache.
//   Using uint32_t instead of uint64_t halves the histogram size,
//   doubling the fraction of it that fits in L1.
// ============================================================
#include <array>
#include <cstdint>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <iomanip>

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(__rdtsc)
#else
#include <x86intrin.h>
#endif

namespace Aquila {

class LatencyProfiler {
public:
    // Constructor calibrates TSC frequency against std::chrono.
    // Spins for ~200ms on construction — only create when bench=true.
    LatencyProfiler() noexcept { calibrate(); }

    // Record the start of a timed region. Store the returned value.
    [[nodiscard]] inline uint64_t start() const noexcept {
        // Serialize: ensure all preceding instructions complete before RDTSC.
        // Without _mm_lfence, out-of-order execution can move RDTSC earlier,
        // making the measured interval appear shorter than it actually is.
        _mm_lfence();
        return __rdtsc();
    }

    // Record the end of a timed region. Adds sample to the histogram.
    inline void stop(uint64_t start_tsc) noexcept {
        _mm_lfence();
        const uint64_t ticks = __rdtsc() - start_tsc;
        // Convert TSC ticks → nanoseconds using calibrated frequency
        const uint64_t ns = (m_tsc_hz > 0)
            ? (ticks * 1'000'000'000ULL) / m_tsc_hz
            : ticks;
        const auto bucket = static_cast<std::size_t>(std::min(ns, (uint64_t)65535));
        ++m_hist[bucket];
        ++m_total_samples;
    }

    // Compute the p-th percentile latency in nanoseconds.
    [[nodiscard]] uint64_t percentile_ns(double p) const noexcept {
        if (m_total_samples == 0) return 0;
        const uint64_t target = static_cast<uint64_t>(
            static_cast<double>(m_total_samples) * p / 100.0) + 1;
        uint64_t cumulative = 0;
        for (std::size_t i = 0; i < m_hist.size(); ++i) {
            cumulative += m_hist[i];
            if (cumulative >= target) return static_cast<uint64_t>(i);
        }
        return 65535;
    }

    [[nodiscard]] double mean_ns() const noexcept {
        if (m_total_samples == 0) return 0.0;
        double total = 0.0;
        for (std::size_t i = 0; i < m_hist.size(); ++i)
            total += static_cast<double>(i) * static_cast<double>(m_hist[i]);
        return total / static_cast<double>(m_total_samples);
    }

    void print_report() const {
        std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        std::cout << "  Aquila TSC Latency Report — "
                  << m_total_samples << " samples\n";
        std::cout << "  TSC Frequency : " << m_tsc_hz / 1'000'000 << " MHz\n";
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "  mean    :  " << std::setw(8) << mean_ns()            << " ns\n";
        std::cout << "  p50     :  " << std::setw(8) << percentile_ns(50.0)  << " ns\n";
        std::cout << "  p90     :  " << std::setw(8) << percentile_ns(90.0)  << " ns\n";
        std::cout << "  p95     :  " << std::setw(8) << percentile_ns(95.0)  << " ns\n";
        std::cout << "  p99     :  " << std::setw(8) << percentile_ns(99.0)  << " ns\n";
        std::cout << "  p99.9   :  " << std::setw(8) << percentile_ns(99.9)  << " ns\n";
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    }

    [[nodiscard]] uint64_t tsc_freq_hz()    const noexcept { return m_tsc_hz; }
    [[nodiscard]] uint64_t total_samples()  const noexcept { return m_total_samples; }

private:
    void calibrate() noexcept {
        // Measure TSC ticks over a known wall-clock interval.
        // 200ms gives a stable estimate (±0.5% typical jitter).
        using namespace std::chrono;
        const auto t0 = steady_clock::now();
        const auto r0 = __rdtsc();
        while (duration_cast<milliseconds>(steady_clock::now() - t0).count() < 200) {}
        const auto r1 = __rdtsc();
        const auto t1 = steady_clock::now();
        const auto elapsed_ns = duration_cast<nanoseconds>(t1 - t0).count();
        if (elapsed_ns > 0)
            m_tsc_hz = (r1 - r0) * 1'000'000'000ULL / static_cast<uint64_t>(elapsed_ns);
    }

    uint64_t m_tsc_hz       = 0;
    uint64_t m_total_samples = 0;
    // 65536 * 4 bytes = 256KB histogram, fits in L2 cache
    alignas(64) std::array<uint32_t, 65536> m_hist{};
};

} // namespace Aquila
