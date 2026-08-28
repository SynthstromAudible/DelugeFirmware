#include "shell/notices.h"
#include "ui/oled/text.h"

#include <algorithm>
#include <cstring>

namespace deluge::ui::shell {

namespace {

/// @brief Copy \p src into \p dst, truncating to fit and always NUL-terminating.
/// @param dst      Destination buffer.
/// @param dst_size Total bytes in \p dst, NUL included.
/// @param src      Source text.
void copy_bounded(char* dst, size_t dst_size, std::string_view src) {
	const size_t n = std::min(src.size(), dst_size - 1);
	std::memcpy(dst, src.data(), n);
	dst[n] = '\0';
}

/// @brief Fold `Notice::popup_type`'s 0 ("unspecified") to `General`, exactly as
///        `notify_drain.cpp`'s `popup_type_of` does for the equivalent legacy `PopupType` cast --
///        so a shown popup's `popup_type_` is never literally the `NONE` value.
/// @param raw The notice's own `popup_type` field, or any other candidate value.
/// @return \p raw unchanged, unless it is 0, in which case `kPopupTypeGeneral`.
uint8_t popup_type_of(uint8_t raw) {
	return raw != kPopupTypeNone ? raw : kPopupTypeGeneral;
}

/// Legacy's flat popup lifetime -- see `Notices::timeout_for`'s own doc for the full citation.
inline constexpr uint32_t kPopupTimeoutMs = 800;

/// Legacy's per-console-line lifetime. `OLED::setupConsole` (`oled.cpp:178-251`) gives a new
/// console item `timeoutTime = AudioEngine::audioSampleTimer + 52000` samples ("1 and a bit
/// seconds", `oled.cpp:235`) before it starts scrolling off; the scroll-off itself then steps one
/// pixel row at a time on `TimerName::OLED_CONSOLE`, rescheduled every
/// `CONSOLE_ANIMATION_FRAME_TIME_SAMPLES` (`6 * 44` = 264 samples, `oled.cpp:797`) -- the same
/// macro `consoleText` uses to arm the timer's first interval (`oled.cpp:821`) and the scroll-off
/// loop uses to reschedule each subsequent pixel row (`oled.cpp:1142`). At the board's fixed
/// 44100 Hz sample rate that is `52000 / 44100 * 1000` = ~1179.14ms of full-strength life per
/// line before it starts to go, and `264 / 44100 * 1000` = ~5.99ms per scroll-off row. This port
/// has no pixel-scroll animation -- `ConsoleScene` keeps up to `kMaxLines` whole lines and simply
/// stops drawing one once its time is up -- so `kConsoleLineMs` uses the display lifetime
/// (rounded to the nearest millisecond, 1179), not the scroll-off frame interval; the latter is
/// recorded above only for provenance.
inline constexpr uint32_t kConsoleLineMs = 1179;

/// @name Legacy `Error`, mirrored numerically for the two codes `Notices` must suppress
///
/// `OLED::displayError` (`oled.cpp:1173-1185`) `return`s before ever calling `displayPopup` for
/// `Error::NONE`/`Error::ABORTED_BY_USER`. `Error` is declared in `src/definitions_cxx.hpp`
/// (`:454-488`), which `src/shell` may not include (the same libdeluge-boundary reason
/// `notices.h`'s `PopupType` mirror documents), so the two codes this case actually tests are
/// reproduced here by their declaration order instead: `NONE` is the enum's first entry (`:455`)
/// = 0; `ABORTED_BY_USER` is the twentieth (`:474`, after `NONE` and 18 others) = 19.
/// @{
inline constexpr int16_t kErrorNone = 0;
inline constexpr int16_t kErrorAbortedByUser = 19;
/// @}

} // namespace

ui::oled::PopupStyle PopupScene::style_for(uint8_t popup_type) {
	return popup_type == kPopupTypeNotification ? ui::oled::PopupStyle::Notification : ui::oled::PopupStyle::Framed;
}

void PopupScene::show(std::string_view text, uint8_t popup_type, uint32_t now_ms, uint32_t timeout_ms) {
	copy_bounded(text_, sizeof(text_), text);
	popup_type_ = popup_type;
	timeout_ms_ = timeout_ms;
	deadline_ms_ = now_ms + timeout_ms;
}

bool PopupScene::expired(uint32_t now_ms) const {
	// Difference form, not `now_ms >= deadline_ms_`: `deadline_ms_` is an absolute clock that wraps
	// at ~49.7 days of uptime, and an absolute compare would read a popup shown in the last
	// `timeout_ms` before the wrap as already expired -- it would flash and vanish on the very next
	// tick. Same form as `RepaintGate::needs_frame` (`ui/core/repaint_gate.cpp:13`).
	return timeout_ms_ != 0 && static_cast<int32_t>(now_ms - deadline_ms_) >= 0;
}

void PopupScene::render_oled(SurfaceRole role, ui::oled::OledFrame& frame) {
	if (role != SurfaceRole::Main) {
		return;
	}
	ui::oled::popup_text(frame, text_, style_for(popup_type_));
}

bool PopupScene::oled_transparent(SurfaceRole role) const {
	return role != SurfaceRole::Main;
}

void ConsoleScene::append(std::string_view line, uint32_t now_ms) {
	for (size_t i = kMaxLines - 1; i > 0; i--) {
		lines_[i] = lines_[i - 1];
	}
	Line& l = lines_[0];
	copy_bounded(l.text, sizeof(l.text), line);
	l.expire_ms = now_ms + kConsoleLineMs;
	l.active = true;
}

void ConsoleScene::render_oled(SurfaceRole role, ui::oled::OledFrame& frame) {
	if (role != SurfaceRole::Main) {
		return;
	}
	// Stack from the bottom of the frame upward, newest line lowest -- legacy's own console
	// grows the same way (`OLED::setupConsole` places a new item below the existing ones).
	constexpr int32_t kLineHeight = 7;
	int32_t y = frame.bounds().h;
	for (const Line& line : lines_) {
		// Difference form for the same wrap reason as `PopupScene::expired`: a line appended just
		// before the clock wraps has a wrapped (small) `expire_ms`, which an absolute compare reads
		// as already gone -- the line would be skipped on its first render and never shown at all.
		if (!line.active || static_cast<int32_t>(frame.now_ms() - line.expire_ms) >= 0) {
			continue;
		}
		y -= kLineHeight;
		frame.region({0, y, frame.bounds().w, kLineHeight}, false, [&](ui::oled::OledFrame& region) {
			ui::oled::text(region, region.bounds(), line.text, ui::oled::TextStyle{gfx::oled::FontId::Apple7});
		});
	}
}

void WorkingScene::arm(std::string_view word, uint32_t now_ms) {
	(void)now_ms; // The animation's own clock is anchored lazily by ui::oled::working() itself.
	copy_bounded(word_, sizeof(word_), word);
	armed_ = true;
	// Zero re-arms: period_ms doubles as "armed" for ui::oled::working (see Blink's own doc), so
	// resetting it here is what makes a re-`arm()` restart the animation from its first frame
	// rather than continuing wherever the previous run's clock had it.
	blink_ = Blink{};
	blink_.period_ms = 0;
}

void WorkingScene::clear() {
	armed_ = false;
}

void WorkingScene::render_oled(SurfaceRole role, ui::oled::OledFrame& frame) {
	if (role != SurfaceRole::Main || !armed_) {
		return;
	}
	ui::oled::working(frame, frame.bounds(), word_, blink_);
}

void Notices::drain(notify::NoticeQueue& queue, uint32_t now_ms) {
	notify::Notice n;
	while (queue.pop(n)) {
		// Every id resolved below goes through `BoardProfile::text_or_digits`/`error_text_or_digits`
		// rather than testing the hook itself, so a profile with no l10n bridge -- which is every
		// profile on this branch, `deluge_profile()` included -- shows the numeric id instead of an
		// empty popup box. `scratch` is what the digits are rendered into; it outlives each view
		// built from it, and `show_popup`/`console_.append` copy the text before the case ends.
		TextScratch scratch;
		switch (n.kind) {
		case notify::Notice::Kind::Error:
			// Legacy's `OLED::displayError` (oled.cpp:1173-1180) shows nothing at all for
			// Error::NONE/Error::ABORTED_BY_USER -- suppressed here the same way, before ever
			// resolving error_text or touching the stack.
			if (n.error_code == kErrorNone || n.error_code == kErrorAbortedByUser) {
				break;
			}
			// Otherwise `displayPopup` runs with its default `type = PopupType::GENERAL`
			// (display.h:48-49) -- always General, never `n.popup_type` -- and the ordinary
			// (non-persistent) timeout: `Display::displayError` never reads severity.
			show_popup(profile_.error_text_or_digits(n.error_code, scratch), kPopupTypeGeneral, now_ms);
			break;
		case notify::Notice::Kind::Text: {
			const std::string_view text =
			    n.string_id ? profile_.text_or_digits(n.string_id, scratch) : std::string_view{n.text};
			const uint8_t type = popup_type_of(n.popup_type);
			// Severity::Warn -> persistent, exactly as legacy's notify_drain.cpp:137
			// (`flashes = (severity == Warn) ? 0 : 3`) feeds OLED::displayPopup's inline
			// `persistent = !numFlashes` (oled.h:156), which oled.cpp:642 turns into "never set
			// the DISPLAY timer" -- everything else keeps the ordinary timeout_for() duration.
			const uint32_t timeout_ms = n.severity == notify::Notice::Severity::Warn ? 0 : timeout_for(type);
			show_popup_with_timeout(text, type, now_ms, timeout_ms);
			break;
		}
		case notify::Notice::Kind::Console: {
			const std::string_view text =
			    n.string_id ? profile_.text_or_digits(n.string_id, scratch) : std::string_view{n.text};
			console_.append(text, now_ms);
			break;
		}
		case notify::Notice::Kind::Working:
			working_.arm(n.text, now_ms);
			if (stack_.top().kind() != SceneKind::Working) {
				stack_.push(SceneId{SceneKind::Working});
			}
			break;
		case notify::Notice::Kind::ClearWorking:
			// Does not pop here: Notices::tick is what pops the working scene, once armed()
			// reports false -- see that method's doc.
			working_.clear();
			break;
		case notify::Notice::Kind::CancelPopup:
			// By kind, not "pop if the popup happens to be on top": a scene pushed above a live
			// popup would otherwise strand it -- invisible (the `render_oled` walk stops at the
			// first non-transparent scene above it) but still stacked, still reported by
			// `state_lines()`, and back on screen the moment that scene pops. `pop(kind)` takes
			// what is above it down too, exactly as `Screensaver::note_activity` does on wake; a
			// no-op when no popup is stacked at all.
			stack_.pop(SceneKind::Popup);
			break;
		}
	}
}

void Notices::show_popup(std::string_view text, uint8_t popup_type, uint32_t now_ms) {
	const uint8_t type = popup_type_of(popup_type);
	show_popup_with_timeout(text, type, now_ms, timeout_for(type));
}

void Notices::show_popup_with_timeout(std::string_view text, uint8_t popup_type, uint32_t now_ms, uint32_t timeout_ms) {
	// `PopupScene::show` fully overwrites text/type/timeout on every call, whatever the previous
	// call's persistence was -- exactly as legacy's OLED::popupText unconditionally sets or unsets
	// TimerName::DISPLAY for *this* call (oled.cpp:636-643), never consulting the popup's prior
	// state. A later non-persistent notice replacing a still-persistent one therefore ends the
	// persistence the same way legacy does: not by any special "cancel persistence" step, but
	// because the replacement simply never re-arms it.
	popup_.show(text, popup_type, now_ms, timeout_ms);
	if (stack_.top().kind() != SceneKind::Popup) {
		stack_.push(SceneId{SceneKind::Popup});
	}
}

void Notices::tick(uint32_t now_ms, uint32_t dt_ms) {
	(void)dt_ms;
	// Both pops are by kind rather than "only while it is the top", for the reason `CancelPopup`'s
	// own case above spells out: a scene pushed over a live notice must not be able to strand it on
	// the stack for ever. Neither is guarded on the scene being stacked at all -- `SceneStack::pop`
	// for a kind that is not there touches nothing and (since the review's `changed_` fix) does not
	// flag the stack as changed either, so calling it on every tick costs one downward scan.
	if (popup_.expired(now_ms)) {
		stack_.pop(SceneKind::Popup);
	}
	if (!working_.armed()) {
		stack_.pop(SceneKind::Working);
	}
}

uint32_t Notices::timeout_for(uint8_t popup_type) const {
	(void)popup_type; // Flat across every PopupType -- see this method's doc for the citation.
	return kPopupTimeoutMs;
}

} // namespace deluge::ui::shell
