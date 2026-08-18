#pragma once
// ============================================================
// RingBuffer.h — Lock-Free Single-Producer Single-Consumer Queue
//
// WHY NOT std::queue?
//   std::queue (backed by std::deque) allocates memory on every push,
//   and its internal mutex blocks the CPU for microseconds. In a
//   latency-sensitive loop processing 1M events, that is catastrophic.
//
// THIS IMPLEMENTATION:
//   Zero allocation in steady state — fixed array, no heap.
//   Zero kernel involvement — atomic loads/stores only, no mutexes.
//
// KEY DESIGN DECISIONS:
//
//   1. SEPARATE CACHE LINES for head and tail:
//      alignas(64) on m_write and m_read ensures they live on
//      different 64-byte cache lines. Without this, both producer
//      and consumer access the same cache line — causing MESI-protocol
//      "coherence traffic" on every push/pop even when the ring is
//      neither full nor empty. This is called FALSE SHARING.
//
//   2. POWER-OF-2 CAPACITY (enforced by C++20 `requires`):
//      Index wrapping via (idx & MASK) instead of (idx % N).
//      Integer division takes 20-80 cycles; bitwise AND takes 1.
//      On 1M iterations, that's ~60ms saved.
//
//   3. ACQUIRE/RELEASE FENCES (not seq_cst):
//      memory_order_seq_cst (the C++ default) adds a full memory
//      barrier (MFENCE on x86) — ~40 cycles. We only need:
//        producer: release  (make written data visible to consumer)
//        consumer: acquire  (see producer's latest write index)
//      This is the minimal correct fence pair for SPSC.
//
// SINGLE-THREAD USAGE NOTE:
//   In the backtester, both producer and consumer run in the same
//   thread (the event loop). SPSC is still correct — the fences
//   are just conservative. This architecture is ready for a
//   live-feed thread (producer) + engine thread (consumer) split.
// ============================================================
#include <atomic>
#include <array>
#include <cstddef>

namespace Aquila {

template<typename T, std::size_t N>
    requires (N > 0 && (N & (N - 1)) == 0) // Power-of-2: enforced at compile time
class SPSCRingBuffer {
public:
    // Push a value. Returns false (non-blocking) if buffer is full.
    // Caller must handle backpressure — never silently drops.
    [[nodiscard]] bool push(T val) noexcept {
        const auto write     = m_write.load(std::memory_order_relaxed);
        const auto next_write = (write + 1) & MASK;

        // Acquire: we must see the consumer's latest m_read update
        if (next_write == m_read.load(std::memory_order_acquire))
            return false; // Full

        m_slots[write] = std::move(val);

        // Release: make m_slots[write] visible to the consumer
        // BEFORE incrementing m_write. Without release, the consumer
        // could see the incremented m_write but stale slot data.
        m_write.store(next_write, std::memory_order_release);
        return true;
    }

    // Pop a value. Returns false (non-blocking) if buffer is empty.
    [[nodiscard]] bool pop(T& val) noexcept {
        const auto read = m_read.load(std::memory_order_relaxed);

        // Acquire: we must see the producer's latest m_write + slot data
        if (read == m_write.load(std::memory_order_acquire))
            return false; // Empty

        val = std::move(m_slots[read]);

        // Release: signal producer that slot is now free for reuse
        m_read.store((read + 1) & MASK, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return m_read.load(std::memory_order_acquire)
            == m_write.load(std::memory_order_acquire);
    }

    static constexpr std::size_t capacity() noexcept { return N; }

private:
    static constexpr std::size_t MASK = N - 1;

    // ──────────────────────────────────────────────────────
    // CRITICAL: each atomic on its own 64-byte cache line.
    // Without this padding, the producer writing m_write
    // invalidates the consumer's cache line (which holds m_read),
    // triggering a cache coherency round-trip on EVERY push().
    // Measurement: false sharing adds ~15ns per operation.
    // ──────────────────────────────────────────────────────
    alignas(64) std::atomic<std::size_t> m_write{0}; // written by producer
    alignas(64) std::atomic<std::size_t> m_read{0};  // written by consumer
    alignas(64) std::array<T, N>         m_slots{};  // data
};

} // namespace Aquila
