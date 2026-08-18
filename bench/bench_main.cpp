// ============================================================
// bench/bench_main.cpp — Aquila Micro-Benchmark Suite
//
// Measures the concrete speedup of each systems optimization:
//   1. SPSCRingBuffer throughput  (vs. std::queue baseline)
//   2. EventArena vs. malloc      (allocation throughput)
//   3. CRTP vs. virtual dispatch  (per-call overhead)
//   4. SoA vs. AoS SMA computation(cache bandwidth efficiency)
//
// Timing: Direct RDTSC (not the histogram profiler — that's for
//         per-event measurements in the engine, not loop timings).
//         We measure total elapsed TSC ticks for N iterations,
//         then compute ns/op = total_ns / N.
// ============================================================
#include "../src/RingBuffer.h"
#include "../src/EventArena.h"
#include "../src/LatencyProfiler.h"
#include "../src/MarketData.h"
#include "../src/MarketDataStore.h"
#include "../src/Event.h"
#include "../src/Strategy.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <queue>
#include <cstring>
#include <numeric>
#include <cmath>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

// ── TSC helpers (direct RDTSC, not the histogramming profiler) ─
inline uint64_t tsc_now() noexcept {
    _mm_lfence();
    return __rdtsc();
}

// Convert TSC ticks → nanoseconds using a pre-calibrated Hz value
inline double ticks_to_ns(uint64_t ticks, uint64_t hz) noexcept {
    return static_cast<double>(ticks) * 1'000'000'000.0 / static_cast<double>(hz);
}

// Calibrate TSC: spin 200ms against steady_clock
static uint64_t calibrate_tsc() {
    using namespace std::chrono;
    const auto t0 = steady_clock::now();
    const auto r0 = __rdtsc();
    while (duration_cast<milliseconds>(steady_clock::now() - t0).count() < 200) {}
    const auto r1 = __rdtsc();
    const auto t1 = steady_clock::now();
    const uint64_t ns = duration_cast<nanoseconds>(t1 - t0).count();
    return ns > 0 ? (r1 - r0) * 1'000'000'000ULL / ns : 3'000'000'000ULL;
}

// ── Benchmark helpers ─────────────────────────────────────────
static constexpr int WARMUP_ITERS = 100'000;
static constexpr int BENCH_ITERS  = 1'000'000;

// Prevents the compiler from optimizing away computed values.
// MSVC-compatible approach: volatile write to a global sink.
// This is recognized as a side-effect, preventing DCE.
static volatile double g_sink_d = 0.0;
static volatile uint64_t g_sink_u = 0;

template<typename T>
inline void do_not_optimize(T val) {
#if defined(_MSC_VER)
    if constexpr (std::is_floating_point_v<T>)
        g_sink_d = static_cast<double>(val);
    else
        g_sink_u = static_cast<uint64_t>(val);
#else
    asm volatile("": "+r,m"(val) : : "memory");
#endif
}

void print_header(const char* title) {
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "  " << title << "\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
}

void print_result(const char* label, double ns_per_op) {
    const double mops = (ns_per_op > 0.0) ? 1000.0 / ns_per_op : 0.0;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  " << std::setw(30) << std::left << label
              << "  " << std::setw(8) << ns_per_op << " ns/op"
              << "  " << std::setw(8) << mops << " Mops/s\n";
}

// ─────────────────────────────────────────────────────────────
// BENCHMARK 1: SPSCRingBuffer throughput
// ─────────────────────────────────────────────────────────────
void bench_ring_buffer(uint64_t hz) {
    print_header("1. SPSCRingBuffer  vs  std::queue");

    // SPSC Ring Buffer
    {
        Aquila::SPSCRingBuffer<uint64_t, 4096> ring;
        uint64_t sum = 0;
        // Warmup
        for (int i = 0; i < WARMUP_ITERS; ++i) {
            [[maybe_unused]] bool ok1 = ring.push(static_cast<uint64_t>(i));
            uint64_t v = 0;
            [[maybe_unused]] bool ok2 = ring.pop(v);
            sum += v;
        }
        do_not_optimize(sum);

        const uint64_t t0 = tsc_now();
        for (int i = 0; i < BENCH_ITERS; ++i) {
            [[maybe_unused]] bool ok1 = ring.push(static_cast<uint64_t>(i));
            uint64_t v = 0;
            [[maybe_unused]] bool ok2 = ring.pop(v);
            sum += v;
        }
        const uint64_t t1 = tsc_now();
        do_not_optimize(sum);

        const double ns_per_op = ticks_to_ns(t1 - t0, hz) / BENCH_ITERS;
        print_result("SPSCRingBuffer push+pop", ns_per_op);
    }

    // std::queue baseline
    {
        std::queue<uint64_t> q;
        uint64_t sum = 0;
        for (int i = 0; i < WARMUP_ITERS; ++i) {
            q.push(static_cast<uint64_t>(i));
            sum += q.front(); q.pop();
        }
        do_not_optimize(sum);

        const uint64_t t0 = tsc_now();
        for (int i = 0; i < BENCH_ITERS; ++i) {
            q.push(static_cast<uint64_t>(i));
            sum += q.front(); q.pop();
        }
        const uint64_t t1 = tsc_now();
        do_not_optimize(sum);

        const double ns_per_op = ticks_to_ns(t1 - t0, hz) / BENCH_ITERS;
        print_result("std::queue push+pop", ns_per_op);
    }
}

