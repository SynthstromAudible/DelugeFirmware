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
	return timeout_ms_ != 0 && now_ms >= deadline_ms_;
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
		if (!line.active || frame.now_ms() >= line.expire_ms) {
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
		switch (n.kind) {
		case notify::Notice::Kind::Error:
			// Legacy's `Display::displayError` reaches `displayPopup` with its default
			// `type = PopupType::GENERAL` (display.h:48-49) -- always General, never
			// `n.popup_type`.
			show_popup(profile_.error_text ? profile_.error_text(n.error_code) : std::string_view{}, kPopupTypeGeneral,
			           now_ms);
			break;
		case notify::Notice::Kind::Text: {
			const std::string_view text = n.string_id
			                                  ? (profile_.text ? profile_.text(n.string_id) : std::string_view{})
			                                  : std::string_view{n.text};
			show_popup(text, n.popup_type, now_ms);
			break;
		}
		case notify::Notice::Kind::Console: {
			const std::string_view text = n.string_id
			                                  ? (profile_.text ? profile_.text(n.string_id) : std::string_view{})
			                                  : std::string_view{n.text};
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
			if (stack_.top().kind() == SceneKind::Popup) {
				stack_.pop();
			}
			break;
		}
	}
}

void Notices::show_popup(std::string_view text, uint8_t popup_type, uint32_t now_ms) {
	const uint8_t type = popup_type_of(popup_type);
	popup_.show(text, type, now_ms, timeout_for(type));
	if (stack_.top().kind() != SceneKind::Popup) {
		stack_.push(SceneId{SceneKind::Popup});
	}
}

void Notices::tick(uint32_t now_ms, uint32_t dt_ms) {
	(void)dt_ms;
	if (stack_.top().kind() == SceneKind::Popup && popup_.expired(now_ms)) {
		stack_.pop();
	}
	if (stack_.top().kind() == SceneKind::Working && !working_.armed()) {
		stack_.pop();
	}
}

uint32_t Notices::timeout_for(uint8_t popup_type) const {
	(void)popup_type; // Flat across every PopupType -- see this method's doc for the citation.
	return kPopupTimeoutMs;
}

} // namespace deluge::ui::shell
