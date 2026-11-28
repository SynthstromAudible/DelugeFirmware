#pragma once

/// @file
/// @brief Every popup the code under test raises, whichever way it reaches the screen: a call on legacy's display, a
///        notice (which legacy's display or the shell's popup scene then shows), or a `Popup` action in a scene's
///        `Result`. A spec asserts on what the user would see (`seen`), so a port that moves a popup from one route to
///        another keeps its spec; `events` keeps the route for the few specs that pin one.
///
/// A `PopupCapture` installs itself for its scope: legacy's `display` points at its recorder, and every notice pushed
/// is shown to it as it is pushed (`notify::set_notice_observer`), whoever drains the queue later. One is active at a
/// time; a nested one takes over and gives the outer one back when it goes.

#include "base/notify/notice_queue.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/language.h"
#include "gui/l10n/strings.h"
#include "hid/display/display.h"
#include "ui/shell/scene.h"

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

/// @brief Records every popup event while it is in scope. See the file docstring.
class PopupCapture {
public:
	/// What the capture keeps.
	enum class Mode : uint8_t {
		RECORD, ///< Every event.
		QUIET,  ///< Nothing: a display for code that needs one to call, whose popups no example asks about.
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
		bool hasPopup() override { return shown_.has_value(); }
		bool hasPopupOfType(PopupType type) override { return shown_.has_value() && *shown_ == type; }
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

	/// @brief A notice as it is pushed, as the event `notify_drain` turns it into.
	static void observe(const deluge::base::notify::Notice& n) {
		PopupCapture* capture = active();
		if (capture == nullptr) {
			return;
		}
		using deluge::base::notify::Notice;
		const std::string text = n.string_id != 0 ? string_text(n.string_id) : std::string{n.text};
		switch (n.kind) {
		case Notice::Kind::Error:
			capture->add(PopupEvent::Kind::ERROR_CODE, std::to_string(n.error_code), PopupEvent::Route::NOTICE);
			break;
		case Notice::Kind::Text:
			capture->add(n.severity == Notice::Severity::Warn ? PopupEvent::Kind::PERSISTENT : PopupEvent::Kind::POPUP,
			             text, PopupEvent::Route::NOTICE);
			break;
		case Notice::Kind::Console:
			capture->add(PopupEvent::Kind::CONSOLE, text, PopupEvent::Route::NOTICE);
			break;
		case Notice::Kind::Working:
			capture->add(PopupEvent::Kind::WORKING, text, PopupEvent::Route::NOTICE);
			break;
		case Notice::Kind::ClearWorking:
			capture->add(PopupEvent::Kind::CLEAR_WORKING, "", PopupEvent::Route::NOTICE);
			break;
		case Notice::Kind::CancelPopup:
			capture->add(PopupEvent::Kind::CANCEL, "", PopupEvent::Route::NOTICE);
			break;
		case Notice::Kind::Notification:
			capture->add(PopupEvent::Kind::NOTIFICATION, banner(n.notification_title(), n.notification_value()),
			             PopupEvent::Route::NOTICE);
			break;
		case Notice::Kind::CancelNotification:
			capture->add(PopupEvent::Kind::CANCEL_NOTIFICATION, "", PopupEvent::Route::NOTICE);
			break;
		}
	}

	/// @return The English text of l10n string @p id.
	static std::string string_text(uint16_t id) { return deluge::l10n::get(static_cast<deluge::l10n::String>(id)); }

	/// @return A banner as an event's text: the title, a newline, the value.
	static std::string banner(std::string_view title, std::string_view value) {
		return std::string{title} + "\n" + std::string{value};
	}

	void add(PopupEvent::Kind kind, std::string text, PopupEvent::Route route) {
		if (mode_ == Mode::RECORD) {
			events_.push_back(PopupEvent{.kind = kind, .text = std::move(text), .route = route});
		}
	}

	Mode mode_;
	PopupCapture* outer_;
	deluge::hid::Display* saved_display_;
	Recorder recorder_;
	std::vector<PopupEvent> events_;
};

} // namespace deluge::ui::scenes::spec
