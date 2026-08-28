#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace deluge::notify {

/// @brief One raised notice: an error, a message, or a display-state request, copied by value.
struct Notice {
	enum class Kind : uint8_t { Error, Text, Console, Working, ClearWorking, CancelPopup };
	enum class Severity : uint8_t { Info, Warn, Error };
	Kind kind = Kind::Text;
	Severity severity = Severity::Info;
	int16_t error_code = 0; // Kind::Error: static_cast<int16_t>(::Error)
	uint16_t string_id = 0; // Text/Console with an l10n id (static_cast of l10n::String); 0 = none
	// Copied at the raise; NUL-terminated; 0-length = none. 48 bytes holds every computed popup
	// the model raises today -- the longest sources are `etl::string<40>` messages
	// ("Transpose Interval: \n<n> Semitones", "Can't control: \n<param name>"), so 40 + NUL fits
	// with room to spare, and nothing else in the model exceeds 47 characters.
	char text[48] = {};
	uint8_t popup_type = 0; // today's PopupType value; 0 = GENERAL
};

constexpr size_t kNoticeCapacity = 16;

/// @brief Overwrite-oldest, allocation-free ring buffer of Notice, bridging model code to the UI tick.
///
/// A single logical producer at a time -- serialised today by cooperative scheduling, marked
/// COOP-WRITE at the call sites -- and a single consumer, the UI tick. COOP-WRITE also covers the
/// overwrite-oldest branch itself: the slot a concurrent pop()'s advanced `tail` just released is
/// rewritten here by a plain struct copy, which assumes push() and pop() never truly overlap --
/// under real preemption the copy could tear.
///
/// @see docs/dev/target_architecture.md for the real SPSC queue this stands in for.
class NoticeQueue {
public:
	/// @brief Push @p n, overwriting the oldest entry if the ring is full. [any] COOP-WRITE:
	///        producers are serialised by today's cooperative scheduler, not by this class.
	///
	/// @param n The notice to push (copied).
	/// @return False when the ring was full and the oldest notice was overwritten to make room --
	///         i.e. a notice the model raised will never be shown. [[nodiscard]] because silently
	///         dropping that is indistinguishable from "the model never raised it"; a caller with
	///         nowhere to report it should say so with `(void)` or count it (see dropped_notices()).
	[[nodiscard]] bool push(const Notice& n);

	/// @brief Pop the oldest notice into @p out. [task]
	/// @param out Overwritten with the popped notice on success; untouched on failure.
	/// @return False if the ring was empty.
	bool pop(Notice& out);

	/// @brief Number of notices currently queued.
	/// @return The number of queued notices.
	size_t size() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }

private:
	Notice ring_[kNoticeCapacity];
	std::atomic<uint32_t> head_{0}; // next write
	std::atomic<uint32_t> tail_{0}; // next read
};

/// @brief The process-global NoticeQueue.
/// @return The global NoticeQueue.
NoticeQueue& notices();

// The raise-side helpers. They cannot propagate a full queue -- their callers are model code deep
// in the audio/playback paths with no way to handle it and nowhere to put it -- so instead of
// discarding the fact, each one counts the drop. `dropped_notices()` is the shell's diagnostic:
// a non-zero, growing count says notices are being raised faster than the UI tick drains them.
void notice_text(std::string_view short_text, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0);
void notice_string(uint16_t string_id, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0);
void notice_error_code(int16_t code);
void notice_kind(Notice::Kind k, uint8_t popup_type = 0); // Working/ClearWorking/CancelPopup

/// @brief Number of notices the helpers above have overwritten because the queue was full, since boot.
/// @note Relaxed: it is a diagnostic counter, ordered against nothing, and the helpers may run [any].
/// @return The drop count.
uint32_t dropped_notices();

} // namespace deluge::notify
