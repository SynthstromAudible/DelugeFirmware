#include "ui/core/blink.h"

namespace deluge::ui {

bool blink_lit(const Blink& blink, uint32_t now_ms) {
	return (((now_ms - blink.until_ms) / blink.period_ms) & 1u) != 0;
}

uint32_t blink_next_flip_in(const Blink& blink, uint32_t now_ms) {
	return blink.period_ms - ((now_ms - blink.until_ms) % blink.period_ms);
}

} // namespace deluge::ui
