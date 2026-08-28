#pragma once
// Typed raisers over deluge_notify. The library stores plain integers so it stays a leaf;
// this header, in the app layer, knows Error and l10n::String. PopupType (hid/display/display.h)
// is deliberately NOT included here -- that header pulls in l10n.h and more, and this library
// must stay light. Callers pass static_cast<uint8_t>(PopupType::X) instead of a typed overload.
//
// deluge::l10n::String only appears here by value/reference and by static_cast, so a forward
// declaration is enough -- this header does NOT include "gui/l10n/strings.h". That keeps this
// widely-included header (processing/audio_output.h and others) from pulling a gui/ header into
// model/playback/storage/io translation units. Callers that name an actual l10n::String
// enumerator (not just the type) must already include "gui/l10n/strings.h" or "gui/l10n/l10n.h"
// themselves, same as they would for any other enum they use by value.
#include "definitions_cxx.hpp" // Error
#include "notify/dirty_set.h"
#include "notify/notice_queue.h"
#include <cstddef> // size_t, for the enum's underlying type below
#include <cstdint>
#include <string_view>

namespace deluge::l10n {
enum class String : size_t;
}

namespace deluge::notify {

/// @brief Raise an error notice.
/// @param e The error to report.
inline void notice(::Error e) {
	notice_error_code(static_cast<int16_t>(e));
}
/// @brief Raise a notice carrying an l10n string id.
/// @param s The string to show.
/// @param k Notice kind (defaults to a popup).
/// @param popup_type Popup type to route it to; 0 = GENERAL.
inline void notice(deluge::l10n::String s, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0) {
	notice_string(static_cast<uint16_t>(s), k, popup_type);
}
/// @brief Raise a notice carrying literal text.
/// @param text The text to show (copied and truncated to Notice::text's capacity).
/// @param k Notice kind (defaults to a popup).
/// @param popup_type Popup type to route it to; 0 = GENERAL.
inline void notice(std::string_view text, Notice::Kind k = Notice::Kind::Text, uint8_t popup_type = 0) {
	notice_text(text, k, popup_type);
}
/// @brief Raise a request to cancel the current popup.
/// @param popup_type Popup type to cancel; 0 = GENERAL.
inline void notice_cancel_popup(uint8_t popup_type = 0) {
	notice_kind(Notice::Kind::CancelPopup, popup_type);
}
/// @brief Raise a request to show the loading/"working" animation with @p word.
/// @param word The word to display alongside the animation.
inline void notice_working(std::string_view word) {
	notice_text(word, Notice::Kind::Working);
}
/// @brief Raise a request to clear the loading/"working" animation.
inline void notice_clear_working() {
	notice_kind(Notice::Kind::ClearWorking);
}

/// @brief The error carried by a Kind::Error notice.
/// @param n The notice; must have kind() == Kind::Error.
/// @return The decoded error.
inline ::Error error_of(const Notice& n) {
	return static_cast<::Error>(n.error_code);
}
/// @brief The l10n string carried by a notice's string_id.
/// @param n The notice; must have a non-zero string_id.
/// @return The decoded string.
inline deluge::l10n::String string_of(const Notice& n) {
	return static_cast<deluge::l10n::String>(n.string_id);
}

} // namespace deluge::notify
