#pragma once
// Typed raisers over deluge_notify. The library stores plain integers so it stays a leaf;
// this header, in the app layer, knows Error and l10n::String. PopupType (hid/display/display.h)
// is deliberately NOT included here -- that header pulls in l10n.h and more, and the spec
// library must stay light. Callers pass static_cast<uint8_t>(PopupType::X) instead; the cast
// moves to a typed one when Display dissolves and PopupType moves into notify.
#include "definitions_cxx.hpp" // Error
#include "gui/l10n/strings.h"  // deluge::l10n::String
#include "notify/dirty_set.h"
#include "notify/notice_queue.h"
#include <cstdint>
#include <string_view>

namespace deluge::notify {

inline void notice(::Error e) {
	notice_error_code(static_cast<int16_t>(e));
}
inline void notice(deluge::l10n::String s, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0) {
	notice_string(static_cast<uint16_t>(s), k, popup_type);
}
inline void notice(std::string_view text, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0) {
	notice_text(text, k, popup_type);
}
inline void notice_cancel_popup(uint8_t popup_type = 0) {
	notice_kind(Notice::Kind::CancelPopup, popup_type);
}
inline void notice_working(std::string_view word) {
	notice_text(word, Notice::Kind::Working);
}
inline void notice_clear_working() {
	notice_kind(Notice::Kind::ClearWorking);
}

inline ::Error error_of(const Notice& n) {
	return static_cast<::Error>(n.error_code);
}
inline deluge::l10n::String string_of(const Notice& n) {
	return static_cast<deluge::l10n::String>(n.string_id);
}

} // namespace deluge::notify
