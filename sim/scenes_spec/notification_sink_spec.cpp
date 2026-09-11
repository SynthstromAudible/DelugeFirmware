/// @file
/// @brief Note-family Task 8b: the horizontal-menu notification's two consumers in the application.
///
/// In the application two consumers pop the one notice queue: legacy's `drain_notifications`
/// (`gui/ui/ui.cpp:387`) and the shell's `Notices::drain` (`shell/shell.cpp:327`). A
/// `Notice::Kind::Notification` must end in the same legacy call whichever pops it --
/// `display->displayNotification(title, value)`, the call `HorizontalMenu::displayNotification`
/// makes itself (`gui/menu_item/horizontal_menu.cpp:342`) -- so legacy's one popup slot holds a
/// `PopupType::NOTIFICATION` either way. Both consumers are driven here against a real
/// `deluge::hid::display::OLED`, and each is compared byte for byte against that direct call.
///
/// This lives in the scenes spec rather than `tests/spec/gui/` because that suite does not build
/// `notify_drain.cpp` (`tests/spec/CMakeLists.txt` compiles only `notify_drain_mapping.cpp`), while
/// this executable links the real `deluge_app`, as `count_in_spec.cpp` already relies on.

#include "gui/ui/notify_drain.h"
#include "gui/ui/shell_notification_sink.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "notify/dirty_set.h"
#include "notify/notice_queue.h"
#include "scenes/registry.h"
#include "shell/notices.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

namespace {

using deluge::hid::display::OLED;
using deluge::notify::Notice;

/// @brief A real `OLED` installed as `display` for one example's scope; the previous `display` and
///        an empty popup slot are restored on the way out.
struct LegacyDisplay {
	OLED oled;
	decltype(display) saved = display;
	LegacyDisplay() {
		display = &oled;
		oled.cancelPopup();
		OLED::popup.clear();
		Notice discard;
		while (deluge::notify::notices().pop(discard)) {}
		(void)deluge::notify::dirty().drain();
	}
	~LegacyDisplay() {
		oled.cancelPopup();
		OLED::popup.clear();
		display = saved;
	}
	LegacyDisplay(const LegacyDisplay&) = delete;
	LegacyDisplay& operator=(const LegacyDisplay&) = delete;
};

/// @return The bytes of legacy's popup canvas, `OLED::popup`.
std::vector<uint8_t> popup_bytes() {
	const auto bytes = OLED::popup.bytes();
	return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

/// @brief What legacy's own call leaves in the popup slot: `HorizontalMenu::displayNotification`'s
///        `display->displayNotification(menuItem->getName(), notificationValueBuf.data())`
///        (`horizontal_menu.cpp:342`), made directly and then cleared again.
std::vector<uint8_t> direct_call(const char* title, const char* value) {
	OLED::popup.clear();
	display->displayNotification(title, value);
	std::vector<uint8_t> out = popup_bytes();
	display->cancelPopup();
	OLED::popup.clear();
	return out;
}

/// @brief The application's notification sink, installed for one example's scope.
struct AppSink {
	AppSink() { deluge::gui::ui::wireShellNotificationSink(); }
	~AppSink() { deluge::ui::shell::set_notification_sink(nullptr); }
	AppSink(const AppSink&) = delete;
	AppSink& operator=(const AppSink&) = delete;
};

/// @brief A `Shell` over the fake surfaces with the application's registry, as the other scenes
///        specs build it.
struct ShellRig {
	deluge::ui::shell::SceneRegistry reg = deluge::scenes::scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge::ui::shell::deluge_profile()};
	deluge::ui::shell::Shell shell{fake, deluge::ui::shell::deluge_profile(), reg, deluge::ui::shell::Services{},
	                               deluge::ui::shell::ShellConfig{}};
	ShellRig() = default;
	ShellRig(const ShellRig&) = delete;
	ShellRig& operator=(const ShellRig&) = delete;
	~ShellRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(deluge::ui::shell::SceneKind::Null);
		}
	}
};

