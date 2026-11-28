/// @file
/// @brief `spec::PopupCapture`: one event list for the three ways a popup reaches the screen, and what the user sees
///        alike whichever way it took.

#include "support/popup_capture.h"

#include "base/notify/notice_queue.h"
#include "gui/l10n/strings.h"
#include "hid/display/display.h"

#include "cppspec.hpp"

#include <string>
#include <vector>

#include "support/spec_main.h"

namespace {

using deluge::ui::scenes::spec::PopupCapture;
using deluge::ui::scenes::spec::PopupEvent;

/// @brief Empty the notice queue, which the capture watches but does not drain.
void drain_notices() {
	deluge::base::notify::Notice discard;
	while (deluge::base::notify::notices().pop(discard)) {}
}

/// @brief A scene's answer holding one `Popup` action for @p id.
deluge::ui::shell::Result popup_result(deluge::l10n::String id) {
	deluge::ui::shell::Result r;
	(void)r.actions.push_back(deluge::ui::shell::Action{.kind = deluge::ui::shell::Action::Kind::Popup,
	                                                    .popup_string = static_cast<uint16_t>(id)});
	return r;
}

} // namespace

// clang-format off
describe popup_capture_spec_body("spec::PopupCapture", $ {
	it("records a popup on legacy's display, a notice and a scene's Popup action as the same thing seen", _ {
		drain_notices();
		const std::string text = deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_LENGTH_ADJUSTED);
		PopupCapture capture;
		display->displayPopup(text.c_str());
		deluge::base::notify::notice_string(
		    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_CLIP_LENGTH_ADJUSTED));
		capture.take(popup_result(deluge::l10n::String::STRING_FOR_CLIP_LENGTH_ADJUSTED));
		const std::string seen = "popup: " + text;
		expect(capture.seen()).to_equal(std::vector<std::string>{seen, seen, seen});
		expect(capture.events().size()).to_equal(size_t{3});
		expect(capture.events()[0].route == PopupEvent::Route::DISPLAY).to_equal(true);
		expect(capture.events()[1].route == PopupEvent::Route::NOTICE).to_equal(true);
		expect(capture.events()[2].route == PopupEvent::Route::ACTION).to_equal(true);
		drain_notices();
	});

	it("sees a popup that stays the same whether the display was asked for it or a notice warned", _ {
		drain_notices();
		PopupCapture capture;
		display->popupText("16TH-NOTES");
		deluge::base::notify::Notice n{.kind = deluge::base::notify::Notice::Kind::Text,
		                               .severity = deluge::base::notify::Notice::Severity::Warn};
		std::string{"16TH-NOTES"}.copy(n.text, sizeof(n.text) - 1);
		(void)deluge::base::notify::notices().push(n);
		display->displayPopup("16TH-NOTES", 0);
		expect(capture.seen_text())
		    .to_equal(std::string{"persistent: 16TH-NOTES\npersistent: 16TH-NOTES\npersistent: 16TH-NOTES\n"});
		drain_notices();
	});

	it("sees a banner, an error, a cancel and the working animation alike on either route", _ {
		drain_notices();
		PopupCapture capture;
		display->displayNotification("LPF MODE", "12DB");
		deluge::base::notify::notice_notification("LPF MODE", "12DB");
		display->displayError(Error::SD_CARD);
		deluge::base::notify::notice_error_code(static_cast<int16_t>(Error::SD_CARD));
		display->cancelPopup();
		deluge::base::notify::notice_kind(deluge::base::notify::Notice::Kind::CancelPopup);
		display->displayLoadingAnimationText("LOADING");
		deluge::base::notify::notice_text("LOADING", deluge::base::notify::Notice::Kind::Working);
		const std::vector<std::string> seen = capture.seen();
		expect(seen.size()).to_equal(size_t{8});
		for (size_t i = 0; i + 1 < seen.size(); i += 2) {
			expect(seen[i]).to_equal(seen[i + 1]);
		}
		expect(seen[0]).to_equal(std::string{"notification: LPF MODE\n12DB"});
		drain_notices();
	});

	it("answers hasPopup and hasPopupOfType as the popup on show, until it is cancelled", _ {
		PopupCapture capture;
		expect(display->hasPopup()).to_equal(false);
		display->displayNotification("TITLE", "VALUE");
		expect(display->hasPopupOfType(PopupType::NOTIFICATION)).to_equal(true);
		expect(display->hasPopupOfType(PopupType::GENERAL)).to_equal(false);
		display->cancelPopup();
		expect(capture.showing()).to_equal(false);
	});

	it("records nothing when quiet, and gives legacy's display back when it goes", _ {
		drain_notices();
		deluge::hid::Display* const before = display;
		{
			PopupCapture quiet{PopupCapture::Mode::QUIET};
			display->displayPopup("ANY");
			deluge::base::notify::notice_text("ANY");
			expect(quiet.events().empty()).to_equal(true);
		}
		expect(display == before).to_equal(true);
		drain_notices();
	});

	it("hands the notices back to an outer capture when an inner one goes", _ {
		drain_notices();
		PopupCapture outer;
		{
			PopupCapture inner;
			deluge::base::notify::notice_text("INNER");
			expect(inner.seen()).to_equal(std::vector<std::string>{"popup: INNER"});
		}
		deluge::base::notify::notice_text("OUTER");
		expect(outer.seen()).to_equal(std::vector<std::string>{"popup: OUTER"});
		drain_notices();
	});
});
// clang-format on

DELUGE_SPEC(popup_capture_spec, popup_capture_spec_body)
