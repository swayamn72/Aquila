#pragma once
// ============================================================
// EngineConfig.h — Central configuration struct for the engine.
// Passed by value into Engine<StrategyT> at construction time.
// All tuneable parameters live here — no magic numbers in headers.
// ============================================================
#include <string>
#include <cstddef>

namespace Aquila {

struct EngineConfig {
    // ── Data paths ────────────────────────────────────────
    std::string data_path     = "data.csv";
    std::string report_path   = "report.json";
    std::string strategy_name = "sma";

    // ── Strategy parameters ───────────────────────────────
    std::size_t short_window = 10;   // SMA short window
    std::size_t long_window  = 30;   // SMA long window
    std::size_t bb_window    = 20;   // Bollinger Band lookback
    double      bb_k         = 2.0;  // Bollinger Band std-dev multiplier

    // ── Portfolio ─────────────────────────────────────────
    double initial_capital = 100'000.0;
    double risk_fraction   = 0.02;   // 2% of equity risked per trade

    // ── Execution simulation ──────────────────────────────
    double slippage_bps    = 5.0;    // basis points of slippage per fill
    double commission_rate = 0.001;  // 0.1% of notional per trade

    // ── System / profiling ────────────────────────────────
    int  pin_core = -1;    // -1 = no CPU core pinning
    bool bench    = false; // enable TSC latency profiling
    bool verbose  = true;  // print per-signal output
};

} // namespace Aquila