// ─────────────────────────────────────────────────────────────
// BENCHMARK 2: EventArena vs. malloc/free
// ─────────────────────────────────────────────────────────────
void bench_arena(uint64_t hz) {
    print_header("2. EventArena (pmr)  vs  malloc/free");

    // EventArena
    {
        Aquila::EventArena arena;
        volatile double sink = 0;
        // Warmup
        for (int i = 0; i < WARMUP_ITERS; ++i) {
            auto* ev = arena.alloc<Aquila::MarketEvent>(Aquila::MarketData{});
            sink += ev->data.close;
            arena.free_event(ev);
        }

        const uint64_t t0 = tsc_now();
        for (int i = 0; i < BENCH_ITERS; ++i) {
            auto* ev = arena.alloc<Aquila::MarketEvent>(Aquila::MarketData{});
            sink += ev->data.close;
            arena.free_event(ev);
        }
        const uint64_t t1 = tsc_now();
        do_not_optimize(sink);

        print_result("EventArena alloc+free", ticks_to_ns(t1 - t0, hz) / BENCH_ITERS);
    }

    // malloc/free baseline
    {
        volatile double sink = 0;
        for (int i = 0; i < WARMUP_ITERS; ++i) {
            auto* ev = new Aquila::MarketEvent(Aquila::MarketData{});
            sink += ev->data.close;
            delete ev;
        }

        const uint64_t t0 = tsc_now();
        for (int i = 0; i < BENCH_ITERS; ++i) {
            auto* ev = new Aquila::MarketEvent(Aquila::MarketData{});
            sink += ev->data.close;
            delete ev;
        }
        const uint64_t t1 = tsc_now();
        do_not_optimize(sink);

        print_result("new/delete (system alloc)", ticks_to_ns(t1 - t0, hz) / BENCH_ITERS);
    }
}

// ─────────────────────────────────────────────────────────────
// BENCHMARK 3: CRTP vs. virtual dispatch
// ─────────────────────────────────────────────────────────────

struct VirtualBase {
    double state = 0.0;
    virtual ~VirtualBase() = default;
    virtual void on_tick(double price) noexcept = 0;
};
struct VirtualSMA : VirtualBase {
    void on_tick(double price) noexcept override { state += price; }
};

class BenchCRTP : public Aquila::Strategy<BenchCRTP> {
    friend class Aquila::Strategy<BenchCRTP>;
public:
    double state = 0.0;
    [[nodiscard]] const char* name() const noexcept { return "BenchCRTP"; }
private:
    void on_market_data_impl(const Aquila::MarketEvent& e) noexcept {
        state += e.data.close;
    }
    void on_fill_impl(const Aquila::FillEvent&) noexcept {}
};

void bench_dispatch(uint64_t hz) {
    print_header("3. CRTP dispatch  vs  virtual dispatch");

    // Pre-build events so allocation isn't in the hot loop
    std::vector<Aquila::MarketEvent> events;
    events.reserve(BENCH_ITERS);
    for (int i = 0; i < BENCH_ITERS; ++i) {
        Aquila::MarketData md{};
        md.close = static_cast<double>(i) * 0.01;
        events.emplace_back(md);
    }

    // CRTP: accumulate into a volatile global to prevent DCE
    {
        BenchCRTP strategy;
        for (int i = 0; i < WARMUP_ITERS; ++i) {
            strategy.on_market_data(events[i]);
            g_sink_d += strategy.state; // force side-effect
        }

        const uint64_t t0 = tsc_now();
        for (int i = 0; i < BENCH_ITERS; ++i) {
            strategy.on_market_data(events[i]);
        }
        const uint64_t t1 = tsc_now();
        g_sink_d = strategy.state; // read final value after timing

        print_result("CRTP (static dispatch)", ticks_to_ns(t1 - t0, hz) / BENCH_ITERS);
    }

    // Virtual
    {
        VirtualSMA virt;
        VirtualBase* base = &virt;
        for (int i = 0; i < WARMUP_ITERS; ++i) {
            base->on_tick(events[i].data.close);
            g_sink_d += virt.state;
        }

        const uint64_t t0 = tsc_now();
        for (int i = 0; i < BENCH_ITERS; ++i) {
            base->on_tick(events[i].data.close);
        }
        const uint64_t t1 = tsc_now();
        g_sink_d = virt.state;

        print_result("Virtual (indirect dispatch)", ticks_to_ns(t1 - t0, hz) / BENCH_ITERS);
    }
}

