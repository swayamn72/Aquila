#pragma once
#include "PerformanceAnalytics.h"
#include <string>

namespace Aquila {

// Hand-rolled JSON report writer — zero external dependencies.
// Outputs a machine-readable report.json for downstream tooling.
class ReportExporter {
public:
    static void write(const BacktestReport& r, const std::string& path);
    static void print_summary(const BacktestReport& r);
};

} // namespace Aquila
