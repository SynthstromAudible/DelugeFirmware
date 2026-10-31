/// @file
/// @brief `MenuScene` horizontal-page controls (E-note-family task 2): the select turn edits the
///        focused column, the instrument buttons focus and page-switch, and a select press on a
///        value column follows its chain or ascends -- all driven through a real `Shell` over
///        `uitest::FakeSurfaceSet`, the same rig `menu_scene_spec.cpp` uses.
///
/// The tree under test is declared here rather than taken from `src/scenes/menu/nodes/`, for the
/// same reason `menu_scene_spec.cpp`'s is: this task's subject is the interpreter, not the data,
/// and the shipped `nodes::lpf` (four value columns, no submenu column, no range-dependent one) is
/// not shaped to exercise every arm this file has to -- `is_submenu`'s no-op, `select_chain`'s
/// both branches, and `Permission::MustSelectRange` reached by a button, all need a column the
/// real table does not have. `nodes::lpf` is still what the level-2 acceptance script
/// (`tests/ui-scripts/shell/menu_submenu_horizontal.txt`) opens; this file's local tree is the
/// toolkit-level counterpart `columns_spec.cpp` already established the pattern for.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/detail.h" // with_slots
#include "scenes/menu/nodes/nodes.h"  // the real `nodes::randomizer`/`randomizer_lock`
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/notices.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "definitions_cxx.hpp"
#include "gfx/oled/icons.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "model/edits/setting_edit.h"
#include "model/mod_controllable/mod_fx_names.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/arpeggiator_rhythms.h"
#include "modulation/params/param.h"
#include "modulation/sync.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"

#include "support/menu_engine.h"
#include "support/no_notification_sink.h"
#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"
#include "spec/ui/support/frames.h"

#include "cppspec.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::ButtonSet;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

using deluge::scenes::spec::NoNotificationSink;

namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

// ---------------------------------------------------------------------------------------------
// The tree under test: a seven-column, two-page horizontal page.
//
// Page 1 (four one-slot columns, buttons SYNTH/KIT/MIDI/CV in order):
//   0 SYNTH  ENUM_COL    Kind::Enum, no chain      -- turn-edits-value, and the "ascends" half of
//                                                     the select-press test
//   1 KIT    TOGGLE_COL  Kind::Toggle               -- the already-focused-press/notify example,
//                                                     and the select-press flip-and-stay example
//   2 MIDI   SUBMENU_COL Kind::Submenu              -- the turn-does-nothing example, and the
//                                                     no-notification-on-focus example
//   3 CV     CHAIN_COL   Kind::Int, with select_chain -- the "enters the chain target" half
// Page 2 (three one-slot columns, SYNTH/KIT/MIDI again once paged across):
//   4 SYNTH  RANGE_COL   Kind::Int, range_dependent -- the MustSelectRange button-focus example
//   5 KIT    FILLER_COL  Kind::Int, plain           -- pads the page, and a neutral stop between
//                                                     RANGE_COL and SESSION_COL below
//   6 MIDI   SESSION_COL Kind::Int, range_dependent AND allow_session_from_horizontal -- the
//                                                     button-focus/already-focused-press pair

int32_t g_enum_col = 0;
constexpr uint16_t kShortEnumNames[] = {sid(String::STRING_FOR_UP), sid(String::STRING_FOR_RANDOM)};
constexpr uint16_t kEnumColNames[] = {sid(String::STRING_FOR_DISABLED), sid(String::STRING_FOR_ENABLED),
                                      sid(String::STRING_FOR_OFF)};
int32_t get_enum_col(const MenuContext&) {
	return g_enum_col;
}
void set_enum_col(const MenuContext&, int32_t v) {
	g_enum_col = v;
}

bool g_toggle_col = false;
int32_t get_toggle_col(const MenuContext&) {
	return g_toggle_col ? 1 : 0;
}
void set_toggle_col(const MenuContext&, int32_t v) {
	g_toggle_col = v != 0;
}

int32_t g_sub_leaf = 0;
int32_t get_sub_leaf(const MenuContext&) {
	return g_sub_leaf;
}
void set_sub_leaf(const MenuContext&, int32_t v) {
	g_sub_leaf = v;
}
constexpr Node sub_leaf =
    menu::int_item(sid(String::STRING_FOR_PAN), get_sub_leaf, set_sub_leaf, deluge::ui::oled::Range{0, 50, 1});
constexpr const Node* kSubmenuColChildren[] = {&sub_leaf};
constexpr Node submenu_col = menu::submenu(sid(String::STRING_FOR_FILTERS), kSubmenuColChildren);

int32_t g_chain_col = 0;
int32_t get_chain_col(const MenuContext&) {
	return g_chain_col;
}
void set_chain_col(const MenuContext&, int32_t v) {
	g_chain_col = v;
}
int32_t g_chain_target = 0;
int32_t get_chain_target(const MenuContext&) {
	return g_chain_target;
}
void set_chain_target(const MenuContext&, int32_t v) {
	g_chain_target = v;
}
constexpr Node chain_target = menu::int_item(sid(String::STRING_FOR_TEMPO), get_chain_target, set_chain_target,
                                             deluge::ui::oled::Range{0, 50, 1});
constexpr const Node* kChainColTargets[] = {&chain_target};
const Node* chain_col_chain(const MenuContext&, const menu::MenuState&) {
	return &chain_target;
}
constexpr Node chain_col = menu::with_select_chain(
    menu::int_item(sid(String::STRING_FOR_RESONANCE), get_chain_col, set_chain_col, deluge::ui::oled::Range{0, 50, 1}),
    chain_col_chain, kChainColTargets);

// The range-dependent column: a real `bind.param` so `permission_for`/`ensure_first_range` have a
// source to ask, exactly as `menu_scene_spec.cpp`'s own `transpose_leaf` is built.
constexpr Node range_col = menu::with_range_dependent(
    menu::param(sid(String::STRING_FOR_TRANSPOSE),
                {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_FREQ}));

int32_t g_filler_col = 0;
int32_t get_filler_col(const MenuContext&) {
	return g_filler_col;
}
void set_filler_col(const MenuContext&, int32_t v) {
	g_filler_col = v;
}
constexpr Node filler_col =
    menu::int_item(sid(String::STRING_FOR_FEEDBACK), get_filler_col, set_filler_col, deluge::ui::oled::Range{0, 50, 1});

// A range-dependent column with `allow_session_from_horizontal` also set -- the port of one of
// `FileSelector`/`osc::AudioRecorder`/`sample::Start`/`sample::End`/`audio_clip::SampleMarkerEditor`
// (`Node::allow_session_from_horizontal`'s own doc). Shares `range_col`'s param on purpose: the two
// columns differ only in this one bit, so a test can hold everything else equal.
constexpr Node session_col = menu::with_allow_session_from_horizontal(menu::with_range_dependent(
    menu::param(sid(String::STRING_FOR_TRANSPOSE),
                {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_FREQ})));

constexpr Node enum_col = menu::enumeration(sid(String::STRING_FOR_MODE), kEnumColNames, get_enum_col, set_enum_col);
constexpr Node toggle_col = menu::toggle_item(sid(String::STRING_FOR_MONOPHONIC), get_toggle_col, set_toggle_col);

constexpr const Node* kHorizChildren[] = {&enum_col,  &toggle_col, &submenu_col, &chain_col,
                                          &range_col, &filler_col, &session_col};
constexpr Node horiz_page = menu::submenu(sid(String::STRING_FOR_LPF), kHorizChildren, menu::Layout::Horizontal);

/// A FIXED page (`HorizontalMenu::Layout::FIXED`): two columns are never relevant, one on each page.
bool never_relevant(const MenuContext&) {
	return false;
}
constexpr Node fixed_hidden_a = menu::int_item(sid(String::STRING_FOR_FEEDBACK), get_filler_col, set_filler_col,
                                               deluge::ui::oled::Range{0, 50, 1}, menu::Hint::None, never_relevant);
constexpr Node fixed_hidden_b = menu::int_item(sid(String::STRING_FOR_RESONANCE), get_filler_col, set_filler_col,
                                               deluge::ui::oled::Range{0, 50, 1}, menu::Hint::None, never_relevant);
constexpr const Node* kFixedChildren[] = {&enum_col,   &fixed_hidden_a, &toggle_col,
                                          &filler_col, &fixed_hidden_b, &chain_col};
constexpr Node fixed_page =
    menu::with_fixed_columns(menu::submenu(sid(String::STRING_FOR_VOICE), kFixedChildren, menu::Layout::Horizontal));
constexpr Node dynamic_page = menu::submenu(sid(String::STRING_FOR_VOICE), kFixedChildren, menu::Layout::Horizontal);

/// A group whose middle member lists nothing: it contributes no page (`HorizontalMenuGroup::switchVisiblePage` skips a
/// member with 0 pages).
constexpr const Node* kGroupPageAChildren[] = {&enum_col, &toggle_col};
constexpr Node group_page_a = menu::submenu(sid(String::STRING_FOR_LPF), kGroupPageAChildren, menu::Layout::Horizontal);
constexpr const Node* kGroupEmptyChildren[] = {&fixed_hidden_a, &fixed_hidden_b};
constexpr Node group_page_empty =
    menu::submenu(sid(String::STRING_FOR_HPF), kGroupEmptyChildren, menu::Layout::Horizontal);
constexpr const Node* kGroupPageBChildren[] = {&filler_col};
constexpr Node group_page_b =
    menu::submenu(sid(String::STRING_FOR_FILTER_ROUTE), kGroupPageBChildren, menu::Layout::Horizontal);
/// A column that begins its own session from the horizontal page (`allowToBeginSessionFromHorizontalMenu`).
constexpr Node session_plain_col = menu::with_allow_session_from_horizontal(menu::int_item(
    sid(String::STRING_FOR_RECORD_TO_SAMPLE), get_filler_col, set_filler_col, deluge::ui::oled::Range{0, 50, 1}));
constexpr const Node* kSessionPageChildren[] = {&enum_col, &session_plain_col};
constexpr Node session_page =
    menu::submenu(sid(String::STRING_FOR_SAMPLE_RECORDER), kSessionPageChildren, menu::Layout::Horizontal);
constexpr const Node* kSyntheticGroupMembers[] = {&group_page_a, &group_page_empty, &group_page_b};
constexpr Node synthetic_group = menu::group(sid(String::STRING_FOR_FILTERS), kSyntheticGroupMembers);

// A single four-slot value column, for the LED "one wide item" exception.
int32_t g_wide_col = 0;
int32_t get_wide_col(const MenuContext&) {
	return g_wide_col;
}
void set_wide_col(const MenuContext&, int32_t v) {
	g_wide_col = v;
}
constexpr Node wide_col_inner =
    menu::int_item(sid(String::STRING_FOR_MASTER_LEVEL), get_wide_col, set_wide_col, deluge::ui::oled::Range{0, 50, 1});
