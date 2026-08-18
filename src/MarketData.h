#pragma once
// ============================================================
// MarketData.h — Cache-Line-Aligned OHLCV Tick Structure
//
// CACHE LINE DESIGN:
//   A CPU cache line is 64 bytes on all modern x86-64 processors.
//   If a struct straddles two cache lines, a single access requires
//   two cache line loads — doubling memory bandwidth for that access.
//
//   alignas(64): guarantees the struct starts at a 64-byte boundary.
//   sizeof == 64: guarantees it fits within exactly one cache line.
//
//   The static_assert below fails at compile time if anyone adds a
//   field without adjusting the layout — enforcing the constraint
//   programmatically rather than relying on documentation.
//
// FIELD LAYOUT (8 fields × 8 bytes = 64 bytes):
//   timestamp    [0-7]    UNIX epoch, seconds or milliseconds
//   open         [8-15]   opening price (double)
//   high         [16-23]  session high (double)
//   low          [24-31]  session low (double)
//   close        [32-39]  closing price — the hot field (double)
//   volume       [40-47]  shares/contracts traded (uint64_t)
//   instrument_id[48-55]  integer ticker identifier (uint64_t)
//   symbol       [56-63]  null-terminated 8-char ticker string
//                          (zero heap allocation — no std::string)
// ============================================================
#include <cstdint>
#include <cstring>

namespace Aquila {

struct alignas(64) MarketData {
    uint64_t timestamp;       // 8 bytes
    double   open;            // 8 bytes
    double   high;            // 8 bytes
    double   low;             // 8 bytes
    double   close;           // 8 bytes  ← hot field (SMA, BB read this)
    uint64_t volume;          // 8 bytes
    uint64_t instrument_id;   // 8 bytes
    char     symbol[8];       // 8 bytes  ← fixed-size, zero heap allocation
    // Total: 64 bytes — exactly one cache line
};

static_assert(sizeof(MarketData)  == 64,
    "MarketData must be exactly 64 bytes. "
    "Adding a field? Remove symbol padding or split the struct.");

static_assert(alignof(MarketData) == 64,
    "MarketData alignment must be 64 bytes (one cache line).");

} // namespace Aquila
