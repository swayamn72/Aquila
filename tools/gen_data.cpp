// ============================================================
// tools/gen_data.cpp — Geometric Brownian Motion OHLCV Generator
//
// Generates realistic synthetic market data using the GBM model:
//   S(t+1) = S(t) * exp((μ - σ²/2)·Δt  +  σ·√Δt·Z)
//   where Z ~ N(0,1), Δt = 1/252 (one trading day as year fraction)
//
// This is the same model underlying the Black-Scholes options formula.
// Even mentioning "GBM" and "drift/volatility" in the README signals
// quantitative finance awareness to any interviewer.
//
// Usage:
//   gen_data --bars 10000 --seed 42 --sigma 0.02 > data_large.csv
//   gen_data --bars 100000 --seed 7 --mu 0.0002 --start-price 150.0 > spy.csv
// ============================================================
#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <random>
#include <cstdint>
#include <string_view>
#include <unordered_map>

// Minimal inline arg parser for this standalone tool
struct ToolArgs {
    uint64_t    bars        = 10'000;
    uint64_t    seed        = 42;
    double      mu          = 0.0001;   // daily drift (annualized / 252)
    double      sigma       = 0.015;    // daily volatility
    double      start_price = 100.0;
    std::string output      = "";       // empty = stdout
};

ToolArgs parse_args(int argc, char* argv[]) {
    ToolArgs a;
    for (int i = 1; i < argc; ++i) {
        std::string_view k(argv[i]);
        if (k.starts_with("--") && i + 1 < argc) {
            const std::string key(k.substr(2));
            const std::string val(argv[++i]);
            if (key == "bars")        a.bars        = std::stoull(val);
            else if (key == "seed")   a.seed        = std::stoull(val);
            else if (key == "mu")     a.mu          = std::stod(val);
            else if (key == "sigma")  a.sigma       = std::stod(val);
            else if (key == "start-price") a.start_price = std::stod(val);
            else if (key == "output") a.output      = val;
        }
    }
    return a;
}

int main(int argc, char* argv[]) {
    const ToolArgs args = parse_args(argc, argv);

    // ── RNG setup ─────────────────────────────────────────
    // mt19937_64: period 2^19937-1, statistically rigorous,
    // reproducible via --seed (important for backtesting validation)
    std::mt19937_64 rng(args.seed);
    std::normal_distribution<double> Z(0.0, 1.0); // standard normal

    // ── GBM parameters ────────────────────────────────────
    // Δt = 1 trading day as fraction of a year
    const double dt    = 1.0 / 252.0;
    const double drift = (args.mu - 0.5 * args.sigma * args.sigma) * dt;
    const double vol   = args.sigma * std::sqrt(dt);

    // ── Output stream ─────────────────────────────────────
    std::ostream* out = &std::cout;
    std::ofstream file_out;
    if (!args.output.empty()) {
        file_out.open(args.output);
        if (!file_out.is_open()) {
            std::cerr << "Cannot open output file: " << args.output << "\n";
            return 1;
        }
        out = &file_out;
    }

    // ── Write CSV header ──────────────────────────────────
    *out << "timestamp,open,high,low,close,volume\n";

    double  price     = args.start_price;
    uint64_t ts       = 1'700'000'000ULL; // starting UNIX timestamp

    for (uint64_t i = 0; i < args.bars; ++i) {
        const double open = price;

        // GBM step: S(t+1) = S(t) * exp(drift + vol * Z)
        const double z_close = Z(rng);
        const double close   = open * std::exp(drift + vol * z_close);

        // Intrabar high/low: extend beyond open/close by random fraction
        const double z_hl   = std::abs(Z(rng));
        const double range  = std::abs(close - open) + open * args.sigma * 0.3 * z_hl;
        const double high   = std::max(open, close) + range * 0.3;
        const double low    = std::min(open, close) - range * 0.3;

        // Volume: log-normal, correlated with |return| (high vol = high volume)
        const double abs_ret = std::abs(close / open - 1.0);
        const double vol_mean = 1'000'000.0 * (1.0 + 5.0 * abs_ret / args.sigma);
        std::lognormal_distribution<double> vol_dist(std::log(vol_mean), 0.5);
        const uint64_t volume = static_cast<uint64_t>(vol_dist(rng));

        *out << ts    << ","
             << open  << ","
             << high  << ","
             << low   << ","
             << close << ","
             << volume << "\n";

        price = close;
        ts   += 86'400; // next trading day (86400 seconds)
    }

    if (!args.output.empty()) {
        std::cerr << "[gen_data] Wrote " << args.bars << " bars to "
                  << args.output << "\n";
        std::cerr << "[gen_data] Model: GBM  μ=" << args.mu
                  << "  σ=" << args.sigma
                  << "  seed=" << args.seed << "\n";
    }

    return 0;
}
