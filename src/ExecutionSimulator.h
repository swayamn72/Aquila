#pragma once
// ============================================================
// ExecutionSimulator.h — Realistic Order Fill Simulation
//
// Models three real-world execution costs that separate naive
// backtests from realistic ones:
//
// 1. SLIPPAGE (market impact):
//    A market order moves price against the buyer/seller.
//    We model it as a fixed spread in basis points:
//      BUY  fill = close * (1 + slippage_bps / 10000)
//      SELL fill = close * (1 - slippage_bps / 10000)
//    Typical equity: 1-10 bps. Default: 5 bps.
//    Without slippage, backtests systematically overestimate returns.
//
// 2. COMMISSION (broker fees):
//    Interactive Brokers tiered model:
//      commission = max(min_fee, notional * commission_rate)
//    Typical: 0.1% or $1 minimum per trade. Default: 0.1%.
//
// 3. PARTIAL FILLS (liquidity risk):
//    If order quantity > available volume, only a fraction fills.
//    Model: max_fillable = 10% of bar volume.
//    Default: disabled (too few bars in demo data).
// ============================================================
#include "Event.h"
#include "RingBuffer.h"
#include "EventArena.h"

namespace Aquila {

class ExecutionSimulator {
public:
    struct Config {
        double slippage_bps    = 5.0;    // basis points
        double commission_rate = 0.001;  // fraction of notional
        double min_commission  = 1.0;    // minimum fee in currency units
        bool   partial_fills   = false;
    };

    explicit ExecutionSimulator(Config cfg = {}) : m_cfg(cfg) {}

    void set_ring(SPSCRingBuffer<Event*, 4096>* ring) noexcept { m_ring  = ring; }
    void set_arena(EventArena* arena)                noexcept { m_arena = arena; }

    // Must be called before on_order() with the current bar's data.
    // Used to compute the fill price from close ± slippage.
    void set_current_bar(const MarketData& bar) noexcept { m_bar = bar; }

    void on_order(const OrderEvent& order);

private:
    Config     m_cfg;
    MarketData m_bar{};

    SPSCRingBuffer<Event*, 4096>* m_ring  = nullptr;
    EventArena*                   m_arena = nullptr;
};

} // namespace Aquila
