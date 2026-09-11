#include "notify/notice_queue.h"
#include <cstring>

namespace deluge::notify {

namespace {
NoticeQueue g_notices;
/// Incremented by the notice_* helpers below when push() reports an overwrite. Relaxed
/// throughout: nothing is ordered against it, and it may be bumped from [any] context.
std::atomic<uint32_t> g_dropped{0};

/// The one place the helpers' `[[nodiscard]] bool` is consumed: a drop is counted, never ignored.
void push_counting_drops(const Notice& n) {
	if (!g_notices.push(n)) {
		g_dropped.fetch_add(1, std::memory_order_relaxed);
	}
}
} // namespace

uint32_t dropped_notices() {
	return g_dropped.load(std::memory_order_relaxed);
}

NoticeQueue& notices() {
	return g_notices;
}

bool NoticeQueue::push(const Notice& n) {
	uint32_t head = head_.load(std::memory_order_relaxed);
	uint32_t tail = tail_.load(std::memory_order_acquire);
	bool overwrote = false;
	if (head - tail >= kNoticeCapacity) { // full: drop the oldest
		tail_.store(tail + 1, std::memory_order_release);
		overwrote = true;
	}
	ring_[head % kNoticeCapacity] = n;
	head_.store(head + 1, std::memory_order_release);
	return !overwrote;
}

bool NoticeQueue::pop(Notice& out) {
	uint32_t tail = tail_.load(std::memory_order_relaxed);
	if (tail == head_.load(std::memory_order_acquire)) {
		return false;
	}
	out = ring_[tail % kNoticeCapacity];
	tail_.store(tail + 1, std::memory_order_release);
	return true;
}

void notice_text(std::string_view s, Notice::Kind k, uint8_t popup_type) {
	Notice n{};
	n.kind = k;
	n.popup_type = popup_type;
	const size_t len = s.size() < sizeof(n.text) - 1 ? s.size() : sizeof(n.text) - 1;
	std::memcpy(n.text, s.data(), len);
	n.text[len] = 0;
	push_counting_drops(n);
}

void notice_string(uint16_t string_id, Notice::Kind k, uint8_t popup_type) {
	Notice n{};
	n.kind = k;
	n.string_id = string_id;
	n.popup_type = popup_type;
	push_counting_drops(n);
}

void notice_error_code(int16_t code) {
	Notice n{};
	n.kind = Notice::Kind::Error;
	n.severity = Notice::Severity::Error;
	n.error_code = code;
	push_counting_drops(n);
}

void notice_notification(std::string_view title, std::string_view value) {
	Notice n{};
	n.kind = Notice::Kind::Notification;
	// "<title>\0<value>\0" in `text`: two NULs, and the value keeps priority over the title (see the
	// declaration's doc for legacy's own truncation order).
	const size_t room = sizeof(n.text) - 2;
	const size_t value_len = value.size() < room ? value.size() : room;
	const size_t title_len = title.size() < room - value_len ? title.size() : room - value_len;
	if (title_len != 0) {
		std::memcpy(n.text, title.data(), title_len);
	}
	n.text[title_len] = 0;
	n.value_at = static_cast<uint8_t>(title_len + 1);
	if (value_len != 0) {
		std::memcpy(n.text + n.value_at, value.data(), value_len);
	}
	n.text[n.value_at + value_len] = 0;
	push_counting_drops(n);
}

void notice_kind(Notice::Kind k, uint8_t popup_type) {
	Notice n{};
	n.kind = k;
	n.popup_type = popup_type;
	push_counting_drops(n);
}

} // namespace deluge::notify
