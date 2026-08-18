#pragma once
// ============================================================
// Event.h — Polymorphic Event Hierarchy for the Ring Buffer
//
// Events are the sole communication mechanism between engine
// components. They flow through SPSCRingBuffer<Event*, N> and
// are allocated by EventArena (zero heap calls in steady state).
//
// NOTE ON VIRTUAL DISPATCH:
//   Event uses a virtual destructor — required for correct cleanup
//   via base pointer. However, this is NOT on the hot path: we
//   always downcast to the concrete type in process_event() before
//   calling free_event<ConcreteType>(), which calls the concrete
//   destructor directly.
//
//   Strategy dispatch (the actual hot path) uses CRTP — see Strategy.h.
//
// EVENT FLOW:
//   MarketEvent → Strategy → SignalEvent
//   SignalEvent → Portfolio → OrderEvent
//   OrderEvent  → ExecutionSimulator → FillEvent
//   FillEvent   → Portfolio (update equity)
// ============================================================
#include "MarketData.h"
#include <cstdint>

namespace Aquila {

enum class EventType : uint8_t {
    MARKET_DATA,
    SIGNAL,
    ORDER,
    FILL
};

// ── Base ─────────────────────────────────────────────────────
struct Event {
    EventType type;
    virtual ~Event() = default;
protected:
    explicit Event(EventType t) noexcept : type(t) {}
};

// ── Market Data Event ─────────────────────────────────────────
struct MarketEvent final : Event {
    MarketData data;
    explicit MarketEvent(const MarketData& md) noexcept
        : Event(EventType::MARKET_DATA), data(md) {}
};

// ── Signal Event ──────────────────────────────────────────────
enum class SignalDirection : uint8_t { LONG, SHORT, EXIT };

struct SignalEvent final : Event {
    uint64_t        instrument_id;
    uint64_t        timestamp;
    SignalDirection direction;
    double          suggested_price;

    SignalEvent(uint64_t inst, uint64_t ts,
                SignalDirection dir, double price = 0.0) noexcept
        : Event(EventType::SIGNAL), instrument_id(inst), timestamp(ts),
          direction(dir), suggested_price(price) {}
};

// ── Order Event ───────────────────────────────────────────────
enum class OrderSide : uint8_t { BUY, SELL };
enum class OrderType : uint8_t { MARKET, LIMIT };

struct OrderEvent final : Event {
    uint64_t  instrument_id;
    uint64_t  timestamp;
    OrderSide side;
    OrderType order_type;
    double    quantity;
    double    limit_price; // used only for LIMIT orders

    OrderEvent(uint64_t inst, uint64_t ts, OrderSide s, double qty,
               OrderType ot = OrderType::MARKET, double lp = 0.0) noexcept
        : Event(EventType::ORDER), instrument_id(inst), timestamp(ts),
          side(s), order_type(ot), quantity(qty), limit_price(lp) {}
};

// ── Fill Event ────────────────────────────────────────────────
struct FillEvent final : Event {
    uint64_t  instrument_id;
    uint64_t  timestamp;
    OrderSide side;
    double    quantity_filled;
    double    fill_price;   // close ± slippage
    double    commission;   // broker fee (IB-style)

    FillEvent(uint64_t inst, uint64_t ts, OrderSide s,
              double qty, double price, double comm) noexcept
        : Event(EventType::FILL), instrument_id(inst), timestamp(ts),
          side(s), quantity_filled(qty), fill_price(price), commission(comm) {}
};

} // namespace Aquila
