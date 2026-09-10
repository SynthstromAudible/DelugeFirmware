/// @file
/// @brief Three optional `Binding` hooks (E-note-family task 3): `adjust`, `value_text` and
///        `permitted` -- driven through a real `Shell` over `uitest::FakeSurfaceSet`, the same rig
///        `menu_horizontal_controls_spec.cpp` uses, plus one direct `render_oled` call (as
///        `menu_scene_spec.cpp`'s `firmware_version` example does) for the one check that needs
///        pixels rather than behaviour.
///
/// The tree under test is declared here rather than taken from `src/scenes/menu/nodes/`: no
/// shipped node sets any of these three hooks yet (they arrive with the note family's own leaves
/// in a later task), so there is nothing in the real tables this file could exercise.
///
/// Each root below is a single-column horizontal page, on purpose: column 0 is already focused the
/// moment the page opens, so a select turn edits it with no instrument-button focus dance first --
/// this file's subject is the three hooks, not column focus, which
/// `menu_horizontal_controls_spec.cpp` already covers.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/profile.h"
#include "scenes/registry.h"
#include "ui/oled/columns.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "model/song/song.h"

#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"
#include "spec/ui/support/frames.h"

#include "cppspec.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
using deluge::ui::shell::SurfaceRole;

namespace {

namespace menu = deluge::scenes::menu;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

// ---------------------------------------------------------------------------------------------
// One single-column horizontal root per hook, so each example needs no column-focus setup.

// --- `Binding::adjust` (spec §5.2): a select turn accumulates and commits through the hook, and
//     never through `set`. ---
int32_t g_adjust_get_value = 64;
int32_t g_adjust_calls = 0;
int32_t g_adjust_last_offset = 0;
bool g_adjust_set_called = false;
int32_t get_adjust_col(const menu::MenuContext&) {
	return g_adjust_get_value;
}
void set_adjust_col(const menu::MenuContext&, int32_t) {
	g_adjust_set_called = true;
}
void do_adjust_col(const menu::MenuContext&, int32_t offset) {
	g_adjust_calls++;
	g_adjust_last_offset = offset;
}
constexpr menu::Node adjust_col = menu::with_adjust(menu::int_item(sid(String::STRING_FOR_VELOCITY), get_adjust_col,
                                                                   set_adjust_col, deluge::ui::oled::Range{0, 127, 1}),
                                                    do_adjust_col);
constexpr const menu::Node* kAdjustKids[] = {&adjust_col};
constexpr menu::Node adjust_page =
    menu::submenu(sid(String::STRING_FOR_VELOCITY), kAdjustKids, menu::Layout::Horizontal);

// --- `Binding::permitted` (spec §5.3): a select turn changes nothing, and calls neither `adjust`
//     nor `set`, while the hook answers false. ---
bool g_permit = false;
int32_t g_permit_get_value = 5;
int32_t g_permit_set_calls = 0;
bool permitted_permit_col(const menu::MenuContext&) {
	return g_permit;
}
int32_t get_permit_col(const menu::MenuContext&) {
	return g_permit_get_value;
}
void set_permit_col(const menu::MenuContext&, int32_t) {
	g_permit_set_calls++;
}
constexpr menu::Node permit_col = menu::with_permitted(
    menu::int_item(sid(String::STRING_FOR_PAN), get_permit_col, set_permit_col, deluge::ui::oled::Range{0, 50, 1}),
    permitted_permit_col);
constexpr const menu::Node* kPermitKids[] = {&permit_col};
constexpr menu::Node permit_page = menu::submenu(sid(String::STRING_FOR_PAN), kPermitKids, menu::Layout::Horizontal);

// --- `Binding::value_text` (spec §5.3): the notification prints the hook's own
//     `ValueTextUse::Notification` text in place of `format_int`. ---
int32_t g_text_get_value = 35;
int32_t g_text_set_calls = 0;
int32_t get_text_col(const menu::MenuContext&) {
	return g_text_get_value;
}
void set_text_col(const menu::MenuContext&, int32_t v) {
	g_text_get_value = v;
	g_text_set_calls++;
}
menu::ValueText value_text_text_col(const menu::MenuContext&, int32_t, menu::ValueTextUse use, std::span<char> out) {
	const std::string_view s =
	    (use == menu::ValueTextUse::Column) ? std::string_view{"OK"} : std::string_view{"35% ltch"};
	const size_t n = std::min(s.size(), out.size());
	std::memcpy(out.data(), s.data(), n);
	return menu::ValueText{.length = n, .strikethrough = false};
}
constexpr menu::Node text_col = menu::with_value_text(
    menu::int_item(sid(String::STRING_FOR_FILL), get_text_col, set_text_col, deluge::ui::oled::Range{0, 100, 1}),
    value_text_text_col);
constexpr const menu::Node* kTextKids[] = {&text_col};
constexpr menu::Node text_page = menu::submenu(sid(String::STRING_FOR_FILL), kTextKids, menu::Layout::Horizontal);

// --- `Binding::value_text` again, this time for the column: `strikethrough` reaches the rendered
//     `TextColumn` (the pixel-level check against `note/fill.h:70-79` itself lives in
//     `columns_spec.cpp`; this only proves `render_horizontal` wires the flag through at all). An
//     empty `Column`-use string isolates the strike line from any glyph ink. ---
menu::ValueText value_text_strike_col(const menu::MenuContext&, int32_t, menu::ValueTextUse use, std::span<char>) {
	return menu::ValueText{.length = 0, .strikethrough = use == menu::ValueTextUse::Column};
}
int32_t get_strike_col(const menu::MenuContext&) {
	return 0;
}
void set_strike_col(const menu::MenuContext&, int32_t) {
}
constexpr menu::Node strike_col = menu::with_value_text(
    menu::int_item(sid(String::STRING_FOR_FILL), get_strike_col, set_strike_col, deluge::ui::oled::Range{0, 1, 1}),
    value_text_strike_col);
constexpr const menu::Node* kStrikeKids[] = {&strike_col};
constexpr menu::Node strike_page = menu::submenu(sid(String::STRING_FOR_FILL), kStrikeKids, menu::Layout::Horizontal);

constexpr const menu::Node* kRoots[] = {&adjust_page, &permit_page, &text_page, &strike_page};

// ---------------------------------------------------------------------------------------------
// Board fixture -- copied from `menu_horizontal_controls_spec.cpp`'s `Rig`, trimmed to what this
// file needs (no instrument buttons: every page here is one column, already focused).

constexpr uint8_t kSelectEncoder = 3;

struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	explicit Rig(const BoardProfile& p = deluge_profile()) : profile(p), fake(deluge::ui::shell::kDelugeBoard, p) {}

