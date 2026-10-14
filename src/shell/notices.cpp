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

const char* popup_type_name(uint8_t popup_type) {
	// The eleven values `notices.h`'s "Legacy PopupType, mirrored numerically" block enumerates,
	// in their declaration order, so the index *is* the legacy value.
	static constexpr const char* kNames[] = {
	    "None",        "General", "Loading", "Probability", "Iterance",
	    "Fill",        "Swing",   "Tempo",   "Quantize",    "ThresholdRecordingMode",
	    "Notification"};
	static_assert(sizeof(kNames) / sizeof(kNames[0]) == kPopupTypeNotification + 1);
	return popup_type < sizeof(kNames) / sizeof(kNames[0]) ? kNames[popup_type] : nullptr;
}

const char* notice_kind_name(notify::Notice::Kind k) {
	switch (k) {
	case notify::Notice::Kind::Error:
		return "Error";
	case notify::Notice::Kind::Text:
		return "Text";
	case notify::Notice::Kind::Console:
		return "Console";
	case notify::Notice::Kind::Working:
		return "Working";
	case notify::Notice::Kind::ClearWorking:
		return "ClearWorking";
	case notify::Notice::Kind::CancelPopup:
		return "CancelPopup";
	case notify::Notice::Kind::CancelConsole:
		return "CancelConsole";
	case notify::Notice::Kind::Notification:
		return "Notification";
	case notify::Notice::Kind::CancelNotification:
		return "CancelNotification";
	}
	return "Text";
}

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

void ConsoleScene::append(std::string_view line, uint32_t now_ms, uint8_t popup_type) {
	for (size_t i = kMaxLines - 1; i > 0; i--) {
		lines_[i] = lines_[i - 1];
	}
	Line& l = lines_[0];
	copy_bounded(l.text, sizeof(l.text), line);
	l.expire_ms = now_ms + kConsoleLineMs;
	l.active = true;
	l.popup_type = popup_type;
}

void ConsoleScene::cancel(uint8_t popup_type) {
	for (Line& line : lines_) {
		if (line.popup_type == popup_type) {
			line.active = false;
		}
	}
}

bool ConsoleScene::any_active(uint32_t now_ms) const {
	for (const Line& line : lines_) {
		// Difference form for the same wrap reason as `render_oled`'s own per-line check and
		// `PopupScene::expired()`: see either for why an absolute compare would misread a line
		// appended just before the clock wraps as already gone.
		if (line.active && static_cast<int32_t>(now_ms - line.expire_ms) < 0) {
			return true;
		}
	}
	return false;
}

