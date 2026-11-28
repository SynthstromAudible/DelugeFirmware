#pragma once

/// @file
/// @brief Every popup the code under test raises, whichever way it reaches the screen: a call on legacy's display, a
///        notice (which legacy's display or the shell's popup scene then shows), or a `Popup` action in a scene's
///        `Result`. A spec asserts on what the user would see (`seen`), so a port that moves a popup from one route to
///        another keeps its spec; `events` keeps the route for the few specs that pin one.
///
/// A `PopupCapture` installs itself for its scope: legacy's `display` points at its recorder, and every notice is shown
/// to it as it is pushed (`notify::set_notice_observer`), whoever drains the queue later. A notice popped and shown at
/// once on legacy's display (`notify_drain`) is one popup, not two: the display call that matches the notice just
/// popped is not recorded again. One capture is active at a time; a nested one takes over and gives the outer one back
/// when it goes.

#include "base/notify/notice_queue.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/language.h"
#include "gui/l10n/strings.h"
#include "hid/display/display.h"
#include "ui/shell/scene.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace deluge::ui::scenes::spec {

/// @brief One popup event.
struct PopupEvent {
	/// What the user sees happen.
	enum class Kind : uint8_t {
		POPUP,               ///< A popup that goes by itself (`displayPopup`, a notice's `Text`).
		PERSISTENT,          ///< A popup that stays until something removes it (`popupText`, `displayPopup` with no
		                     ///< flashes, a `Warn` notice).
		TEMPORARY,           ///< `popupTextTemporary`.
		NOTIFICATION,        ///< The one-line banner: its title, a newline, its value.
		ERROR_CODE,          ///< An error's popup, by its code.
		CANCEL,              ///< A popup taken down.
		CANCEL_NOTIFICATION, ///< The banner taken down, if one is up (a notice; the display sees a `CANCEL`).
		WORKING,             ///< The working animation, with its text.
		CLEAR_WORKING,       ///< The working animation taken down.
		CONSOLE,             ///< A console line.
		FREEZE,              ///< `freezeWithError`.
	};
	/// The way it reached the screen.
	enum class Route : uint8_t { DISPLAY, NOTICE, ACTION };

	Kind kind = Kind::POPUP;
	std::string text;
	Route route = Route::DISPLAY;

	/// @return The event as the user sees it, its route left out: `"<kind>: <text>"`.
	[[nodiscard]] std::string seen() const {
		static constexpr std::array<const char*, 11> kNames{"popup",   "persistent", "temporary",     "notification",
		                                                    "error",   "cancel",     "cancel banner", "working",
		                                                    "cleared", "console",    "freeze"};
		return std::string{kNames.at(static_cast<uint8_t>(kind))} + ": " + text;
	}
};

/// @brief Legacy's display for code that needs one to call and whose popups no example asks about: it draws nothing,
///        keeps nothing, and answers that no popup is up.
struct QuietDisplay final : deluge::hid::Display {
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const* /*text*/, int8_t /*num_flashes*/, bool /*align_right*/, uint8_t /*draw_dot*/,
	                  int32_t /*blink_speed*/, PopupType /*type*/) override {}
	void popupText(char const* /*text*/, PopupType /*type*/) override {}
	void popupTextTemporary(char const* /*text*/, PopupType /*type*/) override {}
	void cancelPopup() override {}
	void freezeWithError(char const* /*text*/) override {}
	void displayError(Error /*error*/) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const* /*text*/, bool /*delayed*/, bool /*transparent*/) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType /*type*/) override { return false; }
	void consoleText(char const* /*text*/) override {}
	void timerRoutine() override {}
};

/// @return The process's one `QuietDisplay`, for a spec that installs it as `display` and leaves it there.
inline QuietDisplay& quiet_display() {
	static QuietDisplay d;
	return d;
}

/// @brief Records every popup event while it is in scope. See the file docstring.
class PopupCapture {
public:
	/// What the capture keeps.
	enum class Mode : uint8_t {
		RECORD, ///< Every event.
		QUIET,  ///< Nothing, and no popup is ever up (`QuietDisplay`), for one scope.
	};

	explicit PopupCapture(Mode mode = Mode::RECORD)
	    : mode_(mode), outer_(active()), saved_display_(display), recorder_(*this) {
		active() = this;
		display = &recorder_;
		// `l10n::chosenLanguage` is null until the app constructs its display; a spec process never does.
		if (deluge::l10n::chosenLanguage == nullptr) {
			deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		}
		deluge::base::notify::set_notice_observer(&observe);
	}
	PopupCapture(const PopupCapture&) = delete;
	PopupCapture& operator=(const PopupCapture&) = delete;
	~PopupCapture() {
		display = saved_display_;
		active() = outer_;
		deluge::base::notify::set_notice_observer(outer_ != nullptr ? &observe : nullptr);
	}