constexpr Node wide_col = menu::nodes::with_slots(wide_col_inner, 4);
constexpr const Node* kWideChildren[] = {&wide_col};
constexpr Node wide_page = menu::submenu(sid(String::STRING_FOR_MASTER_LEVEL), kWideChildren, menu::Layout::Horizontal);

// A vertical page, for the "all six off" example.
constexpr const Node* kVerticalChildren[] = {&chain_target};
constexpr Node vertical_page = menu::submenu(sid(String::STRING_FOR_SETTINGS), kVerticalChildren);

constexpr const Node* kRoots[] = {
    &horiz_page,  &wide_page, &vertical_page, &fixed_page, &dynamic_page, &synthetic_group, &menu::nodes::filters_group,
    &session_page};

// ---------------------------------------------------------------------------------------------
// Board fixture -- copied from `menu_scene_spec.cpp`'s `Rig`.

constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kGoldUpper = 4;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;
constexpr uint8_t kShiftX = 8, kShiftY = 0;
constexpr uint8_t kSynthX = 5, kSynthY = 0;
constexpr uint8_t kKitX = 5, kKitY = 1;
constexpr uint8_t kMidiX = 5, kMidiY = 2;
constexpr uint8_t kCvX = 5, kCvY = 3;
constexpr uint8_t kCrossScreenX = 6, kCrossScreenY = 2;
constexpr uint8_t kScaleX = 6, kScaleY = 0;

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
		menu_scene().forget_rows(); // each example starts with the rows a boot has
	}

	explicit Rig(const BoardProfile& p = deluge_profile()) : profile(p), fake(deluge::ui::shell::kDelugeBoard, p) {}

	MenuContext open(const Node& root) {
		const MenuContext ctx = menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false,
		                                          false, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(kRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		return ctx;
	}
	/// @brief `open`, numbering the application's own tree (`nodes::kAllRoots`) for a real node.
	MenuContext open_real(const Node& root) {
		const MenuContext ctx = menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false,
		                                          false, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(menu::nodes::kAllRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		return ctx;
	}

	void edges(uint8_t index, int16_t count, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, index, 0, count}, at_ms);
	}
	void turn(int32_t detents, uint32_t at_ms) { edges(kSelectEncoder, static_cast<int16_t>(detents * 2), at_ms); }
	void gold(uint8_t index, int16_t delta, uint32_t at_ms) { edges(index, delta, at_ms); }
	void button(uint8_t x, uint8_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	void enter(uint32_t at_ms) {
		button(kSelectEncButtonX, kSelectEncButtonY, true, at_ms);
		button(kSelectEncButtonX, kSelectEncButtonY, false, at_ms);
	}
	/// @brief One clean press-release of instrument button (\p x, \p y), settled by a tick.
	void press(uint8_t x, uint8_t y, uint32_t at_ms) {
		button(x, y, true, at_ms);
		shell.tick(at_ms + 5);
		button(x, y, false, at_ms + 10);
	}
};

int depth_of() {
	return menu_scene().state().depth;
}

std::string last_notice() {
	deluge::notify::Notice n;
	std::string text;
	while (deluge::notify::notices().pop(n)) {
		text = n.text;
	}
	return text;
}

/// @brief Pop every queued notice, in raise order -- for the examples that pin the *kind* a raise
///        used, not only that something was raised.
std::vector<deluge::notify::Notice> raised_notices() {
	std::vector<deluge::notify::Notice> out;
	deluge::notify::Notice n;
	while (deluge::notify::notices().pop(n)) {
		out.push_back(n);
	}
	return out;
}

/// @return How many of @p raised are of kind @p kind.
size_t count_kind(const std::vector<deluge::notify::Notice>& raised, deluge::notify::Notice::Kind kind) {
	size_t n = 0;
	for (const deluge::notify::Notice& notice : raised) {
		n += notice.kind == kind ? 1 : 0;
	}
	return n;
}

/// @brief The column notification legacy's `HorizontalMenu::displayNotification` makes is one
///        `display->displayNotification(title, value)` call (`horizontal_menu.cpp:342`) -- so the
///        port raises exactly one `Notification` and no `Console` line. Returns that notice.
template <typename Self>
deluge::notify::Notice expect_one_notification(Self& self, const std::vector<deluge::notify::Notice>& raised) {
	using Kind = deluge::notify::Notice::Kind;
	expect(count_kind(raised, Kind::Notification)).to_equal(size_t{1});
	expect(count_kind(raised, Kind::Console)).to_equal(size_t{0});
	for (const deluge::notify::Notice& notice : raised) {
		if (notice.kind == Kind::Notification) {
			return notice;
		}
	}
	return deluge::notify::Notice{};
}

/// @brief Whether \p f declared \p b's LED `On` this frame -- `LedFrame` has no query accessor of
///        its own, only `entries()`, so this scans it.
bool led_on(const deluge::ui::LedFrame& f, ButtonId b) {
	const auto id = static_cast<deluge::ui::LedId>(b);
	for (const auto& [entry_id, state] : f.entries()) {
		if (entry_id == id) {
			return state.kind == deluge::ui::LedState::Kind::On;
		}
	}
	return false;
}

} // namespace

