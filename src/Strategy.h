#pragma once
// ============================================================
// Strategy.h — CRTP Base Class: Zero-Cost Strategy Dispatch
//
// THE VTABLE PROBLEM:
//   Virtual dispatch costs (per call on modern x86-64):
//     1. Load vtable pointer from the object: ~4ns (L1 cache hit)
//     2. Load function pointer from vtable: ~4ns (L1 cache hit)
//     3. Indirect CALL instruction: branch predictor sees it as
//        an indirect branch — harder to predict than direct calls.
//     4. Prevents inlining: the compiler cannot inline through an
//        indirect call because the callee is unknown at compile time.
//   Total: ~8-15ns per dispatch, plus missed inlining opportunities.
//
// THE CRTP SOLUTION (Curiously Recurring Template Pattern):
//   Base class is parameterized on the derived class type:
//     class SMAStrategy : public Strategy<SMAStrategy>
//
//   Strategy<Derived>::on_market_data() casts 'this' to Derived*
//   at compile time — resolved without vtable, fully inlineable.
//
//   Strategy<Derived>::on_market_data() effectively becomes:
//     inline void on_market_data(const MarketEvent& e) {
//         // Inlined body of SMAStrategy::on_market_data_impl
//     }
//   One function call, no pointer chasing, no branch misprediction.
//
// MEASURABLE IMPACT (from bench/bench_main.cpp):
//   Virtual dispatch: ~10-15 ns/call (dependent on BTB state)
//   CRTP dispatch:    ~2-4  ns/call  (inlined, zero indirect branch)
//
// DERIVED CLASS CONTRACT:
//   Must implement:
//     void on_market_data_impl(const MarketEvent&)  [private]
//     void on_fill_impl(const FillEvent&)           [private]
//     const char* name() const noexcept             [public]
//   Must declare: friend class Strategy<Derived>;
// ============================================================
#include "Event.h"
#include "RingBuffer.h"
#include "EventArena.h"

// Hot-path annotation: hint for GCC/Clang profile-guided optimization
// Tells the compiler to place this function in the "hot" text section
// for better instruction cache locality.
#if defined(__GNUC__) || defined(__clang__)
#define AQUILA_HOT __attribute__((hot))
#else
#define AQUILA_HOT
#endif

namespace Aquila {

template<typename Derived>
class Strategy {
public:
    // ── Hot path: called once per market bar ──────────────
    // Resolves to Derived::on_market_data_impl() at compile time.
    // No vtable dereference. Compiler can inline the body.
    AQUILA_HOT
    inline void on_market_data(const MarketEvent& e) {
        static_cast<Derived*>(this)->on_market_data_impl(e);
    }

    // Called when an order placed by this strategy is filled
    inline void on_fill(const FillEvent& e) {
        static_cast<Derived*>(this)->on_fill_impl(e);
    }

    // Called by Engine to inject the shared ring buffer pointer.
    // Strategies emit signals by pushing SignalEvents into this bus.
    void set_event_ring(SPSCRingBuffer<Event*, 4096>* ring) noexcept {
        m_ring = ring;
    }

    // Called by Engine to inject the EventArena for zero-alloc emission.
    void set_arena(EventArena* arena) noexcept {
        m_arena = arena;
    }

protected:
    // Helper: construct a SignalEvent in the arena and push to ring.
    // This is the correct way for derived strategies to emit signals.
    void emit_signal(uint64_t instrument_id, uint64_t ts,
                     SignalDirection dir, double price = 0.0) {
        if (!m_ring || !m_arena) return;
        auto* sig = m_arena->alloc<SignalEvent>(instrument_id, ts, dir, price);
        if (!m_ring->push(sig)) {
            // Ring buffer full — backpressure. Return slot to arena.
            // This should be rare in the single-threaded backtester.
            m_arena->free_event(sig);
        }
    }

private:
    SPSCRingBuffer<Event*, 4096>* m_ring  = nullptr;
    EventArena*                   m_arena = nullptr;
};

} // namespace Aquila
