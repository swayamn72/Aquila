#include "PerformanceAnalytics.h"
#include <cmath>
#include <numeric>
#include <algorithm>

namespace Aquila {

BacktestReport PerformanceAnalytics::compute(
    const std::vector<double>& equity_curve,
    double risk_free_annual,
    int bars_per_year)
{
    BacktestReport r;
    r.equity_curve = equity_curve;
    r.total_bars   = equity_curve.size();
    if (equity_curve.size() < 2) return r;

    // ── Compute bar returns ────────────────────────────────
    std::vector<double> returns;
    returns.reserve(equity_curve.size() - 1);
    for (std::size_t i = 1; i < equity_curve.size(); ++i) {
        if (equity_curve[i - 1] != 0.0)
            returns.push_back(
                (equity_curve[i] - equity_curve[i - 1]) / equity_curve[i - 1]);
    }

    // ── Total Return ──────────────────────────────────────
    r.total_return_pct = (equity_curve.front() != 0.0)
        ? (equity_curve.back() - equity_curve.front()) / equity_curve.front() * 100.0
        : 0.0;

    // ── Risk metrics ──────────────────────────────────────
    r.sharpe_ratio     = sharpe(returns, risk_free_annual, bars_per_year);
    r.sortino_ratio    = sortino(returns, risk_free_annual, bars_per_year);
    r.max_drawdown_pct = max_drawdown(equity_curve);

    // ── Calmar Ratio ──────────────────────────────────────
    if (r.max_drawdown_pct != 0.0 && bars_per_year > 0) {
        const double years = static_cast<double>(equity_curve.size())
                           / static_cast<double>(bars_per_year);
        if (years > 0.0 && equity_curve.front() > 0.0) {
            const double cagr = (std::pow(
                equity_curve.back() / equity_curve.front(), 1.0 / years) - 1.0) * 100.0;
            r.calmar_ratio = cagr / std::abs(r.max_drawdown_pct);
        }
    }

    // ── Profit factor and win rate ─────────────────────────
    double gross_profit = 0.0, gross_loss = 0.0;
    int wins = 0;
    for (double ret : returns) {
        if (ret > 0.0) { gross_profit += ret; ++wins; }
        else if (ret < 0.0) { gross_loss += std::abs(ret); }
    }
    r.win_rate_pct  = returns.empty() ? 0.0
        : static_cast<double>(wins) / static_cast<double>(returns.size()) * 100.0;
    r.profit_factor = (gross_loss > 0.0) ? gross_profit / gross_loss : 0.0;

    return r;
}

double PerformanceAnalytics::sharpe(
    const std::vector<double>& returns, double rf_annual, int bpy)
{
    if (returns.empty()) return 0.0;
    const double rf = rf_annual / static_cast<double>(bpy);
    const double n  = static_cast<double>(returns.size());

    double sum_excess = 0.0;
    for (double r : returns) sum_excess += r - rf;
    const double mean_excess = sum_excess / n;

    double sq_sum = 0.0;
    for (double r : returns) {
        const double dev = (r - rf) - mean_excess;
        sq_sum += dev * dev;
    }
    const double std_dev = (n > 1.0) ? std::sqrt(sq_sum / (n - 1.0)) : 0.0;
    return (std_dev > 0.0) ? mean_excess / std_dev * std::sqrt(static_cast<double>(bpy)) : 0.0;
}

double PerformanceAnalytics::sortino(
    const std::vector<double>& returns, double rf_annual, int bpy)
{
    if (returns.empty()) return 0.0;
    const double rf = rf_annual / static_cast<double>(bpy);
    const double n  = static_cast<double>(returns.size());

    double sum_excess = 0.0;
    for (double r : returns) sum_excess += r - rf;
    const double mean_excess = sum_excess / n;

    // Downside deviation: only count bars where return < risk-free rate
    double ds_sq_sum = 0.0;
    int    ds_count  = 0;
    for (double r : returns) {
        if (r < rf) {
            const double dev = r - rf;
            ds_sq_sum += dev * dev;
            ++ds_count;
        }
    }
    const double ds_dev = (ds_count > 0)
        ? std::sqrt(ds_sq_sum / static_cast<double>(ds_count))
        : 0.0;
    return (ds_dev > 0.0) ? mean_excess / ds_dev * std::sqrt(static_cast<double>(bpy)) : 0.0;
}

double PerformanceAnalytics::max_drawdown(const std::vector<double>& equity) {
    if (equity.empty()) return 0.0;
    double peak  = equity[0];
    double max_dd = 0.0;
    for (double e : equity) {
        if (e > peak) peak = e;
        if (peak > 0.0) {
            const double dd = (e - peak) / peak * 100.0;
            if (dd < max_dd) max_dd = dd;
        }
    }
    return max_dd; // negative (drawdown is a loss)
}

} // namespace Aquila