void ConsoleScene::render_oled(SurfaceRole role, ui::oled::OledFrame& frame) {
	if (role != SurfaceRole::Main) {
		return;
	}
	// Stack from the bottom of the frame upward, newest line lowest -- legacy's own console
	// grows the same way (`OLED::setupConsole` places a new item below the existing ones).
	//
	// `text_height_for(Apple7)`, NOT a hand-picked 7: `FontId::Apple7` draws at height 9
	// (`ui/core/metrics.h`'s own note -- the Apple ][ face's "Up" variant, one row lower than its
	// 7-8 sibling, and 9 is what this `FontId` means). A hard-coded 7 clipped the glyphs' bottom
	// row inside every `region` below -- caught by task 11's review round 1: with the row gone,
	// `E` and `F` render pixel-identical, which a set-pixel-count assertion alone cannot catch.
	const int32_t kLineHeight = ui::oled::text_height_for(gfx::oled::FontId::Apple7);
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

namespace {
/// The one installed `NotificationSink`, or null -- see `set_notification_sink`.
const NotificationSink* g_notification_sink = nullptr;
} // namespace

void set_notification_sink(const NotificationSink* sink) {
	g_notification_sink = sink;
}

const NotificationSink* notification_sink() {
	return g_notification_sink;
}

void NotificationScene::show(std::string_view title, std::string_view value, uint32_t now_ms) {
	copy_bounded(title_, sizeof(title_), title);
	copy_bounded(value_, sizeof(value_), value);
	deadline_ms_ = now_ms + kNotificationMs;
}

bool NotificationScene::expired(uint32_t now_ms) const {
	// Difference form for the clock-wrap reason `PopupScene::expired` gives.
	return static_cast<int32_t>(now_ms - deadline_ms_) >= 0;
}

void NotificationScene::render_oled(SurfaceRole role, ui::oled::OledFrame& frame) {
	if (role != SurfaceRole::Main) {
		return;
	}
	ui::oled::notification(frame, title_, value_);
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
			show_popup_with_timeout(profile_.error_text_or_digits(n.error_code, scratch), kPopupTypeGeneral, now_ms,
			                        timeout_for(kPopupTypeGeneral), notify::Notice::Kind::Error);
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
			show_popup_with_timeout(text, type, now_ms, timeout_ms, notify::Notice::Kind::Text);
			break;
		}
		case notify::Notice::Kind::Console: {
			const std::string_view text =
			    n.string_id ? profile_.text_or_digits(n.string_id, scratch) : std::string_view{n.text};
			console_.append(text, now_ms, n.popup_type);
			// Legacy has no scene stack at all: `OLED::sendMainImage` (`oled.cpp:364-376`)
			// unconditionally composites the console strip into every frame, so a console line is
			// always potentially visible the instant it is appended. The port's equivalent is
			// making this scene's OWN presence self-managing, exactly as the `Popup`/`Working`
			// cases already do -- found missing by task 11's `menu_submenu_horizontal` acceptance
			// script: a value-column select press raised a real `Kind::Console` notice
			// (`MenuScene::after_render`), `console_.append` ran, and the appended text never
			// appeared in ANY snapshot because nothing, anywhere (`Shell::boot`/`init` included),
			// ever pushed `SceneKind::Console` onto the stack -- unlike `Working`, whose case
			// below has always self-managed its own push. Scanned like `show_popup_with_timeout`'s
			// own check (the whole stack, not just `top()`): a scene may legitimately sit above an
			// already-live console strip, and re-pushing would plant a second entry pointing at
			// this same instance. Never removed again -- `tick()` below only polls popup expiry
			// and working's armed flag; the console's *content* expires per line
			// (`ConsoleScene::append`'s `kConsoleLineMs`), not the scene's presence on the stack.
			bool console_stacked = false;
			for (Scene* scene : stack_.scenes()) {
				if (scene->kind() == SceneKind::Console) {
					console_stacked = true;
					break;
				}
			}
			if (!console_stacked) {
				stack_.push(SceneId{SceneKind::Console});
			}
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
			//
			// KNOWN LIMITATION, and it is already live: this clear is unconditional and
			// unrefcounted. There is one WorkingScene and no record of who armed it, so when two
			// subsystems have a Kind::Working up at once, whichever raises ClearWorking first takes
			// the other's animation down and leaves it working with no indicator. Two raisers
			// exist today: `PlaybackHandler::doSongSwap` (`playback/playback_handler.cpp:1564`, via
			// `notice_working`, `util/notify.h:52`) and the scenes layer's async listing seam
			// (`scenes/browser/listing.cpp`), and a song swap can overlap a directory listing.
			// Closing it means a refcount, or an owner token on the arm/clear pair, so a clear from
			// a subsystem that did not arm the current animation is ignored -- deliberately not
			// done as a drive-by here, because it changes the meaning of every existing raise.
			working_.clear();
			// `OLED::removeWorkingAnimation` also takes down a `NOTIFICATION` popup
			// (`oled.cpp:733-737`), so the banner goes with it -- a no-op when none is stacked.
			stack_.remove(SceneKind::Notification);
			break;
		case notify::Notice::Kind::CancelPopup:
			// `remove`, not `pop`: a popup is `input_transparent()`, so a scene pushed above a
			// live popup is there legitimately (its own `Push` landed above a layer that never
			// took input) and must not be taken down just because the popup itself is being
			// cancelled. `remove(kind)` excises only the popup and closes the gap; a no-op when
			// no popup is stacked at all.
			stack_.remove(SceneKind::Popup);
			// Legacy's `cancelPopup()` is `removePopup()` (`oled.h:164`), which clears the one popup
			// slot whatever its type (`oled.cpp:253-258`) -- so it takes the banner down too.
			stack_.remove(SceneKind::Notification);
			break;
		case notify::Notice::Kind::CancelConsole:
			// (Its one raiser, `MenuScene`'s horizontal-page cancel, is `CancelNotification` since
			// note-family Task 8b; this arm is kept unchanged.)
			//
			// Scoped to this notice's own `popup_type` -- fix round 2's correction, see
			// `ConsoleScene::cancel`'s own doc for why a console-wide `CancelPopup` was wrong.
			// `cancel()` first, so a scene pushed above a still-live console (again legitimately,
			// `Console` is `input_transparent()` too) does not see a cancelled line reappear if
			// something re-pushes `SceneKind::Console` before this tick's own removal -- and the
			// removal itself is now conditional: an unrelated line of a different `popup_type` may
			// still be active, and the scene must stay up for it.
			console_.cancel(n.popup_type);
			if (!console_.any_active(now_ms)) {
				stack_.remove(SceneKind::Console);
			}
			break;
		case notify::Notice::Kind::Notification: {
			// Legacy's `display->displayNotification(title, value)` (`horizontal_menu.cpp:342`). With a
			// sink installed this *is* that call, in legacy's own popup slot -- the same call legacy's
			// `drain_notifications` makes for this kind, so it does not matter which consumer popped
			// it. With none, the port's banner stands in for the slot. See `NotificationSink`.
			const NotificationSink* sink = g_notification_sink;
			if (sink != nullptr && sink->show != nullptr) {
				sink->show(n.notification_title(), n.notification_value());
				break;
			}
			notification_.show(n.notification_title(), n.notification_value(), now_ms);
			// One popup slot: `setupPopup(PopupType::NOTIFICATION, ...)` (`oled.cpp:768`) overwrites
			// whatever popup held it (`:122-132`), so a live `PopupScene` goes.
			stack_.remove(SceneKind::Popup);
			// Pushed -- and, when a banner is already up, removed first and pushed again, where the
			// popup case re-targets its entry in place. Legacy's replacement is a redraw as well as a
			// rewrite (`displayNotification` ends in `markChanged()`, `oled.cpp:793`), and a stack
			// change is what makes the shell repaint on a tick with no input
			// (`Shell::tick`'s `changed_since_last_query`); an in-place rewrite would leave the old
			// title on the panel until something else asked for a frame. It also lands the banner on
			// top, where legacy composites its popup: after the console, last (`oled.cpp:364-376`).
			// `remove` is a no-op, and flags nothing, when no banner is stacked.
			stack_.remove(SceneKind::Notification);
			stack_.push(SceneId{SceneKind::Notification});
			break;
		}
		case notify::Notice::Kind::CancelNotification: {
			// Legacy's guarded cancel, `if (display->hasPopupOfType(PopupType::NOTIFICATION))
			// { display->cancelPopup(); }` (`horizontal_menu.cpp:363-365`, `:587-589`). With a sink,
			// that call; with none, removing the banner is the same guard -- it can only ever take
			// down a notification, never a `PopupScene` or a console line.
			const NotificationSink* sink = g_notification_sink;
			if (sink != nullptr && sink->cancel != nullptr) {
				sink->cancel();
				break;
			}
			stack_.remove(SceneKind::Notification);
			break;
		}
		}
	}
}