bool any_lit(const std::vector<uint8_t>& bytes) {
	for (uint8_t b : bytes) {
		if (b != 0) {
			return true;
		}
	}
	return false;
}

} // namespace

// clang-format off
describe notification_sink_spec_body("the horizontal-menu notification's two application consumers", $ {
	it("legacy's drain_notifications maps a Notification to display->displayNotification(title, value)", _ {
		LegacyDisplay legacy;
		const std::vector<uint8_t> expected = direct_call("Probability", "100%");
		expect(any_lit(expected)).to_be_true();

		deluge::notify::notice_notification("Probability", "100%");
		drain_notifications();

		expect(display->hasPopupOfType(PopupType::NOTIFICATION)).to_be_true();
		expect(popup_bytes() == expected).to_be_true();
	});

	it("legacy's drain maps CancelNotification to the guarded cancelPopup: a notification goes, a "
	   "general popup stays", _ {
		// `if (display->hasPopupOfType(PopupType::NOTIFICATION)) { display->cancelPopup(); }`
		// (`horizontal_menu.cpp:363-365`, `:587-589`).
		LegacyDisplay legacy;
		deluge::notify::notice_notification("Probability", "100%");
		drain_notifications();
		expect(display->hasPopupOfType(PopupType::NOTIFICATION)).to_be_true();

		deluge::notify::notice_kind(Notice::Kind::CancelNotification);
		drain_notifications();
		expect(display->hasPopupOfType(PopupType::NOTIFICATION)).to_be_false();

		display->displayPopup("SAVED");
		expect(display->hasPopupOfType(PopupType::GENERAL)).to_be_true();
		deluge::notify::notice_kind(Notice::Kind::CancelNotification);
		drain_notifications();
		expect(display->hasPopupOfType(PopupType::GENERAL)).to_be_true();
	});

	it("with the application's sink installed, the shell's drain ends in the identical legacy call "
	   "and pushes no banner", _ {
		LegacyDisplay legacy;
		const std::vector<uint8_t> expected = direct_call("Probability", "100%");
		AppSink sink;
		expect(deluge::ui::shell::notification_sink() != nullptr).to_be_true();
		ShellRig r;

		deluge::notify::notice_notification("Probability", "100%");
		r.shell.tick(0); // step 2, `Notices::drain`, pops it -- legacy's drain never runs here
		expect(deluge::notify::notices().size()).to_equal(size_t{0});

		expect(display->hasPopupOfType(PopupType::NOTIFICATION)).to_be_true();
		expect(popup_bytes() == expected).to_be_true();
		expect(r.shell.state_lines().find("scene Notification") == std::string::npos).to_be_true();

		deluge::notify::notice_kind(Notice::Kind::CancelNotification);
		r.shell.tick(10);
		expect(display->hasPopupOfType(PopupType::NOTIFICATION)).to_be_false();
	});

	it("with no sink, a replacement banner reaches the panel on the tick that drains it, with no input", _ {
		// Legacy's replacement is a redraw as well as a rewrite: `displayNotification` ends in
		// `markChanged()` (`oled.cpp:793`), so the new title is on the panel at once.
		expect(deluge::ui::shell::notification_sink() == nullptr).to_be_true();
		Notice discard;
		while (deluge::notify::notices().pop(discard)) {}
		ShellRig r;
		r.shell.tick(0);
		const std::string blank = r.fake.pbm(0);

		deluge::notify::notice_notification("Probability", "100%");
		r.shell.tick(5);
		expect(r.shell.state_lines().find("scene Notification") != std::string::npos).to_be_true();
		const std::string first = r.fake.pbm(0);
		expect(first != blank).to_be_true(); // the first banner really reached the panel

		deluge::notify::notice_notification("Iterance", "OFF");
		r.shell.tick(10); // quiet: no pad, button or encoder, only the drain
		expect(r.fake.pbm(0) != first).to_be_true();
	});
});
DELUGE_SPEC(notification_sink_spec, notification_sink_spec_body)