	/// @brief Record the `Popup` actions in @p r, a scene's answer the spec handled itself.
	void take(const deluge::ui::shell::Result& r) {
		for (const deluge::ui::shell::Action& a : r.actions) {
			if (a.kind == deluge::ui::shell::Action::Kind::Popup) {
				add(PopupEvent::Kind::POPUP, string_text(a.popup_string), PopupEvent::Route::ACTION);
			}
		}
	}

	/// @return Every event, in the order it happened.
	[[nodiscard]] const std::vector<PopupEvent>& events() const { return events_; }

	/// @return Each event as the user sees it (`PopupEvent::seen`), in order.
	[[nodiscard]] std::vector<std::string> seen() const {
		std::vector<std::string> out;
		out.reserve(events_.size());
		for (const PopupEvent& e : events_) {
			out.push_back(e.seen());
		}
		return out;
	}

	/// @return `seen()`, one event per line.
	[[nodiscard]] std::string seen_text() const {
		std::string out;
		for (const PopupEvent& e : events_) {
			out += e.seen();
			out += '\n';
		}
		return out;
	}

	/// @return How many events so far are of @p kind, whichever way they came.
	[[nodiscard]] size_t count(PopupEvent::Kind kind) const {
		return static_cast<size_t>(std::ranges::count(events_, kind, &PopupEvent::kind));
	}

	/// @return The text of the latest event of @p kind, or nothing if there is none.
	[[nodiscard]] std::optional<std::string> last(PopupEvent::Kind kind) const {
		for (auto e = events_.rbegin(); e != events_.rend(); ++e) {
			if (e->kind == kind) {
				return e->text;
			}
		}
		return std::nullopt;
	}

	/// @return The text of the latest popup of any length (`POPUP`, `PERSISTENT`, `TEMPORARY`), or empty if there is
	///        none.
	[[nodiscard]] std::string last_popup() const {
		for (auto e = events_.rbegin(); e != events_.rend(); ++e) {
			if (e->kind == PopupEvent::Kind::POPUP || e->kind == PopupEvent::Kind::PERSISTENT
			    || e->kind == PopupEvent::Kind::TEMPORARY) {
				return e->text;
			}
		}
		return {};
	}

	/// @brief Forget every event so far.
	void clear() { events_.clear(); }

	/// @return Whether legacy's display holds a popup now (what `hasPopup` answers).
	[[nodiscard]] bool showing() const { return recorder_.shown_.has_value(); }

private:
	/// @brief Legacy's display, reporting each call to the capture and keeping the one popup it shows.
	struct Recorder final : deluge::hid::Display {
		explicit Recorder(PopupCapture& capture) : capture_(&capture) {}
		constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
		void displayPopup(char const* text, int8_t num_flashes, bool /*align_right*/, uint8_t /*draw_dot*/,
		                  int32_t /*blink_speed*/, PopupType type) override {
			show(num_flashes == 0 ? PopupEvent::Kind::PERSISTENT : PopupEvent::Kind::POPUP, text, type);
		}
		void popupText(char const* text, PopupType type) override { show(PopupEvent::Kind::PERSISTENT, text, type); }
		void popupTextTemporary(char const* text, PopupType type) override {
			show(PopupEvent::Kind::TEMPORARY, text, type);
		}
		void cancelPopup() override {
			shown_.reset();
			capture_->add(PopupEvent::Kind::CANCEL, "", PopupEvent::Route::DISPLAY);
		}
		void freezeWithError(char const* text) override {
			capture_->add(PopupEvent::Kind::FREEZE, text != nullptr ? text : "", PopupEvent::Route::DISPLAY);
		}
		void displayError(Error error) override {
			shown_ = PopupType::GENERAL;
			capture_->add(PopupEvent::Kind::ERROR_CODE, std::to_string(static_cast<int32_t>(error)),
			              PopupEvent::Route::DISPLAY);
		}
		void removeWorkingAnimation() override {
			capture_->add(PopupEvent::Kind::CLEAR_WORKING, "", PopupEvent::Route::DISPLAY);
		}
		void displayLoadingAnimationText(char const* text, bool /*delayed*/, bool /*transparent*/) override {
			capture_->add(PopupEvent::Kind::WORKING, text != nullptr ? text : "", PopupEvent::Route::DISPLAY);
		}
		// `removeWorkingAnimation` on the one implementation there is (`hid/display/oled.h`).
		void removeLoadingAnimation() override { removeWorkingAnimation(); }
		void displayNotification(std::string_view title, std::optional<std::string_view> value) override {
			shown_ = PopupType::NOTIFICATION;
			capture_->add(PopupEvent::Kind::NOTIFICATION, banner(title, value.value_or("")),
			              PopupEvent::Route::DISPLAY);
		}
		bool hasPopup() override { return capture_->mode_ == Mode::RECORD && shown_.has_value(); }
		bool hasPopupOfType(PopupType type) override {
			return capture_->mode_ == Mode::RECORD && shown_.has_value() && *shown_ == type;
		}
		void consoleText(char const* text) override {
			capture_->add(PopupEvent::Kind::CONSOLE, text != nullptr ? text : "", PopupEvent::Route::DISPLAY);
		}
		void timerRoutine() override {}

