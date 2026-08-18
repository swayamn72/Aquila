# Aquila — Low-Latency Backtesting Engine

> **Event-driven backtesting engine built around HFT-grade systems primitives.**
> Lock-free ring buffer · CRTP static dispatch · SoA market data · mmap I/O · TSC latency profiling

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![CMake](https://img.shields.io/badge/CMake-3.25%2B-green.svg)](https://cmake.org)
[![Zero Dependencies](https://img.shields.io/badge/dependencies-zero-brightgreen.svg)]()

---

## Architecture

```
CSV File (disk)
    │
    ▼  MappedFile (zero-copy mmap)
CSVReader ──► MarketData[]  ──► MarketDataStore (SoA layout)
                                        │
                                        ▼
                               Engine<StrategyT>  ◄── CRTP, no vtable
                                  │    │    │
                    SPSCRingBuffer │    │    │  EventArena (std::pmr)
                    (lock-free)    │    │    │  (zero heap allocation)
                                   │    │    │
                    ┌──────────────┘    │    └─────────────┐
                    ▼                   ▼                   ▼
               StrategyT          Portfolio          ExecutionSimulator
               (SMA / BB)     (fixed fractional)    (slippage + commission)
                    │               │                       │
                    └───────────────┴───────────────────────┘
                                    │
                                    ▼
                        PerformanceAnalytics
                        (Sharpe · Sortino · MDD · Calmar)
                                    │
                                    ▼
                              report.json
```

---

## Systems Features

| Optimization | Technique | Why It Matters |
|---|---|---|
| **Lock-free event bus** | SPSC ring buffer, `acquire/release` fences | No mutex, no kernel involvement |
| **Cache-line isolation** | `alignas(64)` on ring head/tail | Eliminates false sharing between producer/consumer |
| **Zero heap in hot path** | `std::pmr::unsynchronized_pool_resource` | No system allocator calls during simulation |
| **Static dispatch** | CRTP (no `virtual`) | No vtable lookup; `on_market_data` is inlined |
| **Zero-copy file I/O** | `mmap` / `MapViewOfFile` | File parsed directly from OS page cache |
| **SoA market data** | `struct MarketDataStore` | 8 closes per cache line; SIMD-vectorizable |
| **Huge page backing** | `HugePageAllocator` (2MB pages) | 512× fewer TLB entries for large datasets |
| **Hardware timing** | `__rdtsc` + `_mm_lfence` | 3ns overhead vs 20-50ns for `std::chrono` |
| **CPU core pinning** | `SetThreadAffinityMask` / `pthread_setaffinity_np` | Eliminates L1/L2 cache invalidation from OS migration |
| **Software prefetch** | `_mm_prefetch(_MM_HINT_T0)` | Hides memory latency by prefetching 8 bars ahead |
| **Branch hints** | `[[likely]]` / `[[unlikely]]` | Guides branch predictor for `MARKET_DATA` fast path |
| **Power-of-2 ring** | `(idx & (N-1))` instead of `(idx % N)` | Eliminates integer division from hot path |

---

## Benchmark Results

*(Run `./aquila_bench` after building to fill in your own numbers)*

```
Intel/AMD x64, MSVC 19.44, Release build (/O2), TSC @ 3293 MHz

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  1. SPSCRingBuffer  vs  std::queue
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  SPSCRingBuffer push+pop       1.15 ns/op   871 Mops/s  ← 2.2x faster
  std::queue push+pop           2.57 ns/op   389 Mops/s

  2. EventArena (pmr)  vs  malloc/free
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  EventArena alloc+free        14.72 ns/op    67 Mops/s  ← 2.8x faster
  new/delete (system alloc)    41.41 ns/op    24 Mops/s

  3. CRTP dispatch  vs  virtual dispatch
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  CRTP (static dispatch)        5.16 ns/op   193 Mops/s  ← 1.3x faster
  Virtual (indirect dispatch)   6.66 ns/op   150 Mops/s

  4. SoA layout  vs  AoS layout  (SMA computation, W=20)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  SoA SMA (SIMD-vectorized)     4.76 ns/op   210 Mops/s  ← 1.5x faster
  AoS SMA (cache-inefficient)   7.17 ns/op   139 Mops/s
```

---

## Quick Start

```bash
# 1. Clone and build (Release mode)
git clone <repo>
cd Aquila
cmake --preset release
cmake --build --preset release

# 2. Generate a realistic 10,000-bar dataset (GBM model)
./build/release/gen_data --bars 10000 --seed 42 --sigma 0.02 --output data_large.csv

# 3. Run the engine
./build/release/AquilaEngine --data data_large.csv --strategy sma \
    --short-window 10 --long-window 30 --bench

# 4. Try the Bollinger Band strategy
./build/release/AquilaEngine --data data_large.csv --strategy bollinger \
    --bb-window 20 --bb-k 2.0

# 5. Run benchmarks
./build/release/aquila_bench
```

---

## Strategies

### SMA Crossover (`--strategy sma`)
- **Signal**: `short_sma > long_sma` → BUY; `short_sma < long_sma` → EXIT
- **Implementation**: O(1) sliding window via running sums (no O(N) recompute)

### Bollinger Bands (`--strategy bollinger`)
- **Signal**: `close < lower_band` → BUY; `close > upper_band` or `close ≥ μ` → EXIT
- **Implementation**: **Welford's online algorithm** for numerically stable O(1) rolling σ
  - Avoids catastrophic cancellation that affects the naïve two-pass method
  - Supports reverse (O(1) eviction) updates for sliding window

---

## Design Decisions

### Why CRTP instead of `virtual`?
`virtual on_market_data()` requires a vtable pointer dereference + indirect branch on every tick.
CRTP resolves `on_market_data_impl()` at compile time — the compiler inlines it into the hot loop.
`dumpbin /SYMBOLS` (Windows) or `nm` (Linux) on the release binary shows zero vtable symbols.

### Why `std::pmr` instead of a hand-rolled pool?
`std::pmr::unsynchronized_pool_resource` over a `monotonic_buffer_resource` slab gives us:
- O(1) allocation (pointer bump on fresh slab)
- O(1) per-object reuse (pool free-list)
- Standard library semantics — no custom allocator bugs
- Multiple size classes handled automatically

### Why SoA instead of AoS?
A 1M-bar AoS dataset loads 64 bytes per cache line to access 8 bytes of `close`.
SoA's `close[]` loads 8 closes per cache line — 8× better cache utilization.
With `-O3 -march=native`, the SMA inner loop auto-vectorizes to AVX2 VADDPD (4 doubles/cycle).

### Why `__rdtsc` instead of `std::chrono`?
`steady_clock::now()` costs ~20-50ns (vDSO or syscall).
`__rdtsc + _mm_lfence` costs ~3ns — the serializing fence prevents reordering.
Measuring sub-100ns latency with a 50ns clock is equivalent to measuring a 10cm object with a 5cm ruler.

---

## CLI Reference

```
AquilaEngine [OPTIONS]

DATA:
  --data        <path>    CSV file path       (default: data.csv)
  --report      <path>    JSON output path    (default: report.json)

STRATEGY:
  --strategy    <name>    sma | bollinger     (default: sma)
  --short-window <n>      SMA short window    (default: 10)
  --long-window  <n>      SMA long window     (default: 30)
  --bb-window    <n>      Bollinger window    (default: 20)
  --bb-k         <f>      Bollinger k (σ)     (default: 2.0)

PORTFOLIO:
  --capital     <amount>  Starting capital    (default: 100000)
  --risk-pct    <f>       Risk per trade (%)  (default: 2.0)

EXECUTION:
  --slippage    <bps>     Slippage bps        (default: 5)
  --commission  <rate>    Commission rate     (default: 0.001)

SYSTEM:
  --pin-core    <n>       Pin engine to core  (default: off)
  --bench                 Enable TSC profiling
  --help                  Print this help
```

---

## Output: `report.json`

```json
{
  "engine": "Aquila v2.0",
  "strategy": "SMAStrategy",
  "total_bars": 10000,
  "total_trades": 47,
  "total_return_pct": 12.34,
  "sharpe_ratio": 1.42,
  "sortino_ratio": 1.89,
  "max_drawdown_pct": -8.21,
  "calmar_ratio": 2.11,
  "win_rate_pct": 54.2,
  "profit_factor": 1.67,
  "lat_mean_ns": 138,
  "lat_p50_ns": 112,
  "lat_p95_ns": 287,
  "lat_p99_ns": 891,
  "equity_curve_sample": [100000, 100234, ...]
}
```