	menu::MenuContext open(const menu::Node& root) {
		const menu::MenuContext ctx =
		    menu::context_for(*song.song, song.clip, 0, false, false, false, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(kRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		return ctx;
	}

	void edges(uint8_t index, int16_t count, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, index, 0, count}, at_ms);
	}
	void turn(int32_t detents, uint32_t at_ms) { edges(kSelectEncoder, static_cast<int16_t>(detents * 2), at_ms); }
};

std::string last_notice() {
	deluge::notify::Notice n;
	std::string text;
	while (deluge::notify::notices().pop(n)) {
		text = n.text;
	}
	return text;
}

} // namespace

// clang-format off
describe menu_binding_hooks("scenes::menu::MenuScene Binding hooks (adjust, value_text, permitted)", $ {
	it("a select turn on an adjust node calls adjust once, with the accumulated offset, at commit -- never set", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(adjust_page);
		r.shell.tick(0);
		g_adjust_get_value = 64;
		g_adjust_calls = 0;
		g_adjust_last_offset = 0;
		g_adjust_set_called = false;

		r.turn(3, 10);
		r.shell.tick(20);

		expect(g_adjust_calls).to_equal(1);
		expect(g_adjust_last_offset).to_equal(3);
		expect(g_adjust_set_called).to_be_false();
	});

	it("three separate turns before a settle still commit once, with the sum of the detents", _ {
		// `render_horizontal`'s edit block accumulates into `MenuState::edit_offset` on every turn;
		// `commit_value` applies it once and clears it. Three turns between ticks exercise the
		// accumulation itself, not just a single-turn passthrough.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(adjust_page);
		r.shell.tick(0);
		g_adjust_calls = 0;
		g_adjust_last_offset = 0;

		r.turn(1, 10);
		r.turn(1, 12);
		r.turn(1, 14);
		r.shell.tick(20);

		expect(g_adjust_calls).to_equal(1);
		expect(g_adjust_last_offset).to_equal(3);
	});

	it("a select turn on a permitted-false node changes nothing and calls neither adjust nor set", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(permit_page);
		r.shell.tick(0);
		g_permit = false;
		g_permit_get_value = 5;
		g_permit_set_calls = 0;

		r.turn(2, 10);
		r.shell.tick(20);

		expect(g_permit_get_value).to_equal(5);
		expect(g_permit_set_calls).to_equal(0);
	});

	it("the same node edits normally once permitted answers true", _ {
		// Confirms the false case above is really the hook's answer and not some other block on
		// this node -- flipping `g_permit` to true and turning again edits it like any plain node.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(permit_page);
		r.shell.tick(0);
		g_permit = true;
		g_permit_get_value = 5;
		g_permit_set_calls = 0;

		r.turn(2, 10);
		r.shell.tick(20);

		expect(g_permit_set_calls > 0).to_be_true();
	});

	it("a value_text node's post-edit notification is 'name: ' + its Notification text", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(text_page);
		r.shell.tick(0);
		g_text_get_value = 35;

		(void)last_notice(); // drain
		r.turn(1, 10);
		r.shell.tick(20);

		const std::string notice = last_notice();
		deluge::ui::shell::TextScratch name_scratch;
		const std::string expected_name(
		    deluge::scenes::deluge_profile().text_or_digits(sid(String::STRING_FOR_FILL), name_scratch));
		expect(notice == (expected_name + ": 35% ltch")).to_be_true();
	});

	it("value_text's Column strikethrough reaches the rendered column", _ {
		// Not through the `Shell` at all -- a direct `render_oled` call, as
		// `menu_scene_spec.cpp`'s `firmware_version` example does, because this is the one check in
		// this file that needs pixels rather than behaviour. The pixel-perfect transcription of
		// `note/fill.h:70-79` itself is `columns_spec.cpp`'s job (`TextColumn::strikethrough`'s own
		// example); this only proves `render_horizontal` wires the flag through to begin with.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(strike_page);

		deluge::ui::test::Rig48 rig48;
		rig48.begin_tick(0);
		menu_scene().render_oled(SurfaceRole::Main, rig48.frame);

		// Single column, single slot: `column_width = rect.w / 4 = 32`, `box_width = 32`, so the
		// value region is `{current_x=0, base_y=19, w=31, h=16}` (`kHorizontalMenuBaseY=14` +
		// `Rig48`'s `visible_top=5`, `kHorizontalBoxHeight=25` less the always-on label's
		// `kLabelHeight=9`). Inside `text_column`, `b = slot_frame.bounds() = {0,0,31,16}`, so
		// `center_y = b.y + kHorizontalSlotYOffset(2) + 4 = 6` local -> canvas row `5 + 14 + 6 =
		// 25`; `x0 = b.x + 2 = 2`, `x1 = b.x + b.w - 4 = 27`, both frame-relative == canvas (the
		// column's own x origin is 0). The hook's `Column`-use text is empty, so nothing else draws
		// in this row: a lit pixel here can only be the strike line.
		bool any_lit = false;
		for (int32_t x = 2; x <= 27; x++) {
			if (rig48.lit(x, 25)) {
				any_lit = true;
			}
		}
		expect(any_lit).to_be_true();
	});
});
// clang-format on
DELUGE_SPEC(menu_binding_hooks_spec, menu_binding_hooks)
