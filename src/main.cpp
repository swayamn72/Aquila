// ============================================================
// main.cpp — Aquila Engine Entry Point
//
// STRATEGY SELECTION VIA TEMPLATES:
//   run_backtest<SMAStrategy>() and run_backtest<BollingerStrategy>()
//   are DIFFERENT compiled functions — each one has its on_market_data
//   inlined at the call site, specific to that strategy type.
//   The runtime if/else only executes ONCE at startup to choose which
//   function to call. The hot path itself has zero conditional overhead.
// ============================================================
#include "ArgParser.h"
#include "EngineConfig.h"
#include "Engine.h"
#include "SMAStrategy.h"
#include "BollingerStrategy.h"
#include "ThreadAffinity.h"
#include "ReportExporter.h"
#include <iostream>

// Template function: the compiler generates a separate, fully optimized
// binary for each StrategyT instantiation. on_market_data_impl() is
// inlined into the hot loop — no indirect calls, no branch mispredictions.
template<typename StrategyT>
void run_backtest(StrategyT strategy, const Aquila::EngineConfig& cfg) {
    Aquila::Engine<StrategyT> engine(std::move(strategy), cfg);
    engine.load_data(cfg.data_path);
    engine.run();

    auto report = engine.get_report();
    Aquila::ReportExporter::write(report, cfg.report_path);
    Aquila::ReportExporter::print_summary(report);

    if (cfg.bench) {
        engine.print_latency_report();
    }
}

int main(int argc, char* argv[]) {
    Aquila::ArgParser args(argc, argv);

    if (args.get_flag("help")) {
        Aquila::ArgParser::print_help();
        return 0;
    }

    std::cout << "┌────────────────────────────────────────────┐\n"
              << "│   Aquila  ·  Low-Latency Engine  v2.0      │\n"
              << "│   Lock-Free · CRTP · mmap · SoA · TSC      │\n"
              << "└────────────────────────────────────────────┘\n\n";

    // ── Parse configuration ───────────────────────────────
    Aquila::EngineConfig cfg;
    cfg.data_path       = args.get_str("data",         "data.csv");
    cfg.report_path     = args.get_str("report",       "report.json");
    cfg.strategy_name   = args.get_str("strategy",     "sma");
    cfg.short_window    = static_cast<std::size_t>(args.get_int("short-window", 10));
    cfg.long_window     = static_cast<std::size_t>(args.get_int("long-window",  30));
    cfg.bb_window       = static_cast<std::size_t>(args.get_int("bb-window",    20));
    cfg.bb_k            = args.get_double("bb-k",       2.0);
    cfg.initial_capital = args.get_double("capital",    100'000.0);
    cfg.risk_fraction   = args.get_double("risk-pct",   2.0) / 100.0;
    cfg.slippage_bps    = args.get_double("slippage",   5.0);
    cfg.commission_rate = args.get_double("commission", 0.001);
    cfg.pin_core        = args.get_int("pin-core",     -1);
    cfg.bench           = args.get_flag("bench");

    // ── CPU affinity pinning ──────────────────────────────
    if (cfg.pin_core >= 0) {
        if (Aquila::sys::pin_to_core(cfg.pin_core)) {
            std::cout << "[sys] Engine pinned to core " << cfg.pin_core
                      << " / " << Aquila::sys::logical_core_count()
                      << " logical cores\n";
        } else {
            std::cerr << "[sys] Warning: core pinning failed\n";
        }
    }

    // ── Strategy dispatch ─────────────────────────────────
    // Each branch instantiates Engine<T> with a DIFFERENT T.
    // The compiler emits completely separate machine code for each.
    // Zero runtime overhead after this single conditional.
    if (cfg.strategy_name == "bollinger") {
        run_backtest(Aquila::BollingerStrategy(cfg.bb_window, cfg.bb_k), cfg);
    } else {
        run_backtest(Aquila::SMAStrategy(cfg.short_window, cfg.long_window), cfg);
    }

    return 0;
}