void Notices::show_popup(std::string_view text, uint8_t popup_type, uint32_t now_ms) {
	const uint8_t type = popup_type_of(popup_type);
	// `Kind::Text`: an `Action::Kind::Popup` draws the same popup a text notice does, through this
	// same funnel -- see this method's own doc -- so that is the kind `.state` reports for it.
	show_popup_with_timeout(text, type, now_ms, timeout_for(type), notify::Notice::Kind::Text);
}

void Notices::show_popup_with_timeout(std::string_view text, uint8_t popup_type, uint32_t now_ms, uint32_t timeout_ms,
                                      notify::Notice::Kind kind) {
	last_notice_kind_ = kind;
	// One popup slot: `popupText` -> `setupPopup` overwrites a `NOTIFICATION` popup like any other
	// (`oled.cpp:605-618`, `:122-132`), so a live banner goes.
	stack_.remove(SceneKind::Notification);
	// `PopupScene::show` fully overwrites text/type/timeout on every call, whatever the previous
	// call's persistence was -- exactly as legacy's OLED::popupText unconditionally sets or unsets
	// TimerName::DISPLAY for *this* call (oled.cpp:636-643), never consulting the popup's prior
	// state. A later non-persistent notice replacing a still-persistent one therefore ends the
	// persistence the same way legacy does: not by any special "cancel persistence" step, but
	// because the replacement simply never re-arms it.
	popup_.show(text, popup_type, now_ms, timeout_ms);
	// Re-targets the existing popup entry wherever it sits, rather than testing only `top()`: a
	// scene may have been legitimately `Push`ed above a live popup (it is `input_transparent()`, so
	// that scene still has input), and testing `top()` alone would read that scene's kind, not the
	// popup's, and push a second `SceneKind::Popup` entry pointing at this same instance.
	// (Recorded, not changed: re-targeting in place changes no stack, so on a tick with no input the
	// panel can keep the old text until something else asks for a frame -- Task 8b's gap, closed
	// for the banner in `drain()`'s `Notification` case by removing and pushing again.)
	bool popup_stacked = false;
	for (Scene* scene : stack_.scenes()) {
		if (scene->kind() == SceneKind::Popup) {
			popup_stacked = true;
			break;
		}
	}
	if (!popup_stacked) {
		stack_.push(SceneId{SceneKind::Popup});
	}
}

