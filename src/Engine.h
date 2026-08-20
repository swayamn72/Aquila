#pragma once
// ============================================================
// Engine.h — CRTP-Templated Backtesting Engine (Full Implementation)
//
// WHY IS THIS A CLASS TEMPLATE?
//   Template member function bodies must be in headers (C++ ODR rule).
//   Because Engine<StrategyT> calls StrategyT::on_market_data(), the
//   compiler must see both definitions together to:
//     1. Inline on_market_data_impl() through the CRTP base
//     2. Optimize the dispatch for each concrete strategy type
//     3. Potentially vectorize the inner event loop
//
//   Instantiation in main.cpp:
//     Engine<SMAStrategy>       → one fully-optimized specialization
//     Engine<BollingerStrategy> → separate, independently optimized code
//   No runtime polymorphism — each path is a different compiled function.
//
// COMPONENT WIRING:
//   All components share the same ring buffer and arena. The Engine
//   is the sole owner and injects pointers at construction time:
//
//     StrategyT ──emit_signal──▶ SPSCRingBuffer ◀──pop── Engine::run()
//     Portfolio ──emit_order──▶ SPSCRingBuffer
//     ExecSim   ──emit_fill──▶  SPSCRingBuffer
//     EventArena ◀── alloc/free ── all components
//
// HOT PATH: Engine::run() / process_event() — annotated with prefetch
// and [[likely]]/[[unlikely]] branch hints.
// ============================================================
#include "Event.h"
#include "Strategy.h"
#include "RingBuffer.h"
#include "EventArena.h"
#include "LatencyProfiler.h"
#include "MarketData.h"
#include "MarketDataStore.h"
#include "CSVReader.h"
#include "Portfolio.h"
#include "ExecutionSimulator.h"
#include "PerformanceAnalytics.h"
#include "ReportExporter.h"
#include "EngineConfig.h"
#include <iostream>
#include <optional>
#include <type_traits>
#include <concepts>
#include <string_view>
#include <cstring>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

namespace Aquila {

template<typename StrategyT>
    // C++20 concept: Engine only compiles if StrategyT correctly uses CRTP.
    // Violating this gives a clear compile error, not a cryptic template failure.
    requires std::is_base_of_v<Strategy<StrategyT>, StrategyT>
class Engine {
public:
    explicit Engine(StrategyT strategy, EngineConfig cfg = {})
        : m_strategy(std::move(strategy))
        , m_portfolio(cfg.initial_capital, cfg.risk_fraction)
        , m_exec_sim(ExecutionSimulator::Config{
            .slippage_bps    = cfg.slippage_bps,
            .commission_rate = cfg.commission_rate
          })
        , m_cfg(std::move(cfg))
    {
        // Wire all components to the shared ring buffer and arena.
        // Injection rather than global state — testable, composable.
        m_strategy.set_event_ring(&m_ring);
        m_strategy.set_arena(&m_arena);
        m_portfolio.set_ring(&m_ring);
        m_portfolio.set_arena(&m_arena);
        m_exec_sim.set_ring(&m_ring);
        m_exec_sim.set_arena(&m_arena);

        // Calibrate the TSC profiler only when bench mode is requested.
        // Calibration spins for 200ms — skip it when not needed.
        if (m_cfg.bench) {
            m_profiler.emplace(); // constructs LatencyProfiler (calibrates TSC)
        }
    }

    void load_data(std::string_view path) {
        std::cout << "[Engine] Loading: " << path << "  (mmap I/O)\n";
        CSVReader reader(path);
        auto aos = reader.read_all();
        // Convert AoS → SoA: one-time O(N) pass, hot path always uses SoA.
        m_store = MarketDataStore::from_aos(aos);
        std::cout << "[Engine] Loaded " << m_store.size()
                  << " bars into SoA layout\n";
    }

