#pragma once

// SpscRing<E, N> -- the tier-5 "fast lane" of the publish spine: a fixed-size,
// lock-free, allocation-free single-producer/single-consumer ring buffer for
// [task]->[audio] event delivery (note-on/off, live MPE/expression).
//
// See docs/dev/target_architecture.md §4.2 (mechanism 2) for the architectural
// slot this fills. Sibling primitive: `deluge::util::Published<T>`
// (`published.h`), which this header deliberately does not depend on -- the two
// solve different tiers (immutable snapshot-swap vs. an event stream) and stay
// independently usable.
//
// Concurrency contract: SPSC-shaped, exactly. Exactly one [task] producer
// thread calls try_push(); exactly one [audio] consumer thread calls
// try_pop()/drain(). No multi-producer, no multi-consumer support -- callers
// may rely on that; it is not re-checked at runtime (no <thread> dependency is
// taken here so this header stays compilable on the bare-metal/no-OS device
// build, not just the host sim, matching `published.h`'s discipline).
//
// Overflow policy: try_push() fails (returns false) when the ring is full. It
// does not block, does not allocate, and does not overwrite. Deciding what to
// do about a full ring -- drop the event, or coalesce it with one already
// queued (e.g. a continuous expression control collapsing to latest-wins) --
// is the caller's job, at the call site, using domain knowledge this ring
// deliberately does not have. Symmetrically, drain() is the [audio]-side
// *bounded* consume: capping the number of events applied at block start
// means a producer burst can never make one render block do unbounded work.

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace deluge::util {

/// @brief A bounded, lock-free, allocation-free single-producer/single-consumer
///        queue of `E`, with a compile-time power-of-two capacity `N`.
///
/// @par Storage
/// `slots_` is an inline `std::array<E, N>` -- no heap allocation, ever, on
/// either side. `E` must be default-constructible (to seed the array) and
/// move- or copy-assignable (`try_push`/`try_pop`/`drain` assign into and out
/// of slots in place, rather than constructing/destroying elements per
/// operation). This matches the intended payload: small, POD-like event
/// structs (note-on/off, MPE/expression deltas), not owning/RAII types.
///
/// @par Memory ordering
/// `head_` (next slot the consumer will read) and `tail_` (next slot the
/// producer will write) are each a single `std::atomic<std::size_t>`, each
/// written by exactly one side and read by both:
///
/// - **Producer (`try_push`)**: loads `tail_` **relaxed** -- it is the sole
///   writer of `tail_`, so no other thread can have changed the value out
///   from under it between writes; the load is just "what did I last write."
///   It loads `head_` **acquire** to see how far the consumer has progressed
///   (needed to compute whether the ring is full) -- this acquire is paired
///   with the consumer's release-store to `head_` below, so the producer sees
///   a `head_` value no more stale than the consumer's last freed slot. It
///   then writes the payload into `slots_[tail & mask]` as an ordinary
///   (non-atomic) memory write, and only then `tail_.store(tail + 1, release)`.
///   The release on `tail_` is what **publishes** the slot write: any
///   consumer that later acquire-loads this new `tail_` value is guaranteed
///   (happens-before) to see the fully-written slot, not a torn or reordered
///   partial write.
/// - **Consumer (`try_pop`/`drain`)**: loads `head_` **relaxed** for the same
///   sole-writer reason. It loads `tail_` **acquire** to see how far the
///   producer has published -- paired with the producer's release-store to
///   `tail_` above, this is the edge that makes the slot read safe. It then
///   reads/moves out of `slots_[head & mask]`, and only then
///   `head_.store(head + 1, release)`. The release on `head_` is what
///   **frees** the slot: the producer's next acquire-load of `head_` is
///   guaranteed to see this slot as available again, not stale.
///
/// In short: the tail release/acquire pair carries "the slot is written, safe
/// to read" from producer to consumer; the head release/acquire pair carries
/// "the slot is read, safe to overwrite" from consumer back to producer. Each
/// side touches only the atomic it owns with a relaxed self-read, and the
/// other side's atomic with an acquire read -- there is no read-modify-write,
/// and no atomic is ever touched by both threads as a writer.
///
/// @tparam E Element/event type. Default-constructible, move- or
///         copy-assignable.
/// @tparam N Capacity. Must be a power of two (enables `& (N - 1)` masking
///         instead of `%`).
template <typename E, std::size_t N>
class SpscRing {
public:
	static_assert(N > 0 && (N & (N - 1)) == 0, "SpscRing capacity N must be a power of two");

	// [audio]'s try_pop()/drain() must be wait-free-in-the-lock-free sense, not
	// silently take a lock on a target where std::atomic<size_t> isn't always
	// lock-free; fail to compile instead.
	static_assert(std::atomic<std::size_t>::is_always_lock_free);

	SpscRing() = default;

	// Not copyable/movable: this is a fixed piece of shared producer/consumer
	// state, not a value type. (Also avoids ever having to define what "move
	// a ring one side might be mid-operation on" would mean.)
	SpscRing(const SpscRing&) = delete;
	SpscRing& operator=(const SpscRing&) = delete;
	SpscRing(SpscRing&&) = delete;
	SpscRing& operator=(SpscRing&&) = delete;

	/// @brief [task] Enqueue by copy. Returns false (no-op) if the ring is
	///        full -- never blocks, never allocates. See the file banner for
	///        overflow policy (caller's job, not this ring's).
	[[nodiscard]] bool try_push(const E& value) { return push_impl(value); }

	/// @brief [task] Enqueue by move. Returns false (no-op, `value` is left
	///        untouched) if the ring is full.
	[[nodiscard]] bool try_push(E&& value) { return push_impl(std::move(value)); }

	/// @brief [audio] Dequeue one element into `out`. Returns false (leaving
	///        `out` unmodified) if the ring is empty.
	[[nodiscard]] bool try_pop(E& out) noexcept(std::is_nothrow_move_assignable_v<E>) {
		const std::size_t head = head_.load(std::memory_order_relaxed);
		const std::size_t tail = tail_.load(std::memory_order_acquire);
		if (head == tail) {
			return false; // empty
		}
		out = std::move(slots_[head & kMask]);
		head_.store(head + 1, std::memory_order_release);
		return true;
	}

	/// @brief [audio] The bounded block-start consume: pop up to `max`
	///        elements, calling `fn` on each (accepting either `const E&` or
	///        `E&&` -- each element is offered to `fn` as an rvalue via
	///        `std::move`, which binds to either signature), and return how
	///        many were actually drained. Bounding by `max` is what keeps a
	///        producer burst from making one render block do unbounded work;
	///        callers should pass a small fixed budget, not "however many are
	///        queued."
	///
	/// Reads `tail_` (acquire) once up front rather than once per element, so
	/// a drain of `k` elements costs one acquire load and one release store
	/// total, not `2k`.
	template <typename F>
	std::size_t drain(F&& fn, std::size_t max) noexcept(std::is_nothrow_move_assignable_v<E>
	                                                    && std::is_nothrow_invocable_v<F&, E&&>) {
		const std::size_t head = head_.load(std::memory_order_relaxed);
		const std::size_t tail = tail_.load(std::memory_order_acquire);
		const std::size_t available = tail - head;
		const std::size_t n = available < max ? available : max;
		for (std::size_t i = 0; i < n; ++i) {
			fn(std::move(slots_[(head + i) & kMask]));
		}
		if (n > 0) {
			head_.store(head + n, std::memory_order_release);
		}
		return n;
	}

	/// @brief [audio] Snapshot check: true if the ring had nothing to drain
	///        as of this call. Diagnostic/convenience only -- like any SPSC
	///        query, it can go stale the instant a producer pushes; audio
	///        should still rely on `try_pop`/`drain`'s return values, not
	///        this, to decide whether an element is actually payload.
	[[nodiscard]] bool empty() const noexcept {
		return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_acquire);
	}

	/// @brief Compile-time capacity (the `N` template parameter).
	[[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

private:
	static constexpr std::size_t kMask = N - 1;

	template <typename U>
	bool push_impl(U&& value) {
		const std::size_t tail = tail_.load(std::memory_order_relaxed);
		const std::size_t head = head_.load(std::memory_order_acquire);
		if (tail - head == N) {
			return false; // full
		}
		slots_[tail & kMask] = std::forward<U>(value);
		tail_.store(tail + 1, std::memory_order_release);
		return true;
	}

	std::array<E, N> slots_{};
	std::atomic<std::size_t> head_{0}; ///< [audio]-owned; producer only acquire-reads it.
	std::atomic<std::size_t> tail_{0}; ///< [task]-owned; consumer only acquire-reads it.
};

} // namespace deluge::util
