#include "ExecutionSimulator.h"
#include <algorithm>
#include <iostream>

namespace Aquila {

void ExecutionSimulator::on_order(const OrderEvent& order) {
    if (!m_ring || !m_arena) return;

    // ── Slippage model ────────────────────────────────────
    // Basis points: 1 bps = 0.01% = 1/10000
    const double slip  = m_cfg.slippage_bps / 10'000.0;
    double fill_price  = m_bar.close;
    if (order.side == OrderSide::BUY)
        fill_price *= (1.0 + slip);   // buyer pays more (market impact)
    else
        fill_price *= (1.0 - slip);   // seller receives less

    // ── Partial fill model ────────────────────────────────
    double qty = order.quantity;
    if (m_cfg.partial_fills && m_bar.volume > 0) {
        const double max_fill = static_cast<double>(m_bar.volume) * 0.10;
        qty = std::min(qty, max_fill);
    }
    if (qty <= 0.0) return; // Nothing to fill

    // ── Commission model (IB-style) ───────────────────────
    const double notional   = qty * fill_price;
    const double commission = std::max(m_cfg.min_commission,
                                       notional * m_cfg.commission_rate);

    // ── Emit fill back into the ring buffer ───────────────
    auto* fill = m_arena->alloc<FillEvent>(
        order.instrument_id, order.timestamp,
        order.side, qty, fill_price, commission);

    if (!m_ring->push(fill)) {
        m_arena->free_event(fill);
        std::cerr << "[ExecSim] Ring full — fill dropped\n";
    }
}

} // namespace Aquila
