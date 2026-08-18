#include "SMAStrategy.h"
#include <iostream>

namespace Aquila {

void SMAStrategy::on_market_data_impl(const MarketEvent& event) {
    const double price = event.data.close;

    // ── O(1) running sum update ───────────────────────────
    m_short_sum += price;
    m_long_sum  += price;
    m_prices.push_back(price);

    // Subtract the price that just fell out of the short window.
    // Index: prices.size()-1-short_window = the element that was
    // at position [short_window] before this tick was added.
    if (m_prices.size() > m_short_window) {
        m_short_sum -= m_prices[m_prices.size() - 1 - m_short_window];
    }

    // Evict the oldest price from the long window (and its sum).
    // O(1) pop_front on deque — no reallocation.
    if (m_prices.size() > m_long_window) {
        m_long_sum -= m_prices.front();
        m_prices.pop_front();
    }

    // Not enough data to compute both SMAs yet
    if (m_prices.size() < m_long_window) return;

    const double short_sma = m_short_sum / static_cast<double>(m_short_window);
    const double long_sma  = m_long_sum  / static_cast<double>(m_long_window);

    // ── Crossover signals ─────────────────────────────────
    if (short_sma > long_sma && !m_currently_long) {
        m_currently_long = true;
        emit_signal(event.data.instrument_id, event.data.timestamp,
                    SignalDirection::LONG, event.data.close);
        std::cout << "[SMA] BUY  @ " << event.data.close
                  << "  (short=" << short_sma << "  long=" << long_sma << ")\n";
    }
    else if (short_sma < long_sma && m_currently_long) {
        m_currently_long = false;
        emit_signal(event.data.instrument_id, event.data.timestamp,
                    SignalDirection::EXIT, event.data.close);
        std::cout << "[SMA] SELL @ " << event.data.close
                  << "  (short=" << short_sma << "  long=" << long_sma << ")\n";
    }
}

} // namespace Aquila