void Notices::tick(uint32_t now_ms, uint32_t dt_ms) {
	(void)dt_ms;
	// All three are `remove(kind)` rather than "pop while it is the top" or "pop by kind": a popup,
	// the working animation and the console strip are all `input_transparent()`, so a scene pushed
	// over any of them got there legitimately and must not be taken down just because the notice
	// beneath it expires or clears. `remove(kind)` excises only the notice scene and closes the gap.
	// None of the three is guarded on the scene being stacked at all -- `SceneStack::remove` for a
	// kind that is not there touches nothing and does not flag the stack as changed either, so
	// calling it on every tick costs one downward scan.
	if (popup_.expired(now_ms)) {
		stack_.remove(SceneKind::Popup);
	}
	if (!working_.armed()) {
		stack_.remove(SceneKind::Working);
	}
	// Review round 1, I4: `Kind::Console`'s `drain()` case pushes this scene (self-managing, like
	// the two above) but had no matching removal at all -- once every line it ever showed had
	// expired, the entry just sat on the stack forever. `any_active` is content-level (per line),
	// unlike `popup_.expired()`/`working_.armed()`, so this is the same level test the two above
	// are, just phrased over three lines instead of one.
	if (!console_.any_active(now_ms)) {
		stack_.remove(SceneKind::Console);
	}
	// The banner's 2000 ms `DISPLAY` timer: `OLED::timerRoutine`'s `removePopup()` on expiry
	// (`gui/ui_timer_manager.cpp:91-93`), which runs only when no working animation is running
	// (`oled.cpp:929-937`) -- while one is, the banner stays until `removeWorkingAnimation` takes it
	// down (`:735-736`), `drain()`'s `ClearWorking` case. A no-op when no banner is stacked.
	if (!working_.armed() && notification_.expired(now_ms)) {
		stack_.remove(SceneKind::Notification);
	}
}

uint32_t Notices::timeout_for(uint8_t popup_type) const {
	(void)popup_type; // Flat across every PopupType -- see this method's doc for the citation.
	return kPopupTimeoutMs;
}

} // namespace deluge::ui::shell
