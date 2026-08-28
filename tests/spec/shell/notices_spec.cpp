#include "../gfx/support/golden.h"
#include "../ui/support/frames.h"
#include "shell/notices.h"
#include "shell/scene_stack.h"

#include "cppspec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "../support/spec_main.h"
using namespace deluge::ui::shell;
using deluge::notify::Notice;
using deluge::notify::NoticeQueue;

namespace {

/// @brief A `notify::Notice` with \p text and \p popup_type copied into its own 48-byte buffer,
///        of kind \p kind.
Notice make_notice(Notice::Kind kind, std::string_view text = {}, uint8_t popup_type = 0, int16_t error_code = 0) {
	Notice n{};
	n.kind = kind;
	n.popup_type = popup_type;
	n.error_code = error_code;
	const size_t len = std::min(text.size(), sizeof(n.text) - 1);
	std::memcpy(n.text, text.data(), len);
	n.text[len] = '\0';
	return n;
}

/// @brief A board profile with fixed `text`/`error_text` lookups, so `Notices::drain` has
///        something to resolve string/error ids through without reaching into l10n or `Error`.
BoardProfile fixed_lookup_profile() {
	static constexpr auto text_fn = +[](uint16_t) -> std::string_view { return "STRING"; };
	static constexpr auto error_fn = +[](int16_t) -> std::string_view { return "ERROR TEXT"; };
	BoardProfile p{};
	p.text = text_fn;
	p.error_text = error_fn;
	return p;
}

/// @brief A registry, stack, the three overlay scenes and a `Notices` wired over them -- the
///        `Shell`-less assembly the brief asks for. A real `notify::NoticeQueue`, constructed
///        directly rather than via the process-global `notices()`, is a separate member each spec
///        pushes into and hands to `drain()`.
struct Rig {
	BoardProfile profile = fixed_lookup_profile();
	PopupScene popup;
	ConsoleScene console;
	WorkingScene working;
	std::array<Scene*, 4> table{}; // indices 1=Popup, 2=Console, 3=Working (Null is 0, unused)
	SceneRegistry reg{};
	SceneStack stack{reg};
	Notices notices{stack, profile, popup, console, working};
	NoticeQueue queue;

	Rig() {
		table[static_cast<size_t>(SceneKind::Popup)] = &popup;
		table[static_cast<size_t>(SceneKind::Console)] = &console;
		table[static_cast<size_t>(SceneKind::Working)] = &working;
		reg.by_kind = std::span<Scene* const>{table};
	}

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
};

} // namespace

// clang-format off
describe shell_notices("shell::Notices", $ {
	it("a Text notice pushes the popup scene with the text and a General timeout", _ {
		Rig r;
		(void)r.queue.push(make_notice(Notice::Kind::Text, "SAVED"));

		r.notices.drain(r.queue, 0);

		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("SAVED"));

		// Not yet expired one ms before the deadline.
		r.notices.tick(r.notices.timeout_for(1) - 1, 0);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();

		r.notices.tick(r.notices.timeout_for(1) + 1, 0);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_false();
	});

	it("a second notice replaces the popup's text without pushing twice", _ {
		Rig r;
		(void)r.queue.push(make_notice(Notice::Kind::Text, "FIRST"));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(1));

		(void)r.queue.push(make_notice(Notice::Kind::Text, "SECOND"));
		r.notices.drain(r.queue, 10);

		expect(r.stack.depth()).to_equal(size_t(1)); // still just the one popup, not pushed twice
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("SECOND"));
	});

	it("Error uses error_text and popup_type 0 maps to General", _ {
		Rig r;
		(void)r.queue.push(make_notice(Notice::Kind::Error, {}, /*popup_type=*/0, /*error_code=*/7));

		r.notices.drain(r.queue, 0);

		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("ERROR TEXT"));

		// Popup_type 0 mapped to General (Framed): rendering shows a border, unlike Notification.
		deluge::ui::test::Rig48 general;
		r.popup.render_oled(SurfaceRole::Main, general.frame);
		deluge::ui::test::Rig48 notification;
		r.popup.show("ERROR TEXT", /*popup_type=*/10, 0, r.notices.timeout_for(10));
		r.popup.render_oled(SurfaceRole::Main, notification.frame);
		expect(general.pbm() == notification.pbm()).to_be_false();
	});

	it("Console appends and never occludes; Working arms and ClearWorking pops it", _ {
		Rig r;
		expect(r.console.oled_transparent(SurfaceRole::Main)).to_be_true();
		expect(r.console.occludes_grid()).to_be_false();
		expect(r.console.is_grid_overlay()).to_be_false();

		deluge::ui::test::Rig48 before;
		r.console.render_oled(SurfaceRole::Main, before.frame);
		(void)r.queue.push(make_notice(Notice::Kind::Console, "HELLO"));
		r.notices.drain(r.queue, 0);
		deluge::ui::test::Rig48 after;
		r.console.render_oled(SurfaceRole::Main, after.frame);
		expect(before.pbm() == after.pbm()).to_be_false(); // the appended line actually drew something

		(void)r.queue.push(make_notice(Notice::Kind::Working, "Loading"));
		r.notices.drain(r.queue, 0);
		expect(r.stack.top().kind() == SceneKind::Working).to_be_true();
		expect(r.working.armed()).to_be_true();

		(void)r.queue.push(make_notice(Notice::Kind::ClearWorking));
		r.notices.drain(r.queue, 0);
		expect(r.working.armed()).to_be_false();
		expect(r.stack.top().kind() == SceneKind::Working).to_be_true(); // drain() clears, doesn't pop

		r.notices.tick(0, 0);
		expect(r.stack.top().kind() == SceneKind::Working).to_be_false(); // tick() is what pops it
	});

	it("CancelPopup pops only a popup", _ {
		Rig r;
		// Nothing on the stack yet: CancelPopup is a no-op.
		(void)r.queue.push(make_notice(Notice::Kind::CancelPopup));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(0));

		// Working on top, not a popup: still a no-op.
		(void)r.queue.push(make_notice(Notice::Kind::Working, "Loading"));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(1));
		(void)r.queue.push(make_notice(Notice::Kind::CancelPopup));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(1));
		expect(r.stack.top().kind() == SceneKind::Working).to_be_true();

		// Now push a popup on top of it and cancel that.
		(void)r.queue.push(make_notice(Notice::Kind::Text, "SAVED"));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(2));
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		(void)r.queue.push(make_notice(Notice::Kind::CancelPopup));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(1));
		expect(r.stack.top().kind() == SceneKind::Working).to_be_true();
	});

	it("renders the popup goldens at both geometries", _ {
		deluge::ui::test::Rig48 saved48;
		PopupScene popup48;
		popup48.show("SAVED", /*popup_type=*/1, 0, 800);
		popup48.render_oled(SurfaceRole::Main, saved48.frame);
		expect_golden(self, "../shell/popup_saved_48.pbm", saved48.pbm());

		deluge::ui::test::Rig64 saved64;
		PopupScene popup64;
		popup64.show("SAVED", /*popup_type=*/1, 0, 800);
		popup64.render_oled(SurfaceRole::Main, saved64.frame);
		expect_golden(self, "../shell/popup_saved_64.pbm", saved64.pbm());

		// role != Main is a no-op.
		deluge::ui::test::Rig48 untouched;
		popup48.render_oled(SurfaceRole::Aux, untouched.frame);
		expect(untouched.pbm()).to_equal(deluge::ui::test::Rig48{}.pbm());
	});
});
// clang-format on

DELUGE_SPEC(shell_notices_spec, shell_notices)
