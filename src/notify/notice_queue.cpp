#include "notify/notice_queue.h"
#include <cstring>

namespace deluge::notify {

namespace {
NoticeQueue g_notices;
} // namespace

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
	g_notices.push(n);
}

void notice_string(uint16_t string_id, Notice::Kind k, uint8_t popup_type) {
	Notice n{};
	n.kind = k;
	n.string_id = string_id;
	n.popup_type = popup_type;
	g_notices.push(n);
}

void notice_error_code(int16_t code) {
	Notice n{};
	n.kind = Notice::Kind::Error;
	n.severity = Notice::Severity::Error;
	n.error_code = code;
	g_notices.push(n);
}

void notice_kind(Notice::Kind k, uint8_t popup_type) {
	Notice n{};
	n.kind = k;
	n.popup_type = popup_type;
	g_notices.push(n);
}

} // namespace deluge::notify
