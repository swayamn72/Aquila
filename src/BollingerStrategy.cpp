#include "BollingerStrategy.h"
#include <iostream>
#include <iomanip>

namespace Aquila {

void BollingerStrategy::on_market_data_impl(const MarketEvent& event) {
    const double price = event.data.close;

    // ── Welford's Online Update: add new price ─────────────
    // Step 1: compute delta BEFORE updating mean
    const double delta = price - m_mean;
    // Step 2: update mean (n = m_prices.size() + 1 before push_back)
    const double n_new = static_cast<double>(m_prices.size()) + 1.0;
    m_mean += delta / n_new;
    // Step 3: compute delta2 with the UPDATED mean
    const double delta2 = price - m_mean;
    // Step 4: accumulate squared deviation (Welford's M2)
    m_M2 += delta * delta2;
    m_prices.push_back(price);

    // ── Sliding window: remove oldest price via reverse Welford ──
    if (m_prices.size() > m_window) {
        const double old_price = m_prices.front();
        m_prices.pop_front();
        const double n_old = static_cast<double>(m_prices.size()) + 1.0;

        // Reverse Welford's update — algebraically derived from the forward formula.
        // Subtract the contribution of old_price from (mean, M2) in O(1).
        const double old_delta  = old_price - m_mean;
        m_mean -= old_delta / static_cast<double>(m_prices.size());
        const double old_delta2 = old_price - m_mean;
        m_M2   -= old_delta * old_delta2;

        // Guard against floating-point rounding producing tiny negative M2
        if (m_M2 < 0.0) m_M2 = 0.0;
        (void)n_old;
    }

    // Wait for a full window before computing bands
    if (m_prices.size() < m_window) return;

    // ── Compute bands ─────────────────────────────────────
    // Sample variance (Bessel's correction: divide by n-1, not n)
    const double variance = m_M2 / static_cast<double>(m_prices.size() - 1);
    const double sigma    = std::sqrt(variance);
    const double upper    = m_mean + m_k * sigma;
    const double lower    = m_mean - m_k * sigma;

    // ── Signal logic ──────────────────────────────────────
    if (price < lower && !m_in_position) {
        // Price statistically cheap — initiate long position
        m_in_position = true;
        emit_signal(event.data.instrument_id, event.data.timestamp,
                    SignalDirection::LONG, price);
        std::cout << std::fixed << std::setprecision(4)
                  << "[BB] BUY  @ " << price
                  << "  lower=" << lower << "  μ=" << m_mean
                  << "  σ=" << sigma << "\n";
    }
    else if (price > upper && m_in_position) {
        // Price statistically expensive — exit long
        m_in_position = false;
        emit_signal(event.data.instrument_id, event.data.timestamp,
                    SignalDirection::EXIT, price);
        std::cout << "[BB] SELL @ " << price
                  << "  upper=" << upper << "\n";
    }
    else if (price >= m_mean && m_in_position) {
        // Mean reversion complete: price returned to or above μ — exit
        m_in_position = false;
        emit_signal(event.data.instrument_id, event.data.timestamp,
                    SignalDirection::EXIT, price);
        std::cout << "[BB] EXIT @ " << price
                  << "  (reverted to μ=" << m_mean << ")\n";
    }
}

} // namespace Aquila
