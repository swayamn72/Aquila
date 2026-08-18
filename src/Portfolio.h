#pragma once
// ============================================================
// Portfolio.h — Position Tracking and Order Generation
//
// Turns a SignalEvent into an OrderEvent using fixed fractional
// position sizing: risk_fraction * equity / price = shares.
// Tracks cash, positions, and equity after each fill.
// ============================================================
#include "Event.h"
#include "RingBuffer.h"
#include "EventArena.h"
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace Aquila {

struct Position {
    double   quantity  = 0.0;
    double   avg_cost  = 0.0;
};

class Portfolio {
public:
    explicit Portfolio(double initial_capital, double risk_fraction = 0.02)
        : m_cash(initial_capital)
        , m_initial_capital(initial_capital)
        , m_risk_fraction(risk_fraction)
    {}

    void set_ring(SPSCRingBuffer<Event*, 4096>* ring) noexcept { m_ring  = ring; }
    void set_arena(EventArena* arena)                noexcept { m_arena = arena; }

    void on_signal(const SignalEvent& sig);
    void on_fill(const FillEvent& fill);

    // Update last-known price for unrealized P&L calculation
    void update_price(uint64_t id, double price) noexcept { m_last_price[id] = price; }

    [[nodiscard]] double current_equity()     const noexcept;
    [[nodiscard]] double total_return_pct()   const noexcept;
    [[nodiscard]] double realized_pnl()       const noexcept { return m_realized_pnl; }
    [[nodiscard]] int    total_trades()       const noexcept { return m_total_trades; }
    [[nodiscard]] const std::vector<double>& equity_curve() const noexcept { return m_equity_curve; }

private:
    double m_cash;
    double m_initial_capital;
    double m_risk_fraction;
    double m_realized_pnl = 0.0;
    int    m_total_trades = 0;

    std::unordered_map<uint64_t, Position> m_positions;
    std::unordered_map<uint64_t, double>   m_last_price;
    std::vector<double>                    m_equity_curve;

    SPSCRingBuffer<Event*, 4096>* m_ring  = nullptr;
    EventArena*                   m_arena = nullptr;
};

} // namespace Aquila