// ─────────────────────────────────────────────────────────────
// BENCHMARK 4: SoA vs. AoS SMA computation
// ─────────────────────────────────────────────────────────────
void bench_soa_vs_aos(uint64_t hz) {
    print_header("4. SoA layout  vs  AoS layout  (SMA computation)");

    const std::size_t N      = 1'000'000;
    const std::size_t WINDOW = 20;

    std::vector<Aquila::MarketData> aos;
    aos.reserve(N);
    Aquila::MarketDataStore soa;
    soa.open.reserve(N); soa.high.reserve(N); soa.low.reserve(N);
    soa.close.reserve(N); soa.volume.reserve(N); soa.timestamp.reserve(N);
    soa.count = N;

    for (std::size_t i = 0; i < N; ++i) {
        const double price = 100.0 + static_cast<double>(i % 1000) * 0.01;
        Aquila::MarketData md{};
        md.close = price;
        aos.push_back(md);
        soa.close.push_back(price);
        soa.open.push_back(price); soa.high.push_back(price);
        soa.low.push_back(price);  soa.volume.push_back(1000);
        soa.timestamp.push_back(i);
    }

    // AoS SMA: stride through struct array (8 bytes used per 64-byte load)
    {
        double sum_total = 0.0;
        // Warmup
        for (std::size_t i = WINDOW; i < 10000; ++i) {
            double s = 0.0;
            for (std::size_t j = i - WINDOW; j < i; ++j) s += aos[j].close;
            sum_total += s / WINDOW;
        }
        g_sink_d = sum_total;

        sum_total = 0.0;
        const uint64_t t0 = tsc_now();
        for (std::size_t i = WINDOW; i < N; ++i) {
            double s = 0.0;
            for (std::size_t j = i - WINDOW; j < i; ++j) s += aos[j].close;
            sum_total += s / WINDOW;
        }
        const uint64_t t1 = tsc_now();
        g_sink_d = sum_total; // force the loop to be retained

        print_result("AoS SMA (cache-inefficient)",
                     ticks_to_ns(t1 - t0, hz) / static_cast<double>(N - WINDOW));
    }

    // SoA SMA: contiguous double[] (cache-optimal + SIMD-vectorizable)
    {
        double sum_total = 0.0;
        for (std::size_t i = WINDOW; i < 10000; ++i)
            sum_total += soa.batch_sma(i, WINDOW);
        g_sink_d = sum_total;

        sum_total = 0.0;
        const uint64_t t0 = tsc_now();
        for (std::size_t i = WINDOW; i < N; ++i)
            sum_total += soa.batch_sma(i, WINDOW);
        const uint64_t t1 = tsc_now();
        g_sink_d = sum_total;

        print_result("SoA SMA (SIMD-vectorized)",
                     ticks_to_ns(t1 - t0, hz) / static_cast<double>(N - WINDOW));
    }
}

int main() {
    std::cout << "┌──────────────────────────────────────────────────┐\n";
    std::cout << "│   Aquila Micro-Benchmark Suite                    │\n";
    std::cout << "│   Measures: RingBuf · Arena · CRTP · SoA/AoS     │\n";
    std::cout << "└──────────────────────────────────────────────────┘\n";
    std::cout << "Calibrating TSC frequency (200ms spin)...\n";

    const uint64_t hz = calibrate_tsc();
    std::cout << "TSC frequency: " << hz / 1'000'000 << " MHz\n";
    std::cout << "Iterations per benchmark: " << BENCH_ITERS / 1'000'000 << "M\n";

    bench_ring_buffer(hz);
    bench_arena(hz);
    bench_dispatch(hz);
    bench_soa_vs_aos(hz);

    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "  All benchmarks complete.\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    return 0;
}