    void run() {
        std::cout << "[Engine] Strategy: " << m_strategy.name()
                  << " | Bars: " << m_store.size() << "\n";
        std::cout << "[Engine] Ring buffer capacity: " << m_ring.capacity() << " events\n\n";

        for (std::size_t i = 0; i < m_store.size(); ++i) {

            // ── Software Prefetch ─────────────────────────
            // Ask the CPU to load the cache line for bar (i+8) into
            // L1 cache NOW, while we process bar i.
            // _MM_HINT_T0: prefetch into all cache levels (L1/L2/L3).
            // Without this, the CPU stalls on cache miss (~100ns) when
            // it first accesses close[i+8] several iterations later.
            if (i + 8 < m_store.size()) {
                _mm_prefetch(
                    reinterpret_cast<const char*>(&m_store.close[i + 8]),
                    _MM_HINT_T0);
            }

            // ── Synthesize MarketData from SoA ────────────
            // The event bus uses the AoS MarketData for compatibility.
            // Strategies receive the full bar struct per tick.
            MarketData bar{};
            bar.timestamp     = m_store.timestamp[i];
            bar.open          = m_store.open[i];
            bar.high          = m_store.high[i];
            bar.low           = m_store.low[i];
            bar.close         = m_store.close[i];
            bar.volume        = m_store.volume[i];
            bar.instrument_id = 1;
            bar.symbol[0]     = '\0';

            // Update portfolio's last-known price for unrealized P&L
            m_portfolio.update_price(1, bar.close);
            // ExecutionSimulator uses close to compute fill_price
            m_exec_sim.set_current_bar(bar);

            // ── Push market event into the ring buffer ─────
            auto* me = m_arena.alloc<MarketEvent>(bar);
            // Spin if full: shouldn't happen in single-threaded backtest
            // (ring cap 4096 >> 4 events per bar in flight at once).
            while (!m_ring.push(me)) {}

            // ── Time the full drain loop ───────────────────
            const uint64_t tick_start = m_cfg.bench && m_profiler
                ? m_profiler->start() : 0;

            // ── Drain ring buffer ──────────────────────────
            // This loop processes: MarketData → Signal → Order → Fill
            // New events pushed by handlers are picked up in the same loop.
            Event* ev = nullptr;
            while (m_ring.pop(ev)) {
                process_event(ev);
            }

            if (m_cfg.bench && m_profiler) {
                m_profiler->stop(tick_start);
            }
        }

        std::cout << "\n[Engine] Simulation complete.\n";
    }

    // Build and return the backtest report (call after run()).
    [[nodiscard]] BacktestReport get_report() {
        auto report = PerformanceAnalytics::compute(m_portfolio.equity_curve());
        report.strategy_name = m_strategy.name();
        report.total_bars    = m_store.size();
        report.total_trades  = m_portfolio.total_trades();
        report.total_return_pct = m_portfolio.total_return_pct();
        report.trades        = m_portfolio.trades();

        if (m_cfg.bench && m_profiler) {
            report.lat_mean_ns = static_cast<uint64_t>(m_profiler->mean_ns());
            report.lat_p50_ns  = m_profiler->percentile_ns(50.0);
            report.lat_p95_ns  = m_profiler->percentile_ns(95.0);
            report.lat_p99_ns  = m_profiler->percentile_ns(99.0);
        }
        return report;
    }

    void print_latency_report() const {
        if (m_cfg.bench && m_profiler) m_profiler->print_report();
    }

private:
    // ── Hot path: called once per event ───────────────────
    // [[likely]] / [[unlikely]] hint the branch predictor.
    // 99%+ of events are MARKET_DATA → label it likely,
    // so the CPU's branch predictor generates the fast path first.
    void process_event(Event* ev) {
        switch (ev->type) {
        [[likely]]   case EventType::MARKET_DATA: {
            auto* me = static_cast<MarketEvent*>(ev);
            // CRTP dispatch: resolves to StrategyT::on_market_data_impl().
            // The compiler inlines this at the call site — no vtable.
            m_strategy.on_market_data(*me);
            m_arena.free_event(me);
            break;
        }
        case EventType::SIGNAL: {
            auto* sig = static_cast<SignalEvent*>(ev);
            m_portfolio.on_signal(*sig);
            m_arena.free_event(sig);
            break;
        }
        case EventType::ORDER: {
            auto* ord = static_cast<OrderEvent*>(ev);
            m_exec_sim.on_order(*ord);
            m_arena.free_event(ord);
            break;
        }
        case EventType::FILL: {
            auto* fill = static_cast<FillEvent*>(ev);
            m_portfolio.on_fill(*fill);
            m_strategy.on_fill(*fill);
            m_arena.free_event(fill);
            break;
        }
        [[unlikely]] default:
            // Programming error — unknown event type.
            // Accept the memory leak; don't crash.
            std::cerr << "[Engine] CRITICAL: unknown event type\n";
            break;
        }
    }

    // ── Members ───────────────────────────────────────────
    StrategyT          m_strategy;
    Portfolio          m_portfolio;
    ExecutionSimulator m_exec_sim;
    MarketDataStore    m_store;

    // Ring buffer: 4096 capacity (power of 2). At ~4 events per bar,
    // supports 1024 bars of backlog — more than sufficient for SPSC.
    SPSCRingBuffer<Event*, 4096> m_ring;
    EventArena                   m_arena;

    // Optional: only constructed when bench=true (avoids 200ms calibration)
    std::optional<LatencyProfiler> m_profiler;

    EngineConfig m_cfg;
};

} // namespace Aquila
