#pragma once
// ============================================================
// SMAStrategy.h — Simple Moving Average Crossover (CRTP)
//
// Signal logic:
//   short_sma > long_sma AND not in position → BUY (go long)
//   short_sma < long_sma AND in position     → EXIT (close long)
//
// Implementation: O(1) sliding window via running sums.
//   Naive approach: recompute SMA by summing all N prices = O(N).
//   Running sum:    maintain a cumulative sum, subtract the price
//                   leaving the window, add the new price = O(1).
//   On 1M bars with window=30: O(1) saves 29 additions per tick.
//
// CRTP:
//   Inherits from Strategy<SMAStrategy>.
//   on_market_data_impl() is private — only callable via the
//   CRTP base's on_market_data() (friend declaration required).
// ============================================================
#include "Strategy.h"
#include <deque>
#include <cstddef>

namespace Aquila {

class SMAStrategy : public Strategy<SMAStrategy> {
    friend class Strategy<SMAStrategy>; // Allow base to call private impl

public:
    SMAStrategy(std::size_t short_window, std::size_t long_window)
        : m_short_window(short_window)
        , m_long_window(long_window)
    {}

    [[nodiscard]] const char* name() const noexcept { return "SMAStrategy"; }

private:
    // Called by Strategy<SMAStrategy>::on_market_data() — no vtable overhead
    void on_market_data_impl(const MarketEvent& event);

    // SMA strategy doesn't adjust sizing on fills in this implementation
    void on_fill_impl(const FillEvent&) noexcept {}

    std::size_t m_short_window;
    std::size_t m_long_window;

    std::deque<double> m_prices;   // sliding window of closing prices
    bool   m_currently_long = false;
    double m_short_sum      = 0.0; // running sum for short window
    double m_long_sum       = 0.0; // running sum for long window
};

} // namespace Aquila