// clang-format off
describe menu_horizontal_controls("scenes::menu::MenuScene horizontal-page controls", $ {
	it("a select turn edits the focused column and leaves focus alone", _ {
		// `HorizontalMenu::selectEncoderAction` (`horizontal_menu.cpp:326`).
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		g_enum_col = 0;

		(void)raised_notices(); // drain
		r.turn(1, 10);
		r.shell.tick(20);

		expect(g_enum_col).to_equal(1);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0); // still ENUM_COL

		// ...and notifies (`:328` -> `displayNotification`, `:342`). Read before any further tick:
		// the next tick's step 2 would drain it (`shell.cpp:330`).
		const deluge::notify::Notice n = expect_one_notification(self, raised_notices());
		expect(std::string(n.notification_title())).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_MODE)));
		expect(std::string(n.notification_value()).empty()).to_be_false();
	});

	it("a fast select turn accelerates the focused column's edit, as HorizontalMenu::calcNextKnobSpeed does", _ {
		// `HorizontalMenu::selectEncoderAction` edits by `offset * calcNextKnobSpeed(offset)`
		// (`kHorizontalMenuAcceleration`), truncated to an int: four +1 turns 10 ms apart multiply by
		// 1.0, 1.5, 2.85 and 4.065, which move the value 1 + 1 + 2 + 4.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		r.press(kCvX, kCvY, 200); // focus CHAIN_COL (column 3)
		g_chain_col = 0;
		for (uint32_t t = 600; t <= 630; t += 10) {
			r.turn(1, t);
			r.shell.tick(t);
		}
		expect(g_chain_col).to_equal(8);
	});

	it("an enum column's notification names the option, as Selection::getNotificationValue does", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		g_enum_col = 0;
		(void)raised_notices();
		r.turn(1, 10);
		r.shell.tick(20);
		const deluge::notify::Notice n = expect_one_notification(self, raised_notices());
		expect(std::string(n.notification_value())).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_ENABLED)));
	});

	it("a column whose legacy item shortens draws main's short label and short option", _ {
		// `arpeggiator::NoteMode` overrides `getColumnLabel` and `getOptions(SHORT)` with the short table.
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		constexpr Node shortened = menu::with_short_options(menu::with_short_column_label(
		    menu::enumeration(sid(String::STRING_FOR_NOTE_MODE), kShortEnumNames, get_enum_col, set_enum_col)));
		constexpr Node plain =
		    menu::enumeration(sid(String::STRING_FOR_NOTE_MODE), kShortEnumNames, get_enum_col, set_enum_col);
		menu::NodeText label_scratch;
		expect(std::string(menu::column_label(shortened, ctx, label_scratch))).to_equal(std::string("NMOD"));
		expect(std::string(menu::column_label(plain, ctx, label_scratch)))
		    .to_equal(std::string(deluge::l10n::get(String::STRING_FOR_NOTE_MODE)));

		std::array<std::string_view, 2> names{};
		std::array<deluge::ui::shell::TextScratch, 2> scratch{};
		expect(menu::bindings::names(shortened, ctx, names, scratch, menu::bindings::NameForm::Short)).to_equal(size_t{2});
		expect(std::string(names[1])).to_equal(std::string("RAND"));
		expect(menu::bindings::names(plain, ctx, names, scratch, menu::bindings::NameForm::Short)).to_equal(size_t{2});
		expect(std::string(names[1])).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_RANDOM)));
	});

	it("the arp preset column draws no label: a big OFF, else the preset's icon", _ {
		// `arpeggiator::PresetMode::showColumnLabel()` is false and its `renderInHorizontalMenu` draws OFF in the
		// title face, else an icon (`preset_mode.h:81-116`).
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		menu::present_column(menu::nodes::arp_preset, ctx, static_cast<int32_t>(ArpPreset::OFF), slot, col);
		expect(col.show_label).to_equal(false);
		expect(col.render == deluge::ui::oled::art_column).to_equal(true);
		expect(std::string(slot.art.text)).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_OFF)));
		expect(slot.art.face == deluge::ui::oled::ArtColumn::Face::Title).to_equal(true);

		menu::present_column(menu::nodes::arp_preset, ctx, static_cast<int32_t>(ArpPreset::RANDOM), slot, col);
		expect(slot.art.icon == &deluge::gfx::oled::icons::dice).to_equal(true);
		menu::present_column(menu::nodes::arp_preset, ctx, static_cast<int32_t>(ArpPreset::DOWN), slot, col);
		expect(slot.art.icon == &deluge::gfx::oled::icons::arpModeUp).to_equal(true);
		expect(slot.art.reversed).to_equal(true);
	});

	it("an analog or supersaw oscillator's type column carries main's A or S badge", _ {
		// `osc::Type::renderInHorizontalMenu` (`osc/type.h:150-155`).
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		std::string badges;
		for (int32_t v = 0; v < 16; v++) {
			slot = {};
			menu::present_column(menu::nodes::osc0_type, ctx, v, slot, col);
			if (slot.art.badge != 0) {
				badges += slot.art.badge;
			}
		}
		expect(badges.find('A') != std::string::npos).to_be_true();
		expect(badges.find('S') != std::string::npos).to_be_true();
		menu::present_column(menu::nodes::osc0_type, ctx, 0, slot, col); // sine
		expect(static_cast<int>(slot.art.badge)).to_equal(0);
	});

	it("the stutter direction column draws main's song text, arrow and ping-pong dots", _ {
		// `stutter::Direction::renderInHorizontalMenu` (`stutter/direction.h:133-156`), use-song offered on a sound.
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		menu::present_column(menu::nodes::stutter_direction, ctx, 0, slot, col);
		expect(std::string(slot.art.text)).to_equal(std::string("song"));
		expect(slot.art.icon == nullptr).to_be_true();
		menu::present_column(menu::nodes::stutter_direction, ctx, 2, slot, col); // reversed
		expect(slot.art.icon == &deluge::gfx::oled::icons::direction).to_be_true();
		expect(slot.art.reversed).to_be_true();
		expect(slot.art.dots).to_be_false();
		menu::present_column(menu::nodes::stutter_direction, ctx, 3, slot, col); // forward ping-pong
		expect(slot.art.reversed).to_be_false();
		expect(slot.art.dots).to_be_true();
		menu::present_column(menu::nodes::stutter_direction, ctx, 4, slot, col); // reversed ping-pong
		expect(slot.art.reversed).to_be_true();
		expect(slot.art.dots).to_be_true();
	});

	it("the play direction column draws main's arrow, mirrored pair and obey-parent text", _ {
		// `sequence::Direction::renderInHorizontalMenu` (`sequence/direction.h:123-141`).
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		menu::present_column(menu::nodes::sequence_direction, ctx, 1, slot, col); // reverse
		expect(col.render == deluge::ui::oled::art_column).to_be_true();
		expect(slot.art.icon == &deluge::gfx::oled::icons::direction).to_be_true();
		expect(slot.art.reversed).to_be_true();
		menu::present_column(menu::nodes::sequence_direction, ctx, 2, slot, col); // ping-pong
		expect(static_cast<int>(slot.art.twin_dx)).to_equal(2);
		slot = {};
		menu::present_column(menu::nodes::sequence_direction, ctx, 3, slot, col); // obey parent: the option text
		expect(col.render == deluge::ui::oled::art_column).to_be_false();
	});

	it("the rhythm columns space main's pattern, zero as X, the MIDI/CV one a pixel left", _ {
		// `arpeggiator::Rhythm` and `midi_cv::Rhythm::renderInHorizontalMenu` (`arpeggiator/rhythm.h:42-62`,
		// `midi_cv/rhythm.h:48-66`): value 0 is the pattern's name, centred.
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		menu::present_column(menu::nodes::arp_rhythm, ctx, 5, slot, col);
		expect(col.render == deluge::ui::oled::art_column).to_be_true();
		expect(std::string(slot.art.text)).to_equal(std::string(arpRhythmPatternNames[5]));
		expect(static_cast<int>(slot.art.char_gap)).to_equal(2);
		expect(slot.art.zero_as_x).to_be_true();
		expect(static_cast<int>(slot.art.dx)).to_equal(2);
		// Neither overrides `getColumnLabel` (the MIDI/CV one's base spells the full name, `arp_integer.h:30`): the two
		// slots carry the whole name.
		const std::string rhythm = deluge::l10n::get(String::STRING_FOR_RHYTHM);
		expect(std::string(col.label)).to_equal(rhythm);
		menu::present_column(menu::nodes::arp_rhythm_midi, ctx, 5, slot, col);
		expect(static_cast<int>(slot.art.dx)).to_equal(1);
		expect(std::string(col.label)).to_equal(rhythm);
		menu::present_column(menu::nodes::arp_rhythm, ctx, 0, slot, col);
		expect(std::string(slot.art.text)).to_equal(std::string(arpRhythmPatternNames[0]));
		expect(static_cast<int>(slot.art.char_gap)).to_equal(0);
	});

	it("a quantized stutter rate's column and banner read its option label", _ {
		// `stutter::Rate::renderInHorizontalMenu` and `getNotificationValue` (`stutter/rate.cpp:53-68`): the label of
		// the option value nearest, 4ths 8ths 16ths 32nds 64ths at 0 12 25 37 50.
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		char buf[32];
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		menu::nodes::set_stutter_direction(ctx, 1); // the sound's own stutter
		menu::nodes::set_stutter_quantize(ctx, 1);
		r.shell.tick(0);
		menu::present_column(menu::nodes::stutter_rate, ctx, 26, slot, col);
		expect(std::string(slot.text.value)).to_equal(std::string("16ths"));
		// 31 is as near 25 as 37: the first option holds, as `getClosestQuantizedOptionIndex`'s strict `<` does.
		expect(std::string(menu::column_notification(menu::nodes::stutter_rate, ctx, 31, std::span<char>{buf, sizeof(buf)})))
		    .to_equal(std::string("16ths"));
		expect(std::string(menu::column_notification(menu::nodes::stutter_rate, ctx, 34, std::span<char>{buf, sizeof(buf)})))
		    .to_equal(std::string("32nds"));
		menu::nodes::set_stutter_quantize(ctx, 0);
		r.shell.tick(10);
		expect(std::string(menu::column_notification(menu::nodes::stutter_rate, ctx, 31, std::span<char>{buf, sizeof(buf)})))
		    .to_equal(std::string("31"));
	});

	it("a quantized stutter rate turns along its five option values, on its page and full-screen", _ {
		// `stutter::Rate::selectEncoderAction` (`stutter/rate.cpp:28-41`): the nearest option's index moves by the offset,
		// clamped, and the value becomes that option's 0 12 25 37 50.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		MenuContext ctx = r.open_real(menu::nodes::stutter);
		menu::nodes::set_stutter_direction(ctx, 1); // the sound's own stutter
		menu::nodes::set_stutter_quantize(ctx, 1);
		menu::bindings::write(menu::nodes::stutter_rate, ctx, 25);
		menu_scene().open(menu::nodes::stutter, ctx);
		r.shell.tick(0);
		expect(menu_scene().focused_value() == &menu::nodes::stutter_rate).to_be_true();
		r.turn(1, 300);
		r.shell.tick(310);
		r.shell.tick(400);
		expect(menu::bindings::read(menu::nodes::stutter_rate, ctx)).to_equal(37);
		r.turn(5, 1000);
		r.shell.tick(1010);
		r.shell.tick(1100);
		expect(menu::bindings::read(menu::nodes::stutter_rate, ctx)).to_equal(50);

		ctx.horizontal_menus = false;
		menu_scene().open(menu::nodes::stutter, ctx);
		r.shell.tick(2000);
		r.enter(2300);
		r.shell.tick(2310);
		expect(menu_scene().focused() == &menu::nodes::stutter_rate).to_be_true();
		r.turn(-1, 2600);
		r.shell.tick(2610);
		r.shell.tick(2700);
		expect(menu::bindings::read(menu::nodes::stutter_rate, ctx)).to_equal(37);
	});

	it("the stutter direction banner is main's short option", _ {
		// `stutter::Direction::getNotificationValue` (`stutter/direction.h:129-132`).
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		char buf[32];
		expect(std::string(menu::column_notification(menu::nodes::stutter_direction, ctx, 0, std::span<char>{buf, sizeof(buf)})))
		    .to_equal(std::string(deluge::l10n::get(String::STRING_FOR_USE_SONG_SHORT)));
		expect(std::string(menu::column_notification(menu::nodes::stutter_direction, ctx, 3, std::span<char>{buf, sizeof(buf)})))
		    .to_equal(std::string(deluge::l10n::get(String::STRING_FOR_FORWARD_PING_PONG)));
	});

	it("portamento, detune, pulse width and delay amount columns draw main's pictures", _ {
		// `voice::Portamento`, `unison::Detune`, `osc::PulseWidth` and `delay::Amount`/`Amount_Unpatched`'s
		// `renderInHorizontalMenu` (`voice/portamento.h:34-66`, `unison/detune.h:57-98`, `osc/pulse_width.h:54-77`,
		// `delay/amount.h:35-58`, `delay/amount_unpatched.h:31-54`), each over its 0..50 value.
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		using Shape = deluge::ui::oled::DrawColumn::Shape;
		const auto drawn = [&](const Node& node, int32_t value) {
			slot = {};
			col = {};
			menu::present_column(node, ctx, value, slot, col);
			return col.render == deluge::ui::oled::draw_column;
		};
		expect(drawn(menu::nodes::portamento, 20)).to_be_true();
		expect(slot.draw.shape == Shape::Glide).to_be_true();
		expect(slot.draw.norm).to_equal(0.4f);
		expect(drawn(menu::nodes::unison_detune, 10)).to_be_true();
		expect(slot.draw.shape == Shape::Detune).to_be_true();
		expect(slot.draw.norm).to_equal(0.2f);
		for (const Node* pw : {&menu::nodes::osc0_pulse_width, &menu::nodes::osc1_pulse_width}) {
			expect(drawn(*pw, 31)).to_be_true();
			expect(slot.draw.shape == Shape::PulseWidth).to_be_true();
			expect(static_cast<int>(slot.draw.raw)).to_equal(31);
		}
		for (const Node* amount : {&menu::nodes::delay_amount, &menu::nodes::global_delay_amount}) {
			expect(drawn(*amount, 30)).to_be_true();
			expect(slot.draw.shape == Shape::DelayAmount).to_be_true();
			expect(static_cast<int>(slot.draw.raw)).to_equal(30);
		}
	});

	it("master's transpose column reads TRANSPOSE and a whole number below -9.99", _ {
		// `MasterTranspose::getColumnLabel` (`master_transpose.h:74`); `Decimal::renderInHorizontalMenu` keeps three
		// characters of a value at or below -10.00 (`decimal.cpp:181-186`).
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		menu::present_column(menu::nodes::master_transpose, ctx, -1200, slot, col);
		expect(std::string(col.label)).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_TRANSPOSE)));
		expect(std::string(slot.text.value)).to_equal(std::string("-12"));
		menu::present_column(menu::nodes::master_transpose, ctx, -950, slot, col);
		expect(std::string(slot.text.value)).to_equal(std::string("-9.50"));
	});

	it("a sync column is labelled with its note length and draws the sync type's icon", _ {
		// `SyncLevel::getColumnLabel` / `renderInHorizontalMenu` (`sync_level.cpp:40-77`).
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		menu::present_column(menu::nodes::arp_sync, ctx, 0, slot, col);
		expect(std::string(col.label)).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_SYNC)));
		expect(std::string(slot.art.text)).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_OFF)));
		expect(slot.art.face == deluge::ui::oled::ArtColumn::Face::Row).to_equal(true);

		const int32_t sixteenth = static_cast<int32_t>(SYNC_LEVEL_16TH); // an even sync's option is its level
		menu::present_column(menu::nodes::arp_sync, ctx, sixteenth, slot, col);
		etl::string<30> expected;
		syncValueToStringForHorzMenuLabel(SYNC_TYPE_EVEN, SYNC_LEVEL_16TH, expected, menu::input_tick_magnitude(ctx));
		expect(std::string(col.label)).to_equal(std::string(expected.c_str()));
		expect(slot.art.icon == &deluge::gfx::oled::icons::syncTypeEven).to_equal(true);
	});

	it("the voice count column draws infinity at zero", _ {
		// `voice::VoiceCount::renderInHorizontalMenu` (`polyphony.h:98-104`).
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		menu::present_column(menu::nodes::voice_count, ctx, 0, slot, col);
		expect(col.render == deluge::ui::oled::art_column).to_equal(true);
		expect(slot.art.icon == &deluge::gfx::oled::icons::infinity).to_equal(true);
		menu::present_column(menu::nodes::voice_count, ctx, 8, slot, col);
		expect(col.render == deluge::ui::oled::meter_column).to_equal(true);
	});

	it("a column whose legacy item names its own label draws main's short name", _ {
		// `VoiceCount`, `delay::Amount`, `RetriggerPhase` and the compressor's `Attack` override `getColumnLabel`
		// (`voice/polyphony.h:85-87`, `delay/amount.h:60-62`, `osc/retrigger_phase.h:125-127`,
		// `audio_compressor/compressor_values.h:76-78`).
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		const auto label_of = [&](const Node& node, int32_t value) {
			menu::present_column(node, ctx, value, slot, col);
			return std::string(col.label);
		};
		expect(label_of(nodes::voice_count, 8)).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_MAX_VOICES_SHORT)));
		expect(label_of(nodes::delay_amount, 20)).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_AMOUNT_SHORT)));
		expect(label_of(nodes::osc0_phase, 0)).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_RETRIGGER_PHASE_SHORT)));
		expect(label_of(nodes::comp_attack, 0)).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_ATTACK_SHORT)));
	});

	it("the columns main draws without a label draw none", _ {
		// `showColumnLabel()` is false on `osc::Type`, `lfo::Type`, `mod_fx::Type`, `FilterRouting`, `unison::Count`
		// and `osc::AudioRecorder` (`osc/type.h:110`, `lfo/type.h:75`, `mod_fx/type.h:71`, `filter_route.h:72`,
		// `unison/count.h:60`, `osc/audio_recorder.h:76`).
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		for (const Node* node : {&nodes::osc0_type, &nodes::lfo1_type, &nodes::mod_fx_type, &nodes::filter_routing,
		                         &nodes::unison_count, &nodes::sample0_recorder}) {
			col = deluge::ui::oled::Column{};
			menu::present_column(*node, ctx, 0, slot, col);
			expect(col.show_label).to_equal(false);
		}
	});

	it("an LFO's shape column draws the shape's icon, and a sample's reverse column the direction arrow", _ {
		// `lfo::Type::renderInHorizontalMenu` (`lfo/type.h:77-101`); `sample::Reverse::renderInHorizontalMenu` and
		// `getColumnLabel` (`sample/reverse.h:68-75`).
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		menu::present_column(nodes::lfo1_type, ctx, static_cast<int32_t>(deluge::instrument::LFOType::SAW), slot, col);
		expect(col.render == deluge::ui::oled::art_column).to_equal(true);
		expect(slot.art.icon == &deluge::gfx::oled::icons::saw).to_equal(true);
		expect(static_cast<int>(slot.art.dy)).to_equal(2);

		menu::present_column(nodes::sample0_reverse, ctx, 1, slot, col);
		expect(std::string(col.label)).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_PLAY)));
		expect(col.render == deluge::ui::oled::art_column).to_equal(true);
		expect(slot.art.icon == &deluge::gfx::oled::icons::direction).to_equal(true);
		expect(slot.art.reversed).to_equal(true);
	});

	it("a retrigger-phase column draws a slider, and OFF while free-running", _ {
		// `RetriggerPhase::getRenderingStyle()` is SLIDER, which `Decimal::renderInHorizontalMenu` hands to
		// `Number::renderInHorizontalMenu` (`decimal.cpp:176-179`); below zero it draws OFF (`retrigger_phase.h:106-115`).
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		menu::present_column(nodes::osc0_phase, ctx, 90, slot, col);
		expect(col.render == deluge::ui::oled::meter_column).to_equal(true);
		expect(slot.meter.hint == deluge::ui::oled::Hint::Slider).to_equal(true);
		menu::present_column(nodes::osc0_phase, ctx, -1, slot, col);
		expect(col.render == deluge::ui::oled::text_column).to_equal(true);
		expect(std::string(slot.text.value)).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_OFF)));
	});

	it("a column's notification spells main's getNotificationValue", _ {
		// `SyncLevel::getShortOption` (`sync_level.cpp:83-91`), `Toggle` (none), `Number`'s PERCENT doubling
		// (`number.cpp:429-431`), `VoiceCount`'s OFF (`voice/polyphony.h:89-96`), `RetriggerPhase`'s OFF
		// (`retrigger_phase.h:117-123`).
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		char buf[32];
		const auto banner = [&](const Node& node, int32_t value) {
			return std::string(menu::column_notification(node, ctx, value, std::span<char>{buf, sizeof(buf)}));
		};
		const std::string off = deluge::l10n::get(String::STRING_FOR_OFF);
		expect(banner(nodes::arp_sync, 0)).to_equal(off);
		const int32_t sixteenth = static_cast<int32_t>(SYNC_LEVEL_16TH);
		etl::string<30> length;
		syncValueToString(static_cast<uint32_t>(sixteenth), length, menu::input_tick_magnitude(ctx));
		expect(banner(nodes::arp_sync, sixteenth)).to_equal(std::string(length.c_str()));
		expect(banner(nodes::osc_sync, 1)).to_equal(std::string());
		expect(banner(nodes::note_probability, 10)).to_equal(std::string("20"));
		expect(banner(nodes::voice_count, 0)).to_equal(off);
		expect(banner(nodes::voice_count, 5)).to_equal(std::string("5"));
		expect(banner(nodes::osc0_phase, -1)).to_equal(off);
	});

	it("a setting's column reads main's own short option", _ {
		// `FilterModeSelection::getOptions(SHORT)` (`filter/mode.h:62-80`) and the other `getOptions(SHORT)` overrides:
		// the setting's `short_choices`.
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		std::array<std::string_view, 8> names{};
		std::array<deluge::ui::shell::TextScratch, 8> scratch{};
		const auto short_name = [&](const Node& node, size_t index) {
			const size_t n = menu::bindings::names(node, ctx, names, scratch, menu::bindings::NameForm::Short);
			return index < n ? std::string(names[index]) : std::string("<none>");
		};
		expect(short_name(menu::nodes::hpf_mode, 2)).to_equal(std::string("12dB"));
		expect(short_name(menu::nodes::lpf_mode, 1)).to_equal(std::string("24dB"));
		expect(short_name(menu::nodes::delay_type, 2))
		    .to_equal(std::string(deluge::l10n::get(String::STRING_FOR_ANALOG_SHORT)));
		expect(short_name(menu::nodes::voice_priority, 1))
		    .to_equal(std::string(deluge::l10n::get(String::STRING_FOR_MEDIUM_SHORT)));
	});

	it("the filters group pages LPF, HPF and FILTER ROUTE as 1/3, 2/3, 3/3", _ {
		// `filtersMenuGroup` (`menus.cpp:631`); `HorizontalMenuGroup::getTitle` is the current member's
		// (`horizontal_menu_group.cpp:77-79`), and CROSS_SCREEN/SCALE page across members, wrapping.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(menu::nodes::filters_group);
		r.shell.tick(0);
		menu::NodeText scratch;
		const auto title = [&] { return std::string(menu_scene().page_title(scratch)); };
		expect(title()).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_LPF)));
		expect(static_cast<int>(menu_scene().horizontal_paging().total_pages)).to_equal(3);
		expect(static_cast<int>(menu_scene().horizontal_paging().page)).to_equal(0);
		r.press(kCrossScreenX, kCrossScreenY, 300);
		r.shell.tick(400);
		expect(title()).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_HPF)));
		expect(static_cast<int>(menu_scene().horizontal_paging().page)).to_equal(1);
		r.press(kCrossScreenX, kCrossScreenY, 600);
		r.shell.tick(700);
		expect(title()).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_FILTER_ROUTE)));
		r.press(kCrossScreenX, kCrossScreenY, 900);
		r.shell.tick(1000);
		expect(title()).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_LPF)));
		r.press(kScaleX, kScaleY, 1200);
		r.shell.tick(1300);
		expect(title()).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_FILTER_ROUTE)));
	});

	it("SCALE and CROSS_SCREEN light on a one-page member of a group", _ {
		// `horizontal_menu.cpp:117-119`: lit iff `totalPages > 1`, the group's total on a group.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(menu::nodes::filters_group);
		r.shell.tick(0);
		deluge::ui::LedFrame f;
		menu_scene().leds(f);
		expect(led_on(f, ButtonId::Scale)).to_be_true();
		expect(led_on(f, ButtonId::CrossScreen)).to_be_true();
	});

	it("a group member with no relevant columns contributes no page", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(synthetic_group);
		r.shell.tick(0);
		expect(static_cast<int>(menu_scene().horizontal_paging().total_pages)).to_equal(2);
		r.press(kCrossScreenX, kCrossScreenY, 300);
		r.shell.tick(400);
		menu::NodeText scratch;
		expect(std::string(menu_scene().page_title(scratch)))
		    .to_equal(std::string(deluge::l10n::get(String::STRING_FOR_FILTER_ROUTE)));
	});

	it("reopening a group lands on the member and column last focused", _ {
		select_language();
		deluge::notify::dirty().drain();
		menu_scene().forget_rows();
		Rig r;
		const MenuContext ctx = r.open(menu::nodes::filters_group);
		r.shell.tick(0);
		r.press(kCrossScreenX, kCrossScreenY, 300);
		r.shell.tick(400);
		const int8_t focused = menu_scene().state().columns[0].focused;
		r.button(kBackX, kBackY, true, 500);
		r.shell.tick(510);
		(void)r.open(menu::nodes::filters_group);
		r.shell.tick(600);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(static_cast<int>(focused));
		menu::NodeText scratch;
		expect(std::string(menu_scene().page_title(scratch))).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_HPF)));
		(void)ctx;
	});

	it("a select on a column that begins its own session enters it", _ {
		// `Submenu::selectButtonPress` returns the focused item when it `allowToBeginSessionFromHorizontalMenu()`
		// (`submenu.cpp:187-194`), and the select arm enters it (`sound_editor.cpp:613-637`): the recorder, the file
		// selectors, the cable strengths.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(session_page);
		r.shell.tick(0);
		r.press(kKitX, kKitY, 300);
		r.shell.tick(400);
		r.enter(500);
		r.shell.tick(600);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);
		expect(static_cast<int>(menu_scene().state().depth)).to_equal(1);
		expect(r.shell.stack().depth()).to_equal(size_t(1));
		expect(menu_scene().focused() == &session_plain_col).to_be_true();
	});

	it("a select detent on a horizontal Decimal column moves 1.00, and 0.01 with Shift held", _ {
		// `Decimal::getNumberEditSize` on a horizontal page (`decimal.cpp:49-54`): MASTER's transpose, a semitone a
		// detent, a cent with SELECT or SHIFT held.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open_real(menu::nodes::sound_master);
		r.shell.tick(0);
		r.press(kCvX, kCvY, 300); // TRAN, column 3
		r.shell.tick(400);
		const int32_t before = menu::bindings::read(menu::nodes::master_transpose, ctx);
		r.turn(1, 1000);
		r.shell.tick(1010);
		r.shell.tick(1020);
		expect(menu::bindings::read(menu::nodes::master_transpose, ctx)).to_equal(before + 100);
		r.button(kShiftX, kShiftY, true, 3000);
		r.turn(1, 3100);
		r.shell.tick(3110);
		r.shell.tick(3120);
		r.button(kShiftX, kShiftY, false, 3200);
		expect(menu::bindings::read(menu::nodes::master_transpose, ctx)).to_equal(before + 101);
	});

	it("retrigger phase steps 0.10 and 0.01 and restarts from OFF; the compressor steps 1", _ {
		// `osc::RetriggerPhase` (`osc/retrigger_phase.h:91-104`); `DecimalWithoutScrolling::selectEncoderAction`
		// (`decimal.cpp:193`) has no edit size.
		expect(static_cast<int>(menu::nodes::osc0_phase.bind.horizontal_step.coarse)).to_equal(10);
		expect(static_cast<int>(menu::nodes::osc0_phase.bind.horizontal_step.fine)).to_equal(1);
		expect(menu::nodes::osc0_phase.bind.horizontal_step.restart_from_negative).to_be_true();
		// The FM modulators' phases are the same class (`g_menus.inc:126,135`).
		for (const Node* phase : {&menu::nodes::modulator0_phase_node, &menu::nodes::modulator1_phase_node}) {
			expect(static_cast<int>(phase->bind.horizontal_step.coarse)).to_equal(10);
			expect(static_cast<int>(phase->bind.horizontal_step.fine)).to_equal(1);
			expect(phase->bind.horizontal_step.restart_from_negative).to_be_true();
		}
		expect(static_cast<int>(menu::nodes::comp_ratio.bind.horizontal_step.coarse)).to_equal(1);
	});

	it("the sample and audio-clip reverse turn the other way on a page; the oscillator type wraps", _ {
		// `sample::Reverse`/`audio_clip::Reverse::selectEncoderAction` (`sample/reverse.h:81-86`,
		// `audio_clip/reverse.h:84`); `osc::Type::wrapAround` (`osc/type.h:158-160`).
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		expect(menu::nodes::sample0_reverse.bind.reverse_in_horizontal != nullptr
		       && menu::nodes::sample0_reverse.bind.reverse_in_horizontal(ctx))
		    .to_be_true();
		expect(menu::nodes::audio_clip_reverse.bind.reverse_in_horizontal != nullptr
		       && menu::nodes::audio_clip_reverse.bind.reverse_in_horizontal(ctx))
		    .to_be_true();
		expect(menu::nodes::osc0_type.bind.wrap_in_horizontal).to_be_true();
	});

	it("with HorizontalMenus off a horizontal page is a vertical list", _ {
		// `HorizontalMenu::renderingStyle()` is VERTICAL with the feature off (`horizontal_menu.cpp:592-597`): the page
		// renders and edits as a `Submenu`, the select knob moving the row cursor.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		MenuContext ctx = r.open(horiz_page);
		ctx.horizontal_menus = false;
		menu_scene().open(horiz_page, ctx);
		r.shell.tick(0);
		r.turn(1, 100);
		r.shell.tick(150);
		expect(static_cast<int>(menu_scene().state().list[0].cursor)).to_equal(1);
		expect(static_cast<int>(menu_scene().horizontal_paging().total_pages)).to_equal(0);
	});

	it("with HorizontalMenus off a param row opens main's continuous number screen", _ {
		// `IntegerContinuous::drawPixelsForOled` (`integer.cpp:63-72`): the Big-face digits over the value bar at canvas
		// rows 35..43, columns 12..114.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		MenuContext ctx = r.open_real(menu::nodes::lpf);
		ctx.horizontal_menus = false;
		menu_scene().open(menu::nodes::lpf, ctx);
		r.shell.tick(0);
		r.turn(1, 300);
		r.shell.tick(310);
		r.enter(400);
		r.shell.tick(410);
		expect(menu_scene().focused() == &menu::nodes::lpf_freq).to_be_true();

		deluge::ui::test::Rig48 rig48;
		rig48.begin_tick(500);
		menu_scene().render_oled(uitest::SurfaceRole::Main, rig48.frame);
		expect(rig48.lit(12, 38)).to_be_true();
		expect(rig48.lit(114, 38)).to_be_true();
		expect(rig48.lit(64, 35)).to_be_true();
		expect(rig48.lit(64, 43)).to_be_true();
		// The huge face's band (rows 23..42) would ink below the Big face's 20..32 between the bar's ends.
		bool any_between = false;
		for (int32_t x = 0; x < 128; x++) {
			any_between = any_between || rig48.lit(x, 33) || rig48.lit(x, 34);
		}
		expect(any_between).to_be_false();
	});

	it("select on a patched param column opens its sources where the param row does", _ {
		// `PatchedParam::selectButtonPress` (`patched_param.cpp:32-40`) records the param before `SourceSelection::
		// beginSession` lands on the first source patched to it -- from a column as from the item alone.
		select_language();
		deluge::notify::dirty().drain();
		int16_t vertical_cursor = -1;
		{
			Rig r;
			MenuContext ctx = r.open_real(menu::nodes::lpf);
			ctx.horizontal_menus = false;
			menu_scene().open(menu::nodes::lpf, ctx);
			r.shell.tick(0);
			r.turn(1, 300);
			r.shell.tick(310);
			r.enter(400);
			r.shell.tick(410);
			expect(static_cast<int>(menu::node_id(*menu_scene().focused()))).to_equal(static_cast<int>(menu::node_id(menu::nodes::lpf_freq)));
			r.enter(700);
			r.shell.tick(710);
			const uint8_t d = menu_scene().state().depth;
			vertical_cursor = menu_scene().state().list[d].cursor;
		}
		expect(vertical_cursor > 0).to_be_true(); // the fixture's synth patches a source to the LPF frequency

		Rig r;
		(void)r.open_real(menu::nodes::lpf);
		r.shell.tick(0);
		r.press(kKitX, kKitY, 200); // focus column 1, the frequency
		const Node* fv = menu_scene().focused_value();
		expect(fv == nullptr ? -1 : static_cast<int>(menu::node_id(*fv))).to_equal(static_cast<int>(menu::node_id(menu::nodes::lpf_freq)));
		r.enter(400);
		r.shell.tick(410);
		expect(static_cast<int>(menu_scene().state().patching_param))
		    .to_equal(static_cast<int>(deluge::modulation::params::LOCAL_LPF_FREQ));
		const uint8_t d = menu_scene().state().depth;
		expect(static_cast<int>(menu_scene().state().list[d].cursor)).to_equal(static_cast<int>(vertical_cursor));

		// `SourceSelection::drawPixelsForOled` (`source_selection.cpp:59-108`) draws the names alone: the patched
		// source's dot is the 7-segment display's (`shouldDrawDotOnValue`), so the highlighted row has no mark.
		deluge::ui::test::Rig48 rig48;
		rig48.begin_tick(800);
		menu_scene().render_oled(uitest::SurfaceRole::Main, rig48.frame);
		bool marked = false;
		for (int32_t y = 29; y <= 35; y++) {
			for (int32_t x = 110; x <= 124; x++) {
				marked = marked || !rig48.lit(x, y);
			}
		}
		expect(marked).to_be_false();
	});

	it("a quantized stutter rate's screen has no value bar", _ {
		// `stutter::Rate::drawPixelsForOled` (`stutter/rate.cpp:43-50`): quantized, the option label alone; otherwise
		// `UnpatchedParam`'s continuous screen with its bar.
		select_language();
		deluge::notify::dirty().drain();
		for (const bool quantized : {false, true}) {
			Rig r;
			MenuContext ctx = r.open_real(menu::nodes::stutter);
			ctx.horizontal_menus = false;
			// The sound's own stutter, not the song's (`stutter::Rate::isStutterQuantized`, `stutter/rate.cpp:70-76`).
			menu::nodes::set_stutter_direction(ctx, 1);
			menu::nodes::set_stutter_quantize(ctx, quantized ? 1 : 0);
			menu_scene().open(menu::nodes::stutter, ctx);
			r.shell.tick(0);
			r.enter(300);
			r.shell.tick(310);
			expect(menu_scene().focused() == &menu::nodes::stutter_rate).to_be_true();
			deluge::ui::test::Rig48 rig48;
			rig48.begin_tick(400);
			menu_scene().render_oled(uitest::SurfaceRole::Main, rig48.frame);
			expect(rig48.lit(12, 38)).to_equal(!quantized);
			if (quantized) {
				// `drawStringCentred(label, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX, kTextHugeSizeY)`, the label of
				// the nearest of 0 12 25 37 50.
				const int32_t v = menu::bindings::read(menu::nodes::stutter_rate, ctx);
				constexpr std::array<int32_t, 5> kValues{0, 12, 25, 37, 50};
				constexpr std::array<const char*, 5> kLabels{"4ths", "8ths", "16ths", "32nds", "64ths"};
				size_t nearest = 0;
				for (size_t i = 1; i < kValues.size(); i++) {
					if (std::abs(v - kValues[i]) < std::abs(v - kValues[nearest])) {
						nearest = i;
					}
				}
				deluge::ui::test::Rig48 label;
				label.reference.draw_string_centred(kLabels[nearest], 18 + 5, 18, 20);
				std::string got;
				std::string want;
				for (int32_t y = 23; y <= 42; y++) {
					for (int32_t x = 0; x < 128; x++) {
						got += rig48.lit(x, y) ? '#' : '.';
						want += label.reference_lit(x, y) ? '#' : '.';
					}
					got += '\n';
					want += '\n';
				}
				expect(got).to_equal(want);
			}
		}
	});

	it("with HorizontalMenus off a shortcut opens the item alone", _ {
		// `getCurrentHorizontalMenusChain` answers nothing with the feature off (`sound_editor.cpp:2328-2330`).
		select_language();
		Rig r;
		MenuContext ctx = r.open(horiz_page);
		ctx.horizontal_menus = false;
		expect(menu::shortcut_landing_page(ctx, menu::nodes::lpf_freq) == nullptr).to_be_true();
		ctx.horizontal_menus = true;
		expect(menu::shortcut_landing_page(ctx, menu::nodes::lpf_freq) == &menu::nodes::filters_group).to_be_true();
	});

	it("reads HorizontalMenus at each open, not once", _ {
		select_language();
		Rig r;
		const auto open_ctx = [&] {
			return menu::context_for(*r.song.song, deluge::gui::menuViewOf(r.song.clip), 0, false, false, false, false);
		};
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::Off);
		expect(open_ctx().horizontal_menus).to_be_false();
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
		expect(open_ctx().horizontal_menus).to_be_true();
	});

	it("the EQ page orders its columns as main's EqMenu does", _ {
		// `EqMenu::ensureCorrectItemsOrderAndGetValues` (`eq/eq_menu.h:120-160`): bass, bass frequency, treble frequency,
		// treble -- the order of the graph's four handles left to right.
		expect(menu::nodes::eq.children.size()).to_equal(size_t{4});
		expect(menu::nodes::eq.children[0] == &menu::nodes::eq_bass).to_be_true();
		expect(menu::nodes::eq.children[1] == &menu::nodes::eq_bass_freq).to_be_true();
		expect(menu::nodes::eq.children[2] == &menu::nodes::eq_treble_freq).to_be_true();
		expect(menu::nodes::eq.children[3] == &menu::nodes::eq_treble).to_be_true();
		expect(menu::nodes::eq.page_draw != nullptr).to_be_true();
	});

	it("the envelope group pages ENV1..ENV4 as 1/4..4/4, each drawn as main's envelope graph", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open_real(menu::nodes::env_group);
		r.shell.tick(0);
		expect(static_cast<int>(menu_scene().horizontal_paging().total_pages)).to_equal(4);
		expect(menu::nodes::env1.page_draw != nullptr).to_be_true();
		r.press(kCrossScreenX, kCrossScreenY, 300);
		r.shell.tick(400);
		expect(static_cast<int>(menu_scene().horizontal_paging().page)).to_equal(1);
	});

	it("the oscillator group reaches its last column and counts every member's pages on an FM synth", _ {
		// `HorizontalMenuGroup::preparePaging` (`horizontal_menu_group.cpp:167-205`) adds up each member's pages, however
		// many columns the members list between them; an FM synth's lists seventeen.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open_real(menu::nodes::source_group);
		spec::menu_engine(ctx).sound->setSynthMode(SynthMode::FM, r.song.song);
		const Node* listed[menu::kMaxDynamicChildren];
		const size_t columns = menu::bindings::listed_children(menu::nodes::source_group, menu_scene().menu_context(),
		                                                       listed, menu::bindings::ListFor::Columns);
		expect(columns > 16).to_be_true();

		// A shortcut to the last column lands on it (`focusChild` over every member, `horizontal_menu_group.cpp:209-221`).
		const Node* last = listed[columns - 1];
		const uint16_t arg = menu_scene().arm_shortcut_landing(*last);
		r.shell.init(SceneId{SceneKind::FirstApp, arg});
		r.shell.tick(0);
		expect(menu_scene().focused_value() == last).to_be_true();

		const auto pages_of = [&r](const Node& page) {
			menu_scene().open(page, menu_scene().menu_context());
			r.shell.init(SceneId{SceneKind::FirstApp, menu::node_id(page)});
			r.shell.tick(0);
			return static_cast<int>(menu_scene().horizontal_paging().total_pages);
		};
		int members = 0;
		for (const Node* member : menu::nodes::source_group.children) {
			members += pages_of(*member);
		}
		expect(pages_of(menu::nodes::source_group)).to_equal(members);
	});

	it("a mod-FX knob's name and title follow the effect, its column label the short form", _ {
		// `mod_fx::Depth_Patched::getName`/`getTitle` (`mod_fx/depth_patched.h:34-37`): `modfx::getParamName`.
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::bindings::write(menu::nodes::mod_fx_type, ctx, 1); // flanger
		menu::NodeText scratch;
		expect(std::string(menu::display_text(menu::nodes::mod_fx_depth, menu::ValueTextUse::Name, ctx, scratch)))
		    .to_equal(std::string(modfx::getParamName(deluge::effects::ModFXType::FLANGER, ModFXParam::DEPTH, false)));
		expect(std::string(menu::display_text(menu::nodes::mod_fx_depth, menu::ValueTextUse::Title, ctx, scratch)))
		    .to_equal(std::string(modfx::getParamName(deluge::effects::ModFXType::FLANGER, ModFXParam::DEPTH, false)));
	});

	it("the filter morph's name and title are the long morph name for its family", _ {
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::NodeText scratch;
		menu::bindings::write(menu::nodes::lpf_mode, ctx, 3); // SVF band
		expect(std::string(menu::display_text(menu::nodes::lpf_morph, menu::ValueTextUse::Title, ctx, scratch)))
		    .to_equal(std::string(deluge::l10n::get(String::STRING_FOR_MORPH)));
		menu::bindings::write(menu::nodes::lpf_mode, ctx, 1); // 24 dB ladder
		expect(std::string(menu::display_text(menu::nodes::lpf_morph, menu::ValueTextUse::Name, ctx, scratch)))
		    .to_equal(std::string(deluge::l10n::get(String::STRING_FOR_DRIVE)));
	});

	it("the mod FX page is titled with the effect's name past its first page", _ {
		// `submenu::ModFxHorizontalMenu::getTitle` (`submenu/mod_fx.h:29-43`).
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open_real(menu::nodes::mod_fx);
		menu::bindings::write(menu::nodes::mod_fx_type, ctx, 1); // flanger
		r.shell.tick(0);
		menu::NodeText scratch;
		expect(std::string(menu_scene().page_title(scratch))).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_MOD_FX)));
		r.press(kCrossScreenX, kCrossScreenY, 300);
		r.shell.tick(400);
		expect(std::string(menu_scene().page_title(scratch))).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_FLANGER)));
	});

	it("a group lists its members' columns, not the member pages themselves", _ {
		// `HorizontalMenuGroup::hasItem` asks each member menu's items (`horizontal_menu_group.cpp:209-211`): entering
		// ARPEGGIATOR > BASIC from the vertical list opens the BASIC page alone, not the arpeggiator group.
		expect(menu::page_lists(menu::nodes::arp_group, menu::nodes::arp_gate)).to_be_true();
		expect(menu::page_lists(menu::nodes::arp_group, menu::nodes::arp_basic)).to_be_false();
		expect(menu::page_lists(menu::nodes::arp_basic, menu::nodes::arp_gate)).to_be_true();
	});

	it("the LPF frequency column draws main's filter graph across FREQ and RESO", _ {
		// `filter::FilterContainer` (`filter/filter_container.h:28-146`), registered over {freq, res}.
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		menu::present_column(menu::nodes::lpf_freq, ctx, 25, slot, col);
		expect(col.render == deluge::ui::oled::draw_column).to_be_true();
		expect(slot.draw.shape == deluge::ui::oled::DrawColumn::Shape::FilterGraph).to_be_true();
		expect(static_cast<int>(col.span_next)).to_equal(1);
		expect(slot.draw.filter.freq_raw).to_equal(25);
		expect(slot.draw.filter.hpf).to_be_false();
	});

	it("the LPF morph column reads DRIVE on a ladder, MORPH and a slider on an SVF", _ {
		// `filter::FilterParam` (`filter/param.h:40-63`) through `SpecificFilter::getMorphName`.
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		menu::bindings::write(menu::nodes::lpf_mode, ctx, 1); // 24 dB ladder
		menu::present_column(menu::nodes::lpf_morph, ctx, 20, slot, col);
		expect(std::string(col.label)).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_DRIVE)));
		expect(col.render == deluge::ui::oled::meter_column).to_be_true();
		menu::bindings::write(menu::nodes::lpf_mode, ctx, 3); // SVF band
		menu::present_column(menu::nodes::lpf_morph, ctx, 20, slot, col);
		expect(std::string(col.label)).to_equal(std::string(deluge::l10n::get(String::STRING_FOR_MORPH_SHORT)));
		expect(col.render == deluge::ui::oled::draw_column).to_be_true();
		expect(slot.draw.shape == deluge::ui::oled::DrawColumn::Shape::Slider).to_be_true();
		expect(slot.draw.norm).to_equal(20.f / 50.f);
	});

	it("HPF SVF morph draws 50 - v and turns the other way on its page", _ {
		select_language();
		Rig r;
		const MenuContext ctx = r.open(horiz_page);
		menu::ColumnSlot slot;
		deluge::ui::oled::Column col;
		menu::bindings::write(menu::nodes::hpf_mode, ctx, 0); // SVF band
		menu::present_column(menu::nodes::hpf_morph, ctx, 10, slot, col);
		expect(slot.draw.norm).to_equal(40.f / 50.f);
		expect(menu::nodes::hpf_morph.bind.reverse_in_horizontal(ctx)).to_be_true();
		menu::bindings::write(menu::nodes::hpf_mode, ctx, 2); // HP ladder
		expect(menu::nodes::hpf_morph.bind.reverse_in_horizontal(ctx)).to_be_false();
	});

	it("a FIXED page keeps an irrelevant column in place, drawn as a dash", _ {
		// `HorizontalMenu::Layout::FIXED` keeps irrelevant items (`horizontal_menu.cpp:494`, `:253-257`); DYNAMIC drops them.
		select_language();
		Rig r;
		const MenuContext ctx = r.open(fixed_page);
		std::array<const Node*, 8> kids{};
		expect(menu::bindings::listed_children(fixed_page, ctx, kids, menu::bindings::ListFor::Columns)).to_equal(size_t{6});
		expect(menu::bindings::listed_children(fixed_page, ctx, kids, menu::bindings::ListFor::Rows)).to_equal(size_t{4});
		expect(menu::bindings::listed_children(dynamic_page, ctx, kids, menu::bindings::ListFor::Columns))
		    .to_equal(size_t{4});
	});

	it("an instrument button on a FIXED page's irrelevant column does nothing", _ {
		// `horizontal_menu.cpp:407-410`.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(fixed_page);
		r.shell.tick(0);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0);
		r.press(kKitX, kKitY, 100);
		r.shell.tick(200);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0);
		r.press(kMidiX, kMidiY, 300);
		r.shell.tick(400);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(2);
	});

	it("the page switch on a FIXED page walks the same columns as main's selectMenuItem", _ {
		// `preparePaging` keeps the irrelevant fifth column on page 1 (`horizontal_menu.cpp:499`), so the counter shows two
		// pages; `selectMenuItem` breaks on slots alone (`:448-466`), so its page 2 starts at that column, which sits at
		// the focused position: the walk stops there with no relevant column seen and the focus stays.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(fixed_page);
		r.shell.tick(0);
		r.press(kCrossScreenX, kCrossScreenY, 300); // past the 140 ms navigation debounce from the clock's zero
		r.shell.tick(400);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0);
		expect(static_cast<int>(menu_scene().state().columns[0].page)).to_equal(0);
	});

	it("a select turn on a focused submenu column does nothing", _ {
		// `horizontal_menu.cpp:315-318`: `if (child->isSubmenu()) return;` -- no action at all.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		(void)last_notice(); // drain
		r.press(kMidiX, kMidiY, 200); // focus SUBMENU_COL (column 2); `press()`'s own internal
		                              // tick (`at_ms + 5`) is where a notice would be raised
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(2);
		// `Submenu::showNotification()` is false (`submenu.h:67`), and `displayNotification` returns
		// early for it (`horizontal_menu.cpp:336-338`) -- a button focus onto a submenu column raises
		// no banner, unlike every value column's own new-focus notification (see the KIT/toggle test
		// below).
		//
		// **Must be read right here, before any further tick.** `Shell::tick` drains
		// `notify::notices()` at the *start* of the next tick (`shell.cpp:330`), so an extra
		// `r.shell.tick(220)` here (as an earlier version of this test had) would silently eat any
		// notice raised at `press()`'s own internal tick before `last_notice()` ever read it --
		// making this assertion pass whether or not a banner was actually raised. Verified by
		// mutation: temporarily deleting `notify_column`'s `Kind::Submenu` guard made this fail
		// (see task-2-report.md's fix-round-2 section for the failing output).
		expect(last_notice().empty()).to_be_true();
		r.shell.tick(220);

		r.turn(1, 400);
		r.shell.tick(410);

		expect(depth_of()).to_equal(0); // no descend
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(2); // no move either
	});

	it("KIT focuses column 1; pressing KIT again flips the toggle and notifies", _ {
		// `handleInstrumentButtonPress`'s new-focus tail (`horizontal_menu.cpp:424-437`) and
		// `handleItemAction` (`:663-679`).
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		g_toggle_col = false;

		// `press()` already settles with its own internal tick; reading the notice after a
		// SECOND tick would drain it first (`Shell::tick` drains `notify::notices()` at the start
		// of the next tick, `shell.cpp:330`), so it must be read right after `press()` returns.
		(void)last_notice(); // drain
		r.press(kKitX, kKitY, 200); // new focus: notifies, does not flip
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);
		expect(g_toggle_col).to_be_false();
		const std::string monophonic{deluge::l10n::get(String::STRING_FOR_MONOPHONIC)};
		{ // new-focus notification (`:437` -> `:342`); a `Toggle` spells no value (`menu_item.h:324`)
			const deluge::notify::Notice n = expect_one_notification(self, raised_notices());
			expect(std::string(n.notification_title())).to_equal(monophonic);
			expect(std::string(n.notification_value()).empty()).to_be_true();
		}

		(void)last_notice(); // drain
		r.press(kKitX, kKitY, 500); // already focused: the column's own press
		expect(g_toggle_col).to_be_true();
		{
			const deluge::notify::Notice n = expect_one_notification(self, raised_notices());
			expect(std::string(n.notification_title())).to_equal(monophonic);
		}
	});

	it("a select press on a focused toggle column flips and stays -- not ascend", _ {
		// `Toggle::selectButtonPress` (`gui/menu_item/toggle.h:38-42`): toggles and returns
		// `NO_NAVIGATION` when the page, not the toggle, is the current item, so
		// `SoundEditor::buttonAction`'s SELECT arm does nothing further (`sound_editor.cpp:394-419`)
		// -- no chain, no ascend. `Selection::selectButtonPress` (`gui/menu_item/selection.h:54-63`) has the
		// identical shape when `isToggle()` is true -- ported as `Node::selection_is_toggle`, whose
		// own doc names the three live examples on shipped horizontal pages
		// (`delay::PingPong`/`delay_ping_pong`, `randomizer::RandomizerLock`/`randomizer_lock`,
		// `sample::PitchSpeed`/`sample<i>_pitch_speed`); a dedicated example on `randomizer_lock`
		// covers that column kind below (`TOGGLE_COL` here is `Kind::Toggle` itself, not one of the
		// three). Every OTHER `NO_NAVIGATION`-returning `selectButtonPress` override in
		// `gui/menu_item/` either has no ported column kind (`ActiveScalesItem`, `PerformEditorItem`,
		// `ActionItem`, `ContextMenuItem`, `SourceSelectionItem`) or sits only on a vertical page in
		// the shipped tree (`Selection::isToggle()`'s other three overriders --
		// `arpeggiator::Mode`, `arpeggiator::IncludeInKitArp`, `runtime_feature::SettingToggle`).
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		g_toggle_col = false;
		r.press(kKitX, kKitY, 200); // focus TOGGLE_COL (column 1)
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);

		r.enter(500);
		r.shell.tick(510);
		expect(g_toggle_col).to_be_true();
		expect(depth_of()).to_equal(0); // stayed -- no ascend
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);

		r.enter(800);
		r.shell.tick(810);
		expect(g_toggle_col).to_be_false(); // flips right back
		expect(depth_of()).to_equal(0);
	});

	it("a select press on the real randomizer_lock column flips and stays -- Selection::isToggle, "
	   "not Kind::Toggle",
	   _ {
		// `randomizer_lock` (`nodes/sound_randomizer.h`) is `Kind::Enum` with `Node::
		// selection_is_toggle` set, the port of `randomizer::RandomizerLock::isToggle() { return
		// true; }` (`gui/menu_item/randomizer/randomizer_lock.h:62`) -- one of the three live
		// examples `Node::selection_is_toggle`'s doc names, and the first column
		// (`kRandomizerChildren[0]`, SYNTH) of the real, shipped `randomizer` horizontal page.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		// Not `r.open()`: that assigns ids over this file's own local `kRoots`, not the real tree
		// `nodes::randomizer` lives in.
		const MenuContext ctx =
		    menu::context_for(*r.song.song, deluge::gui::menuViewOf(r.song.clip), 0, false, false, false, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::randomizer, ctx);
		r.shell.init(SceneId{SceneKind::FirstApp, menu::node_id(nodes::randomizer)});
		r.shell.tick(0);
		spec::menu_engine(ctx).arp->randomizerLock = false;
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0); // default focus

		r.enter(100);
		r.shell.tick(110);
		expect(spec::menu_engine(ctx).arp->randomizerLock).to_be_true();
		expect(depth_of()).to_equal(0); // stayed -- no ascend, no chain
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0);

		r.enter(400);
		r.shell.tick(410);
		expect(spec::menu_engine(ctx).arp->randomizerLock).to_be_false(); // flips right back
	});

	it("SYNTH focuses randomizer_lock; pressing SYNTH again flips it and notifies -- the "
	   "already-focused-press half",
	   _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx =
		    menu::context_for(*r.song.song, deluge::gui::menuViewOf(r.song.clip), 0, false, false, false, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::randomizer, ctx);
		r.shell.init(SceneId{SceneKind::FirstApp, menu::node_id(nodes::randomizer)});
		r.shell.tick(0);
		spec::menu_engine(ctx).arp->randomizerLock = false;

		r.press(kKitX, kKitY, 200); // move focus off column 0 first (spread_gate, column 1)
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);

		(void)last_notice(); // drain
		r.press(kSynthX, kSynthY, 500); // new focus onto randomizer_lock: notifies, does not flip
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0);
		expect(spec::menu_engine(ctx).arp->randomizerLock).to_be_false();
		std::string title;
		{
			const deluge::notify::Notice n = expect_one_notification(self, raised_notices());
			title = n.notification_title();
			expect(title.empty()).to_be_false();
		}

		(void)last_notice(); // drain
		r.press(kSynthX, kSynthY, 800); // already focused: flips and notifies
		expect(spec::menu_engine(ctx).arp->randomizerLock).to_be_true();
		{
			const deluge::notify::Notice n = expect_one_notification(self, raised_notices());
			expect(std::string(n.notification_title())).to_equal(title); // the same column, notified again
		}
	});

	it("Back at depth 0 on a horizontal page cancels its notification too", _ {
		// `SoundEditor::goUpOneLevel` runs `endScreen()` (-> `HorizontalMenu::endSession`'s cancel)
		// **before** its depth-0 arm's `exitCompletely()` (`sound_editor.cpp:711-716`) -- the one
		// path `descend_to`/`ascend`'s own cancels do not cover, since neither runs for a Back that
		// closes the whole menu. Ships for real: `reroot_to_scene_opener_page()` lands a claimed
		// file selector's browser back at depth 0.
		const NoNotificationSink no_sink;
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);

		r.turn(1, 100); // a select turn on ENUM_COL (column 0) notifies unconditionally
		r.shell.tick(110); // pump_input raises the notice (queued, not yet drained --
		                   // `Shell::tick` step 2 drains what a PRIOR tick queued, `shell.cpp:330`)
		r.shell.tick(120); // this tick's step 2 drains it, pushing `SceneKind::Notification` (no sink)
		expect(r.shell.state_lines().find("scene Notification") != std::string::npos).to_be_true();
		expect(r.shell.state_lines().find("scene Console") != std::string::npos).to_be_false();

		r.button(kBackX, kBackY, true, 200);
		r.button(kBackX, kBackY, false, 220);
		r.shell.tick(230); // pump_input runs the depth-0 Back arm, queuing `CancelNotification`
		r.shell.tick(240); // this tick's step 2 drains it, removing `SceneKind::Notification`
		expect(r.shell.state_lines().find("scene Notification") != std::string::npos).to_be_false();
	});

	it("an unrelated Console notice survives a menu cancel", _ {
		// Undo/Redo (`action_logger.cpp:827`, `:840`) and the startup banner are `Kind::Console`
		// lines, and legacy's cancel of the menu's notification never touches the console:
		// `cancelPopup` -> `removePopup` clears only the popup slot (`oled.h:164`,
		// `oled.cpp:253-258`). The menu's notification is its own `Notification` banner now, so
		// cancelling it must take the banner down and leave the unrelated line, and its scene, up.
		const NoNotificationSink no_sink;
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);

		(void)deluge::notify::notice_text("UNRELATED", deluge::notify::Notice::Kind::Console);
		r.shell.tick(50); // queues it
		r.shell.tick(60); // drains it -- pushes `SceneKind::Console`
		expect(r.shell.state_lines().find("scene Console") != std::string::npos).to_be_true();

		r.turn(1, 100); // the menu's own notification
		r.shell.tick(110);
		r.shell.tick(120);
		expect(r.shell.state_lines().find("scene Notification") != std::string::npos).to_be_true();

		r.button(kBackX, kBackY, true, 200);
		r.button(kBackX, kBackY, false, 220);
		r.shell.tick(230); // queues `CancelNotification`
		r.shell.tick(240); // drains it -- the banner goes, "UNRELATED" does not
		expect(r.shell.state_lines().find("scene Notification") != std::string::npos).to_be_false();
		expect(r.shell.state_lines().find("scene Console") != std::string::npos).to_be_true();
	});

	it("two navigation presses inside 140 ms: the second is ignored", _ {
		// `horizontal_menu.cpp:77-82`, one debounce shared by all six buttons.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);

		r.press(kKitX, kKitY, 200); // accepted: focus -> column 1
		r.shell.tick(220);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);

		r.press(kSynthX, kSynthY, 250); // 50 ms later: inside the 140 ms window, ignored
		r.shell.tick(270);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1); // unchanged

		r.press(kSynthX, kSynthY, 450); // 200 ms after the accepted press: outside the window
		r.shell.tick(470);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0);
	});

	it("CROSS_SCREEN / SCALE page forward and back with wrap", _ {
		// `switchVisiblePage` (`horizontal_menu.cpp:345-366`), reached from `:85-94`.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		expect(static_cast<int>(menu_scene().state().columns[0].page)).to_equal(0);

		r.press(kCrossScreenX, kCrossScreenY, 200); // page forward: page 1 (RANGE_COL/FILLER_COL)
		r.shell.tick(220);
		expect(static_cast<int>(menu_scene().state().columns[0].page)).to_equal(1);

		r.press(kCrossScreenX, kCrossScreenY, 500); // forward again: wraps back to page 0
		r.shell.tick(520);
		expect(static_cast<int>(menu_scene().state().columns[0].page)).to_equal(0);

		r.press(kScaleX, kScaleY, 800); // backward: wraps to page 1
		r.shell.tick(820);
		expect(static_cast<int>(menu_scene().state().columns[0].page)).to_equal(1);
	});

	it("SHIFT+CROSS_SCREEN at depth 0 moves to the next chained menu, SHIFT+SCALE back", _ {
		// `HorizontalMenu::buttonAction` -> `switchHorizontalMenu` (`horizontal_menu.cpp:84-94`, `:364-394`) over
		// `horizontalMenusChainForSound` (`menus.cpp:2028-2035`): FILTERS is followed by EQ.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open_real(menu::nodes::filters_group);
		r.shell.tick(0);
		r.button(kShiftX, kShiftY, true, 250);
		r.press(kCrossScreenX, kCrossScreenY, 300);
		r.shell.tick(400);
		expect(menu_scene().focused() == &menu::nodes::eq).to_be_true();
		r.press(kScaleX, kScaleY, 600);
		r.shell.tick(700);
		expect(menu_scene().focused() == &menu::nodes::filters_group).to_be_true();
		r.button(kShiftX, kShiftY, false, 800);
	});

	it("switching onto mod FX lands on its page 1", _ {
		// `switchHorizontalMenu`'s `desired_page` (`horizontal_menu.cpp:383`).
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		const MenuContext ctx = r.open_real(menu::nodes::eq);
		menu::bindings::write(menu::nodes::mod_fx_type, ctx, 1); // an effect, so the page has its controls' page
		r.shell.tick(0);
		r.button(kShiftX, kShiftY, true, 250);
		r.press(kCrossScreenX, kCrossScreenY, 300);
		r.shell.tick(400);
		r.button(kShiftX, kShiftY, false, 450);
		expect(menu_scene().focused() == &menu::nodes::mod_fx).to_be_true();
		expect(static_cast<int>(menu_scene().horizontal_paging().page)).to_equal(1);
	});

	it("select on FILTER ROUTE in Sound > FILTERS opens the filters group on its routing page, and BACK returns", _ {
		// `SoundEditor::buttonAction`'s select arm (`sound_editor.cpp:630-634`): `maybeGetParentMenu` finds the chained
		// group that lists the item, and it is entered focused on it.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open_real(menu::nodes::sound_filters);
		r.shell.tick(0);
		r.turn(2, 100);
		r.shell.tick(150);
		r.enter(300);
		r.shell.tick(400);
		expect(menu_scene().focused() == &menu::nodes::filters_group).to_be_true();
		expect(static_cast<int>(menu_scene().horizontal_paging().page)).to_equal(2);
		r.button(kBackX, kBackY, true, 500);
		r.shell.tick(510);
		r.button(kBackX, kBackY, false, 520);
		r.shell.tick(530);
		expect(menu_scene().focused() == &menu::nodes::sound_filters).to_be_true();
	});

	it("a select press on a value column follows its select_chain; with none, it ascends -- no notification either way",
	   _ {
		// `Submenu::selectButtonPress` (`submenu.cpp:187-194`) forwarded through
		// `SoundEditor::buttonAction`'s SELECT arm (`sound_editor.cpp:394-424`); no
		// `displayNotification` on this arm at all.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);
		r.press(kCvX, kCvY, 200); // focus CHAIN_COL (column 3), which has a select_chain
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(3);

		(void)last_notice();
		r.enter(500);
		r.shell.tick(510);
		expect(depth_of()).to_equal(1);
		expect(menu_scene().focused() == &chain_target).to_be_true();
		expect(last_notice().empty()).to_be_true();

		// Back to the page. Column focus at depth 0 is untouched by the descent -- `ascend()`
		// restores the level's own cursor, and nothing wrote `columns[0]` while depth 1 was on
		// screen -- so CHAIN_COL is still focused; a select press here would just re-enter the
		// same chain. Move to ENUM_COL (no chain) with SYNTH before testing the ascend arm.
		r.button(kBackX, kBackY, true, 600);
		r.button(kBackX, kBackY, false, 620);
		r.shell.tick(630);
		expect(depth_of()).to_equal(0);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(3);

		r.press(kSynthX, kSynthY, 800); // ENUM_COL, no select_chain
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0);

		(void)last_notice();
		r.enter(1000);
		r.shell.tick(1010);
		// `ascend()` at depth 0 is the port's own carried no-op (`menu_scene.cpp`'s note on
		// `ascend`), so this stays open -- the claim is only that nothing entered and nothing
		// notified.
		expect(depth_of()).to_equal(0);
		expect(last_notice().empty()).to_be_true();
	});

	it("button focus onto a range_dependent column runs the MustSelectRange check", _ {
		// `handleInstrumentButtonPress`'s `initializeItem` arm (`:424-433`), ported through
		// `enter_range_picker_if_needed`.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		auto* sound = static_cast<SoundInstrument*>(r.song.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1); // two ranges: MustSelectRange, not Yes
		deluge::edits::recordRanges(*sound, 0); // recorded, as a gesture records them
		(void)r.open(horiz_page);
		r.shell.tick(0);

		r.press(kCrossScreenX, kCrossScreenY, 200); // page forward: `switchVisiblePage` keeps the
		                                            // on-page position, which lands focus on
		                                            // RANGE_COL (page 1, position 0) directly
		expect(static_cast<int>(menu_scene().state().columns[0].page)).to_equal(1);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(4);

		// Move off RANGE_COL first, so the SYNTH press below is a genuine new-focus transition --
		// `handleItemAction`'s already-focused arm (tested above) does not run the range check at
		// all, only `handleInstrumentButtonPress`'s new-focus tail does.
		r.press(kKitX, kKitY, 500); // FILLER_COL, page 1 position 1
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(5);

		r.press(kSynthX, kSynthY, 800); // back onto RANGE_COL: a new focus this time

		// The picker opened instead of the column itself -- a real descent, one level deeper.
		expect(depth_of()).to_equal(1);
		expect(menu_scene().state().range_picker_pending).to_be_true();
		// And the page's own focus was still assigned to RANGE_COL first, exactly as
		// `handleInstrumentButtonPress` assigns `current_item_` before the range check.
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(4);
	});

	it("allow_session_from_horizontal: new focus skips MustSelectRange, an already-focused press "
	   "does not",
	   _ {
		// `Node::allow_session_from_horizontal`'s two call sites, both read on SESSION_COL --
		// identical to RANGE_COL except for this one bit:
		//   * `handleInstrumentButtonPress`'s new-focus tail (`horizontal_menu.cpp:427-428`):
		//     `MUST_SELECT_RANGE && !allowToBeginSessionFromHorizontalMenu()` -- the bit suppresses
		//     the picker entirely, so a first press onto this column just focuses it.
		//   * `handleItemAction`'s already-focused press (`:663-677`): the bit does NOT suppress
		//     the same check here -- permission is read unconditionally, and `MUST_SELECT_RANGE`
		//     still opens the picker, exactly as it would for an ordinary range-dependent column.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		auto* sound = static_cast<SoundInstrument*>(r.song.instrument);
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1); // two ranges: MustSelectRange, not Yes
		deluge::edits::recordRanges(*sound, 0); // recorded, as a gesture records them
		(void)r.open(horiz_page);
		r.shell.tick(0);

		r.press(kCrossScreenX, kCrossScreenY, 200); // page 1: RANGE_COL, FILLER_COL, SESSION_COL
		r.press(kKitX, kKitY, 500); // FILLER_COL first, so the MIDI press below is a genuine
		                           // new-focus transition onto SESSION_COL
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(5);

		r.press(kMidiX, kMidiY, 800); // new focus onto SESSION_COL: picker suppressed
		expect(depth_of()).to_equal(0);
		expect(menu_scene().state().range_picker_pending).to_be_false();
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(6);

		r.press(kMidiX, kMidiY, 1100); // already focused: permission checked unconditionally
		expect(depth_of()).to_equal(1);
		expect(menu_scene().state().range_picker_pending).to_be_true();
	});

	it("LEDs: SYNTH/KIT/MIDI/CV light the focused column's slots", _ {
		// `updateSelectedMenuItemLED` (`horizontal_menu.cpp:546-572`).
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(horiz_page);
		r.shell.tick(0);

		deluge::ui::LedFrame f;
		menu_scene().leds(f);
		expect(led_on(f, ButtonId::Synth)).to_be_true(); // ENUM_COL is focused, column 0
		expect(led_on(f, ButtonId::Kit)).to_be_false();
		expect(led_on(f, ButtonId::Midi)).to_be_false();
		expect(led_on(f, ButtonId::Cv)).to_be_false();
	});

	it("LEDs: SCALE and CROSS_SCREEN light only with more than one page", _ {
		// `renderOLED`'s own two-line LED update (`:118-119`).
		select_language();
		deluge::notify::dirty().drain();
		{
			Rig r;
			(void)r.open(horiz_page); // two pages
			r.shell.tick(0);
			deluge::ui::LedFrame f;
			menu_scene().leds(f);
			expect(led_on(f, ButtonId::Scale)).to_be_true();
			expect(led_on(f, ButtonId::CrossScreen)).to_be_true();
		}
		{
			Rig r;
			(void)r.open(wide_page); // one page, one wide column
			r.shell.tick(0);
			deluge::ui::LedFrame f;
			menu_scene().leds(f);
			expect(led_on(f, ButtonId::Scale)).to_be_false();
			expect(led_on(f, ButtonId::CrossScreen)).to_be_false();
		}
	});

	it("LEDs: a single four-slot value item lights none of SYNTH/KIT/MIDI/CV", _ {
		// `updateSelectedMenuItemLED`'s own single-item exception: `page_items.size() > 1 ||
		// page_items[0]->isSubmenu() || page_items[0]->getOccupiedSlots() < 4` is false here, so
		// `led_states` stays all-false.
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(wide_page);
		r.shell.tick(0);

		deluge::ui::LedFrame f;
		menu_scene().leds(f);
		expect(led_on(f, ButtonId::Synth)).to_be_false();
		expect(led_on(f, ButtonId::Kit)).to_be_false();
		expect(led_on(f, ButtonId::Midi)).to_be_false();
		expect(led_on(f, ButtonId::Cv)).to_be_false();
	});

	it("LEDs: all six off on a vertical page", _ {
		select_language();
		deluge::notify::dirty().drain();
		Rig r;
		(void)r.open(vertical_page);
		r.shell.tick(0);

		deluge::ui::LedFrame f;
		menu_scene().leds(f);
		for (ButtonId b : {ButtonId::Synth, ButtonId::Kit, ButtonId::Midi, ButtonId::Cv, ButtonId::CrossScreen,
		                   ButtonId::Scale}) {
			expect(led_on(f, b)).to_be_false();
		}
	});
});
// clang-format on
DELUGE_SPEC(menu_horizontal_controls_spec, menu_horizontal_controls)
