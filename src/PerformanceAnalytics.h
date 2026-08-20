#pragma once
// ============================================================
// PerformanceAnalytics.h — Quantitative Performance Metrics
//
// Metrics computed from an equity curve (portfolio value over time):
//
//   Sharpe Ratio  = (mean_excess_return / std_dev) * sqrt(bars_per_year)
//                   measures return per unit of TOTAL risk
//
//   Sortino Ratio = (mean_excess_return / downside_dev) * sqrt(bars_per_year)
//                   measures return per unit of DOWNSIDE risk only
//                   (doesn't penalize upside volatility — more appropriate
//                   for asymmetric return distributions)
//
//   Max Drawdown  = max peak-to-trough decline as % of peak equity
//                   the single most important risk metric for live trading
//
//   Calmar Ratio  = CAGR / |Max Drawdown|
//                   how much annual return per unit of drawdown risk
//
//   Profit Factor = gross_profit / gross_loss
//                   > 1.0 means the strategy makes money overall
//
//   Win Rate      = winning_trades / total_trades * 100
// ============================================================
#include <vector>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Aquila {

struct TradeRecord {
    std::size_t bar_index;
    bool is_buy;
    double price;
    double qty;
};

struct BacktestReport {
    std::string strategy_name;
    std::size_t total_bars   = 0;
    int         total_trades = 0;

    std::vector<TradeRecord> trades;

    // Quant metrics
    double total_return_pct = 0.0;
    double sharpe_ratio     = 0.0;
    double sortino_ratio    = 0.0;
    double max_drawdown_pct = 0.0; // negative value (e.g., -12.4)
    double calmar_ratio     = 0.0;
    double win_rate_pct     = 0.0;
    double profit_factor    = 0.0;

    // Latency (populated when bench=true)
    uint64_t lat_mean_ns = 0;
    uint64_t lat_p50_ns  = 0;
    uint64_t lat_p95_ns  = 0;
    uint64_t lat_p99_ns  = 0;

    std::vector<double> equity_curve;
};

class PerformanceAnalytics {
public:
    // Compute all metrics from a sequence of equity snapshots.
    // equity_curve[i] = portfolio value at end of bar i.
    // risk_free_annual = annualized risk-free rate (default 5%)
    // bars_per_year    = 252 for daily bars, 252*6.5*60 for minute bars
    static BacktestReport compute(
        const std::vector<double>& equity_curve,
        double risk_free_annual = 0.05,
        int    bars_per_year   = 252
    );

private:
    static double sharpe (const std::vector<double>& returns,
                          double rf_annual, int bpy);
    static double sortino(const std::vector<double>& returns,
                          double rf_annual, int bpy);
    static double max_drawdown(const std::vector<double>& equity);
};

} // namespace Aquila
