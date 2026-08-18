#pragma once
// ArgParser.h — Minimal CLI argument parser, zero external dependencies.
// Supports: --key value (string/int/double) and --flag (boolean).
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <iostream>

namespace Aquila {

class ArgParser {
public:
    ArgParser(int argc, char* argv[]) {
        for (int i = 1; i < argc; ++i) {
            std::string_view arg(argv[i]);
            if (arg.starts_with("--")) {
                const std::string key(arg.substr(2));
                if (i + 1 < argc && !std::string_view(argv[i + 1]).starts_with("--")) {
                    m_args[key] = argv[++i];
                } else {
                    m_flags.insert(key);
                }
            }
        }
    }

    [[nodiscard]] std::string get_str(std::string_view key, std::string_view def = "") const {
        auto it = m_args.find(std::string(key));
        return (it != m_args.end()) ? it->second : std::string(def);
    }
    [[nodiscard]] int get_int(std::string_view key, int def = 0) const {
        auto it = m_args.find(std::string(key));
        return (it != m_args.end()) ? std::stoi(it->second) : def;
    }
    [[nodiscard]] double get_double(std::string_view key, double def = 0.0) const {
        auto it = m_args.find(std::string(key));
        return (it != m_args.end()) ? std::stod(it->second) : def;
    }
    [[nodiscard]] bool get_flag(std::string_view key) const {
        return m_flags.count(std::string(key)) > 0;
    }

    static void print_help() {
        std::cout << R"(
Aquila Low-Latency Backtesting Engine v2.0
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
USAGE: AquilaEngine [OPTIONS]

DATA:
  --data        <path>    CSV file path       (default: data.csv)
  --report      <path>    JSON output path    (default: report.json)

STRATEGY:
  --strategy    <name>    sma | bollinger     (default: sma)
  --short-window <n>      SMA short window    (default: 10)
  --long-window  <n>      SMA long window     (default: 30)
  --bb-window    <n>      Bollinger window    (default: 20)
  --bb-k         <f>      Bollinger k (σ)     (default: 2.0)

PORTFOLIO:
  --capital     <amount>  Starting capital    (default: 100000)
  --risk-pct    <f>       Risk per trade (%)  (default: 2.0)

EXECUTION:
  --slippage    <bps>     Slippage bps        (default: 5)
  --commission  <rate>    Commission rate     (default: 0.001)

SYSTEM:
  --pin-core    <n>       Pin engine to core  (default: off)
  --bench                 Enable TSC profiling
  --help                  Print this help
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
)";
    }

private:
    std::unordered_map<std::string, std::string> m_args;
    std::unordered_set<std::string>              m_flags;
};

} // namespace Aquila
