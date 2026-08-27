#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace deluge::notify {

struct Notice {
	enum class Kind : uint8_t { Error, Text, Console, Working, ClearWorking, CancelPopup };
	enum class Severity : uint8_t { Info, Warn, Error };
	Kind kind = Kind::Text;
	Severity severity = Severity::Info;
	int16_t error_code = 0; // Kind::Error: static_cast<int16_t>(::Error)
	uint16_t string_id = 0; // Text/Console with an l10n id (static_cast of l10n::String); 0 = none
	char text[24] = {};     // copied; NUL-terminated; 0-length = none
	uint8_t popup_type = 0; // today's PopupType value; 0 = GENERAL
};

constexpr size_t kNoticeCapacity = 16;

/// Overwrite-oldest ring. Single logical producer at a time (cooperative scheduling today --
/// see spec C §6, `COOP-WRITE`); the consumer is the UI tick. COOP-WRITE also covers the
/// overwrite-oldest branch itself: the slot a concurrent pop's advanced `tail` just released is
/// rewritten here by a plain struct copy, which assumes push and that pop never truly overlap --
/// under real preemption the copy could tear. Replace with the real SPSC queue of
/// target_architecture.md §4.2.
class NoticeQueue {
public:
	bool push(const Notice& n); // [any] COOP-WRITE: producers are serialised by today's
	                            // cooperative scheduler, not by this class.
	bool pop(Notice& out);      // [task]
	size_t size() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }

private:
	Notice ring_[kNoticeCapacity];
	std::atomic<uint32_t> head_{0}; // next write
	std::atomic<uint32_t> tail_{0}; // next read
};

NoticeQueue& notices();
void notice_text(std::string_view short_text, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0);
void notice_string(uint16_t string_id, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0);
void notice_error_code(int16_t code);
void notice_kind(Notice::Kind k, uint8_t popup_type = 0); // Working/ClearWorking/CancelPopup

} // namespace deluge::notify
