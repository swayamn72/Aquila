#include "Portfolio.h"
#include <iostream>
#include <iomanip>

namespace Aquila {

double Portfolio::current_equity() const noexcept {
    double equity = m_cash;
    for (const auto& [id, pos] : m_positions) {
        auto it = m_last_price.find(id);
        if (it != m_last_price.end())
            equity += pos.quantity * it->second;
    }
    return equity;
}

double Portfolio::total_return_pct() const noexcept {
    if (m_initial_capital == 0.0) return 0.0;
    return (current_equity() - m_initial_capital) / m_initial_capital * 100.0;
}

void Portfolio::on_signal(const SignalEvent& sig) {
    if (!m_ring || !m_arena) return;

    const double equity = current_equity();
    auto it = m_positions.find(sig.instrument_id);
    const bool has_position = (it != m_positions.end() && it->second.quantity > 0.0);

    if (sig.direction == SignalDirection::LONG && !has_position) {
        // Fixed fractional sizing: risk (risk_fraction * equity) on this trade
        const double risk_amount = equity * m_risk_fraction;
        const double qty = (sig.suggested_price > 0.0)
            ? risk_amount / sig.suggested_price
            : 0.0;

        // Refuse order if insufficient cash
        if (qty <= 0.0 || m_cash < qty * sig.suggested_price) return;

        auto* order = m_arena->alloc<OrderEvent>(
            sig.instrument_id, sig.timestamp, OrderSide::BUY, qty);
        if (!m_ring->push(order)) m_arena->free_event(order);
    }
    else if ((sig.direction == SignalDirection::EXIT ||
              sig.direction == SignalDirection::SHORT) && has_position) {
        const double qty = it->second.quantity;
        if (qty <= 0.0) return;

        auto* order = m_arena->alloc<OrderEvent>(
            sig.instrument_id, sig.timestamp, OrderSide::SELL, qty);
        if (!m_ring->push(order)) m_arena->free_event(order);
    }
}

void Portfolio::on_fill(const FillEvent& fill) {
    auto& pos = m_positions[fill.instrument_id];

    if (fill.side == OrderSide::BUY) {
        // Update average cost basis using weighted average
        const double total_cost = pos.quantity * pos.avg_cost
                                + fill.quantity_filled * fill.fill_price;
        pos.quantity += fill.quantity_filled;
        pos.avg_cost  = (pos.quantity > 0.0) ? total_cost / pos.quantity : 0.0;
        m_cash       -= fill.quantity_filled * fill.fill_price + fill.commission;
    }
    else { // SELL
        const double proceeds = fill.quantity_filled * fill.fill_price - fill.commission;
        m_realized_pnl       += proceeds - fill.quantity_filled * pos.avg_cost;
        pos.quantity         -= fill.quantity_filled;
        m_cash               += proceeds;
        if (pos.quantity <= 0.0) { pos.quantity = 0.0; pos.avg_cost = 0.0; }
        ++m_total_trades;
    }
    const double eq = current_equity();
    m_equity_curve.push_back(eq);

    m_trades.push_back({
        m_equity_curve.size() - 1, // index in the full equity curve
        (fill.side == OrderSide::BUY),
        fill.fill_price,
        fill.quantity_filled
    });

    std::cout << std::fixed << std::setprecision(2)
              << "[Portfolio] " << (fill.side == OrderSide::BUY ? "BUY " : "SELL")
              << " qty=" << fill.quantity_filled
              << " @ " << fill.fill_price
              << "  comm=" << fill.commission
              << "  equity=" << eq << "\n";
}

} // namespace Aquila