		void show(PopupEvent::Kind kind, char const* text, PopupType type) {
			shown_ = type;
			capture_->add(kind, text != nullptr ? text : "", PopupEvent::Route::DISPLAY);
		}

		PopupCapture* capture_;
		/// The type of the popup on show, or nothing.
		std::optional<PopupType> shown_;
	};

	/// @brief The capture notices are shown to: the innermost in scope.
	static PopupCapture*& active() {
		static PopupCapture* capture = nullptr;
		return capture;
	}

	/// @brief A notice pushed, recorded as the event `notify_drain` turns it into; or popped, kept until the next event
	///        so legacy's display showing it is not recorded twice.
	static void observe(const deluge::base::notify::Notice& n, deluge::base::notify::NoticeTurn turn) {
		PopupCapture* capture = active();
		if (capture == nullptr) {
			return;
		}
		PopupEvent e = event_of(n);
		if (turn == deluge::base::notify::NoticeTurn::Pushed) {
			capture->add(e.kind, std::move(e.text), PopupEvent::Route::NOTICE);
			return;
		}
		// `notify_drain` takes a banner down with a plain cancel, and only when one is up.
		if (e.kind == PopupEvent::Kind::CANCEL_NOTIFICATION) {
			e.kind = PopupEvent::Kind::CANCEL;
		}
		capture->popped_ = std::move(e);
	}

	/// @return @p n as the event the user sees, its route left to the caller.
	static PopupEvent event_of(const deluge::base::notify::Notice& n) {
		using deluge::base::notify::Notice;
		const std::string text = n.string_id != 0 ? string_text(n.string_id) : std::string{n.text};
		switch (n.kind) {
		case Notice::Kind::Error:
			return {.kind = PopupEvent::Kind::ERROR_CODE, .text = std::to_string(n.error_code)};
		case Notice::Kind::Text:
			return {.kind =
			            n.severity == Notice::Severity::Warn ? PopupEvent::Kind::PERSISTENT : PopupEvent::Kind::POPUP,
			        .text = text};
		case Notice::Kind::Console:
			return {.kind = PopupEvent::Kind::CONSOLE, .text = text};
		case Notice::Kind::Working:
			return {.kind = PopupEvent::Kind::WORKING, .text = text};
		case Notice::Kind::ClearWorking:
			return {.kind = PopupEvent::Kind::CLEAR_WORKING};
		case Notice::Kind::CancelPopup:
			return {.kind = PopupEvent::Kind::CANCEL};
		case Notice::Kind::Notification:
			return {.kind = PopupEvent::Kind::NOTIFICATION,
			        .text = banner(n.notification_title(), n.notification_value())};
		case Notice::Kind::CancelNotification:
			return {.kind = PopupEvent::Kind::CANCEL_NOTIFICATION};
		}
		return {};
	}

	/// @return The English text of l10n string @p id.
	static std::string string_text(uint16_t id) { return deluge::l10n::get(static_cast<deluge::l10n::String>(id)); }

	/// @return A banner as an event's text: the title, a newline, the value.
	static std::string banner(std::string_view title, std::string_view value) {
		return std::string{title} + "\n" + std::string{value};
	}

	void add(PopupEvent::Kind kind, std::string text, PopupEvent::Route route) {
		const std::optional<PopupEvent> popped = std::exchange(popped_, std::nullopt);
		if (route == PopupEvent::Route::DISPLAY && popped.has_value() && popped->kind == kind && popped->text == text) {
			return; // the notice just popped, shown: recorded when it was pushed
		}
		if (mode_ == Mode::RECORD) {
			events_.push_back(PopupEvent{.kind = kind, .text = std::move(text), .route = route});
		}
	}

	Mode mode_;
	PopupCapture* outer_;
	deluge::hid::Display* saved_display_;
	Recorder recorder_;
	std::vector<PopupEvent> events_;
	/// The notice popped last, as legacy's display would show it, until the next event.
	std::optional<PopupEvent> popped_;
};

} // namespace deluge::ui::scenes::spec
