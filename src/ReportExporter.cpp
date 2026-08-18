#include "ReportExporter.h"
#include <fstream>
#include <iostream>
#include <iomanip>

namespace Aquila {

void ReportExporter::write(const BacktestReport& r, const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "[Report] Cannot open: " << path << "\n";
        return;
    }
    f << std::fixed << std::setprecision(4);
    f << "{\n";
    f << "  \"engine\": \"Aquila v2.0\",\n";
    f << "  \"strategy\": \""          << r.strategy_name    << "\",\n";
    f << "  \"total_bars\": "          << r.total_bars        << ",\n";
    f << "  \"total_trades\": "        << r.total_trades      << ",\n";
    f << "  \"total_return_pct\": "    << r.total_return_pct  << ",\n";
    f << "  \"sharpe_ratio\": "        << r.sharpe_ratio      << ",\n";
    f << "  \"sortino_ratio\": "       << r.sortino_ratio     << ",\n";
    f << "  \"max_drawdown_pct\": "    << r.max_drawdown_pct  << ",\n";
    f << "  \"calmar_ratio\": "        << r.calmar_ratio      << ",\n";
    f << "  \"win_rate_pct\": "        << r.win_rate_pct      << ",\n";
    f << "  \"profit_factor\": "       << r.profit_factor     << ",\n";
    f << "  \"lat_mean_ns\": "         << r.lat_mean_ns       << ",\n";
    f << "  \"lat_p50_ns\": "          << r.lat_p50_ns        << ",\n";
    f << "  \"lat_p95_ns\": "          << r.lat_p95_ns        << ",\n";
    f << "  \"lat_p99_ns\": "          << r.lat_p99_ns        << ",\n";

    // Sample up to 200 equity curve points to keep JSON compact
    f << "  \"equity_curve_sample\": [";
    const auto& eq   = r.equity_curve;
    const std::size_t step = std::max((std::size_t)1, eq.size() / 200);
    bool first = true;
    for (std::size_t i = 0; i < eq.size(); i += step) {
        if (!first) f << ", ";
        f << eq[i];
        first = false;
    }
    f << "]\n}\n";

    std::cout << "[Report] Written → " << path << "\n";
}

void ReportExporter::print_summary(const BacktestReport& r) {
    const auto W = std::setw(12);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n┌─────────────────────────────────────────────┐\n";
    std::cout << "│  BACKTEST RESULTS  ·  " << r.strategy_name;
    std::cout << "\n├─────────────────────────────────────────────┤\n";
    std::cout << "│  Total Bars      " << W << r.total_bars        << "              │\n";
    std::cout << "│  Total Trades    " << W << r.total_trades      << "              │\n";
    std::cout << "│  Total Return    " << W << r.total_return_pct  << " %            │\n";
    std::cout << "│  Sharpe Ratio    " << W << r.sharpe_ratio      << "              │\n";
    std::cout << "│  Sortino Ratio   " << W << r.sortino_ratio     << "              │\n";
    std::cout << "│  Max Drawdown    " << W << r.max_drawdown_pct  << " %            │\n";
    std::cout << "│  Calmar Ratio    " << W << r.calmar_ratio      << "              │\n";
    std::cout << "│  Win Rate        " << W << r.win_rate_pct      << " %            │\n";
    std::cout << "│  Profit Factor   " << W << r.profit_factor     << "              │\n";
    if (r.lat_p50_ns > 0) {
        std::cout << "├─────────────────────────────────────────────┤\n";
        std::cout << "│  Latency p50     " << W << r.lat_p50_ns << " ns            │\n";
        std::cout << "│  Latency p95     " << W << r.lat_p95_ns << " ns            │\n";
        std::cout << "│  Latency p99     " << W << r.lat_p99_ns << " ns            │\n";
    }
    std::cout << "└─────────────────────────────────────────────┘\n";
}

} // namespace Aquila
