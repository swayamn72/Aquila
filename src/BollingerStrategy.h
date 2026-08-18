#pragma once
// ============================================================
// BollingerStrategy.h — Bollinger Band Mean-Reversion (CRTP)
//
// THEORY:
//   Bollinger Bands place statistical bounds around a rolling mean:
//     Upper Band = μ + k·σ  (price is statistically "expensive")
//     Lower Band = μ - k·σ  (price is statistically "cheap")
//   where μ = 20-bar rolling mean, σ = 20-bar rolling std deviation.
//
//   Mean reversion hypothesis: prices that deviate far from μ tend
//   to revert back — so we buy when price is below lower band and
//   exit when it crosses back above the mean.
//
// KEY IMPLEMENTATION — WELFORD'S ONLINE ALGORITHM:
//   Naive σ computation: sum all N squared deviations each tick = O(N).
//   Welford's algorithm: maintains running mean and M2 (sum of squared
//   deviations from running mean) in O(1) per tick.
//
//   Update rules (adding price x):
//     delta  = x - mean
//     mean  += delta / n            ← mean updated first
//     delta2 = x - mean             ← delta2 uses the NEW mean
//     M2    += delta * delta2
//     σ²    = M2 / (n - 1)          ← sample variance (Bessel's correction)
//
//   Reverse update (removing oldest price x_old from window):
//     Algebraically equivalent O(1) removal using the same deltas.
//     This avoids recomputing from scratch on every window slide.
//
//   WHY WELFORD'S vs. two-pass?
//     Two-pass: compute mean, then sum (x_i - mean)^2.
//     Problem: catastrophic cancellation for large, close-valued prices.
//     E.g., prices around 10000.00 — sum of squares overflows double precision.
//     Welford's: computes variance from deviations, not raw squares.
//     Numerically stable for any price magnitude. Used in scientific computing.
// ============================================================
#include "Strategy.h"
#include <deque>
#include <cmath>
#include <cstddef>

namespace Aquila {

class BollingerStrategy : public Strategy<BollingerStrategy> {
    friend class Strategy<BollingerStrategy>;

public:
    // window: lookback period for mean and std dev (default 20 bars)
    // k:      number of standard deviations for the bands (default 2.0)
    BollingerStrategy(std::size_t window = 20, double k = 2.0)
        : m_window(window), m_k(k) {}

    [[nodiscard]] const char* name() const noexcept { return "BollingerStrategy"; }

private:
    void on_market_data_impl(const MarketEvent& event);
    void on_fill_impl(const FillEvent&) noexcept {}

    std::size_t m_window;
    double      m_k;

    // Welford's algorithm state
    std::deque<double> m_prices; // sliding window for reverse update
    double m_mean        = 0.0;  // running mean
    double m_M2          = 0.0;  // running sum of squared deviations
    bool   m_in_position = false;
};

} // namespace Aquila
