/// @file
/// @brief The two preset-scale checklists -- `active_scales` and `default_active_scale` -- against
///        the real legacy `ActiveScaleMenu` objects they port.
///
/// Both sides are reachable (`activeScaleMenu` and `defaultActiveScaleMenu` are the shipped
/// singletons, and the two bitsets they edit are model/`FlashStorage` state the port reads through
/// the same names) and both sides are **driven** in every comparison: legacy through
/// `selectEncoderAction` / `selectButtonPress` / `renderOLED`, the port through a real `Shell` and
/// the real input path, each from its own copy of the same sentinel bitset.
///
/// The trap this spec is shaped against is the one that matters for a *pair* of near-identical
/// items: `active_scales` edits the song's bitset and `default_active_scale` edits
/// `FlashStorage`'s, and nothing about the two renders distinguishes them. So the item-to-bitset
/// mapping is proved by mutation -- each item is driven and the *other* bitset is asserted
/// unchanged -- rather than by reading the two `ScaleSet` values back out of the items that carry
/// them.

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/scales.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/active_scales.h"
#include "hid/display/oled.h"
#include "model/scale/preset_scales.h"
#include "model/song/song.h"
#include "storage/flash_storage.h"

#include "support/song_fixture.h"

#include "spec/gfx/support/golden.h"
#include "spec/gfx/support/pbm.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <bitset>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

/// The two real legacy items (`gui/ui/menus.cpp:1670`, `:2071`). Declared here because
/// `gui/ui/menus.h` exports neither -- the same situation `tables_3_spec.cpp:589` records.
extern deluge::gui::menu_item::ActiveScaleMenu defaultActiveScaleMenu;
extern deluge::gui::menu_item::ActiveScaleMenu activeScaleMenu;

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::scenes::menu::nodes::ScaleSet;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

/// Board encoder indices (`kDelugeEncoders`, `shell/profiles/deluge.cpp:69-76`) and the
/// select-encoder button's matrix position (`kMatrix`).
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;

constexpr const Node* kRoots[] = {&nodes::root_settings, &nodes::root_song};

/// @brief Saves and restores both bitsets, so no example leaks into the next.
struct ScalesGuard {
	std::bitset<NUM_PRESET_SCALES> saved_flash = FlashStorage::defaultDisabledPresetScales;
	std::bitset<NUM_PRESET_SCALES> saved_song{};
	Song* song = currentSong;

	ScalesGuard() {
		if (song != nullptr) {
			saved_song = song->disabledPresetScales;
		}
	}
	ScalesGuard(const ScalesGuard&) = delete;
	ScalesGuard& operator=(const ScalesGuard&) = delete;
	~ScalesGuard() {
		FlashStorage::defaultDisabledPresetScales = saved_flash;
		if (song != nullptr) {
			song->disabledPresetScales = saved_song;
		}
	}
};

/// @brief A 128x64 single-panel board (`menu_scene_spec.cpp:293-314`).
constexpr DelugeDisplayDesc kDisplays64[] = {{128, 64, 0}};

DelugeBoard board_64() {
	DelugeBoard b = deluge::ui::shell::kDelugeBoard;
	b.display_height = 64;
	b.display_count = 1;
	b.displays = kDisplays64;
	return b;
}

const BoardProfile& profile_64() {
	static const BoardProfile p = [] {
		static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so every scene's `on_exit` always runs (E3 task 10).
	///
	/// `MenuScene` is a process singleton (`shell/scene.h`'s `SceneState` cache): an example that
	/// left it -- or anything pushed above it -- on the stack would make the NEXT example's
	/// `on_enter` see stale state instead of a fresh one, the cross-contamination
	/// `recorder_scene_spec.cpp`'s `~Rig` was written to stop (a leaked recorder there made the next
	/// `on_enter` refuse outright and killed 13 of 16 examples on one mutant's first run). Popping
	/// unconditionally means every example starts from the same place whatever the one before it did.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	MenuContext open(const Node& root) {
		const MenuContext ctx =
		    menu::context_for(*song.song, song.clip, 0, false, false, false, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(kRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		return ctx;
	}

	void edges(uint8_t index, int16_t count, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, index, 0, count}, at_ms);
	}
	/// One detent of the (detented, two edges per detent) select encoder.
	void turn(int32_t detents, uint32_t at_ms) { edges(kSelectEncoder, static_cast<int16_t>(detents * 2), at_ms); }
	void press(uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, kSelectEncButtonX, kSelectEncButtonY, 255}, at_ms);
	}
	/// @brief Land the cursor on @p row through the real input path, one detent per tick.
	void walk_to(int32_t row, uint32_t& at) {
		for (int32_t i = 0; i < row; i++) {
			turn(1, at);
			shell.tick(at + 5);
			at += 10;
		}
	}
};

menu::MenuScene& scene() {
	return menu_scene();
}

/// @brief Move the legacy item's own `currentPos` to @p row, from wherever the last example left it.
///
/// `currentPos` is private and persists across visits (`beginSession` is only a redraw,
/// `active_scales.cpp:17-19`), so it is driven to a known row rather than assumed: enough single
/// down-detents to reach the bottom, then @p row single up-detents.
///
/// @note **One detent at a time, deliberately.** `ActiveScaleMenu::selectEncoderAction`
///       (`active_scales.cpp:101-106`) is
///       `std::clamp((int8_t)(currentPos + offset), (int8_t)0, (int8_t)LAST_PRESET_SCALE)` -- the sum
///       is narrowed to `int8_t` **before** the clamp, so a large offset wraps and lands somewhere
///       unrelated: `selectEncoderAction(-1000)` from row 0 gives `(int8_t)(-1000) == 24`, which
///       clamps *up* to `LAST_PRESET_SCALE` rather than staying at 0. Found by this spec, which was
///       written with the big-offset spelling and produced exactly that off-by-a-list. It is
///       unreachable on the device -- the encoder delivers one detent per pass -- and it is not
///       ported: `ui::oled::list` walks the cursor one selectable row per detent and clamps in
///       `int32_t`. Recorded here rather than silently normalised away.
void legacy_walk_to(deluge::gui::menu_item::ActiveScaleMenu& item, int32_t row) {
	for (int32_t i = 0; i <= static_cast<int32_t>(LAST_PRESET_SCALE); i++) {
		item.selectEncoderAction(-1);
	}
	for (int32_t i = 0; i < row; i++) {
		item.selectEncoderAction(1);
	}
}

/// @brief `OLED::main` -- what `MenuItem::renderOLED` draws into -- as a PBM.
std::string legacy_panel_pbm() {
	const auto& canvas = deluge::hid::display::OLED::main;
	return page_major_to_pbm(canvas.bytes().data(), OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS);
}

/// @brief Drop the five bezel rows the port's root frame cannot ink, from both sides.
///        (`context_menu_scene_spec.cpp`'s `below_bezel`, same reasoning.)
std::string below_bezel(const std::string& pbm) {
	size_t pos = 0;
	int newlines = 0;
	while (pos < pbm.size() && newlines < 2) {
		if (pbm[pos] == '\n') {
			newlines++;
		}
		pos++;
	}
	std::string out = pbm.substr(0, pos);
	for (int i = 0; i < 5 && pos < pbm.size(); i++) {
		const size_t next = pbm.find('\n', pos);
		pos = next == std::string::npos ? pbm.size() : next + 1;
	}
	out += pbm.substr(pos);
	return out;
}

/// @brief A repeatable, asymmetric starting bitset -- not all-on and not all-off, so a renderer that
///        drew one box for every row would be visible.
std::bitset<NUM_PRESET_SCALES> sentinel_bits() {
	std::bitset<NUM_PRESET_SCALES> bits;
	for (size_t i = 0; i < NUM_PRESET_SCALES; i++) {
		bits[i] = (i % 3) == 1;
	}
	return bits;
}

} // namespace

// clang-format off
describe menu_scales_table("scenes::menu the two scale checklists: the table", $ {
	it("covers exactly the two nodes, by identity", _ {
		select_language();
		const uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);
		std::set<const Node*> found;
		for (uint16_t id = 1; id <= assigned; id++) {
			const Node* n = menu::node_by_id(id);
			if (n != nullptr && n->kind == menu::Kind::Custom
			    && dynamic_cast<const nodes::ActiveScalesItem*>(n->custom) != nullptr) {
				found.insert(n);
			}
		}
		const std::set<const Node*> expected{&nodes::active_scales, &nodes::default_active_scale};
		expect(found == expected).to_be_true();
	});

	it("is not a placeholder any more", _ {
		expect(nodes::active_scales.custom->is_placeholder()).to_be_false();
		expect(nodes::default_active_scale.custom->is_placeholder()).to_be_false();
		// Two instances, not one shared: the bitset an item edits is its only state.
		expect(nodes::active_scales.custom == nodes::default_active_scale.custom).to_be_false();
	});

	it("draws one row per preset scale, and no scalelike beyond them", _ {
		// `selectEncoderAction` clamps to `LAST_PRESET_SCALE` (`active_scales.cpp:104`), so
		// `USER_SCALE` and everything else in `NUM_SCALELIKE` is out of reach -- driven on the real
		// legacy item rather than read off the constant.
		spec::SongFixture fixture = spec::make_synth_song(); // legacy reads `currentSong` directly
		ScalesGuard guard;
		legacy_walk_to(activeScaleMenu, 1000); // one detent at a time, well past the end
		// The item is now on its last row; toggling proves which one that is.
		activeScaleMenu.selectButtonPress();
		expect(bool{currentSong->disabledPresetScales[LAST_PRESET_SCALE]}).to_be_true();
		activeScaleMenu.selectButtonPress();
		expect(bool{currentSong->disabledPresetScales[LAST_PRESET_SCALE]}).to_be_false();
	});
});

describe menu_scales_write("scenes::menu the two scale checklists: the write", $ {
	it("toggles the row the cursor is on, exactly where legacy toggles it", _ {
		select_language();
		for (const int32_t row : {0, 1, 4, static_cast<int32_t>(LAST_PRESET_SCALE)}) {
			// The Rig builds the song and points `currentSong` at it, which is what the *legacy*
			// half reads (`active_scales.cpp:118-120`). So the Rig comes first and both halves run
			// over the one song -- with the sentinel rewritten between them, which is the whole
			// point: the two halves must not see each other's result.
			Rig r;
			ScalesGuard guard;

			// --- legacy, driven from the sentinel
			currentSong->disabledPresetScales = sentinel_bits();
			legacy_walk_to(activeScaleMenu, row);
			activeScaleMenu.selectButtonPress();
			const std::bitset<NUM_PRESET_SCALES> after_legacy = currentSong->disabledPresetScales;

			// --- port, from the SAME sentinel, through the real input path
			currentSong->disabledPresetScales = sentinel_bits();
			(void)r.open(nodes::active_scales);
			r.shell.tick(5);
			uint32_t at = 10;
			r.walk_to(row, at);
			expect(static_cast<int>(scene().state().list[0].cursor)).to_equal(row);
			r.press(at);
			r.shell.tick(at + 5);

			expect(currentSong->disabledPresetScales == after_legacy).to_be_true();
			// ... and something actually moved, or the comparison proves nothing.
			expect(currentSong->disabledPresetScales == sentinel_bits()).to_be_false();
		}
	});

	it("stays on the page after a toggle, which is legacy's NO_NAVIGATION", _ {
		select_language();
		Rig r;
		ScalesGuard guard;
		currentSong->disabledPresetScales.reset();
		(void)r.open(nodes::root_song);
		r.shell.tick(5);
		// Walk to `active_scales` and enter it.
		const MenuContext ctx = menu::context_for(*r.song.song, r.song.clip, 0, false, false, false, false);
		int32_t index = -1;
		{
			int32_t i = 0;
			for (const Node* child : nodes::root_song.children) {
				if (child->relevant == nullptr || child->relevant(ctx)) {
					if (child == &nodes::active_scales) {
						index = i;
					}
					i++;
				}
			}
		}
		expect(index >= 0).to_be_true();
		uint32_t at = 10;
		r.walk_to(index, at);
		r.press(at);
		r.shell.tick(at + 5);
		at += 10;
		expect(scene().focused() == &nodes::active_scales).to_be_true();
		expect(static_cast<int>(scene().state().depth)).to_equal(1);

		// Now press: the bit flips and the page stays.
		r.press(at);
		r.shell.tick(at + 5);
		expect(scene().focused() == &nodes::active_scales).to_be_true();
		expect(bool{currentSong->disabledPresetScales[0]}).to_be_true();
	});

	it("a turn moves the cursor and toggles nothing", _ {
		select_language();
		Rig r;
		ScalesGuard guard;
		currentSong->disabledPresetScales = sentinel_bits();
		(void)r.open(nodes::active_scales);
		r.shell.tick(5);
		uint32_t at = 10;
		r.walk_to(5, at);
		expect(static_cast<int>(scene().state().list[0].cursor)).to_equal(5);
		expect(currentSong->disabledPresetScales == sentinel_bits()).to_be_true();
	});

	it("each item edits its own bitset and leaves the other alone", _ {
		// The lookalike-mapping proof. Two items, two indistinguishable renders; the only way to
		// know which is which is to move one and check that the other did not move.
		select_language();
		{
			Rig r;
			ScalesGuard guard;
			currentSong->disabledPresetScales.reset();
			FlashStorage::defaultDisabledPresetScales.reset();
			(void)r.open(nodes::active_scales);
			r.shell.tick(5);
			r.press(10);
			r.shell.tick(15);
			expect(bool{currentSong->disabledPresetScales[0]}).to_be_true();
			expect(FlashStorage::defaultDisabledPresetScales.none()).to_be_true();
		}
		{
			Rig r;
			ScalesGuard guard;
			currentSong->disabledPresetScales.reset();
			FlashStorage::defaultDisabledPresetScales.reset();
			(void)r.open(nodes::default_active_scale);
			r.shell.tick(5);
			r.press(10);
			r.shell.tick(15);
			expect(bool{FlashStorage::defaultDisabledPresetScales[0]}).to_be_true();
			expect(currentSong->disabledPresetScales.none()).to_be_true();
		}
	});

	it("reads back through the same accessor the legacy item reads, for every scale", _ {
		select_language();
		spec::SongFixture fixture = spec::make_synth_song();
		ScalesGuard guard;
		const MenuContext ctx = menu::context_for(*currentSong, nullptr, 0, false, true, false, false);
		const auto* song_item = dynamic_cast<const nodes::ActiveScalesItem*>(nodes::active_scales.custom);
		const auto* default_item = dynamic_cast<const nodes::ActiveScalesItem*>(nodes::default_active_scale.custom);
		expect(song_item != nullptr).to_be_true();
		expect(default_item != nullptr).to_be_true();
		expect(static_cast<int>(song_item->set())).to_equal(static_cast<int>(ScaleSet::Song));
		expect(static_cast<int>(default_item->set())).to_equal(static_cast<int>(ScaleSet::Default));

		currentSong->disabledPresetScales = sentinel_bits();
		FlashStorage::defaultDisabledPresetScales = ~sentinel_bits(); // deliberately the complement
		for (size_t i = 0; i < NUM_PRESET_SCALES; i++) {
			expect(song_item->is_disabled(ctx, i)).to_equal(bool{currentSong->disabledPresetScales[i]});
			expect(default_item->is_disabled(ctx, i)).to_equal(bool{FlashStorage::defaultDisabledPresetScales[i]});
			// The complement makes "read the wrong bitset" a failure at every index rather than at
			// two thirds of them.
			expect(song_item->is_disabled(ctx, i) == default_item->is_disabled(ctx, i)).to_be_false();
		}
	});

	it("treats a null song as legacy does: nothing disabled, and a write that goes nowhere", _ {
		// `isDisabled`'s `else return false` and `setDisabled`'s `else if (currentSong)`
		// (`active_scales.cpp:121-123`, `:130-132`). A settings-root context always has a song, so
		// this is the defensive arm rather than a reachable screen -- ported because legacy has it.
		spec::SongFixture fixture = spec::make_synth_song();
		ScalesGuard guard;
		const MenuContext empty{};
		const auto* song_item = dynamic_cast<const nodes::ActiveScalesItem*>(nodes::active_scales.custom);
		for (size_t i = 0; i < NUM_PRESET_SCALES; i++) {
			expect(song_item->is_disabled(empty, i)).to_be_false();
		}
		song_item->set_disabled(empty, 3, true);
		expect(song_item->is_disabled(empty, 3)).to_be_false();
	});
});

describe menu_scales_pixels("scenes::menu the two scale checklists: pixels", $ {
	it("draws exactly what the real legacy ActiveScaleMenu draws, at three cursor positions", _ {
		select_language();
		for (const int32_t row : {0, 4, static_cast<int32_t>(LAST_PRESET_SCALE)}) {
			Rig r; // builds the song both halves read through `currentSong`
			ScalesGuard guard;

			// --- legacy
			currentSong->disabledPresetScales = sentinel_bits();
			legacy_walk_to(activeScaleMenu, row);
			deluge::hid::display::OLED::main.clear();
			activeScaleMenu.renderOLED();
			const std::string legacy = below_bezel(legacy_panel_pbm());

			// --- port, same bitset, same row, driven
			currentSong->disabledPresetScales = sentinel_bits();
			(void)r.open(nodes::active_scales);
			r.shell.tick(5);
			uint32_t at = 10;
			r.walk_to(row, at);
			r.shell.tick(at + 10);
			const std::string port = below_bezel(r.fake.pbm(0));

			expect(port).to_equal(legacy);
		}
	});

	it("records the goldens at both panel geometries", _ {
		select_language();
		Rig r;
		ScalesGuard guard;
		currentSong->disabledPresetScales = sentinel_bits();
		(void)r.open(nodes::active_scales);
		r.shell.tick(5);
		uint32_t at = 10;
		r.walk_to(4, at);
		r.shell.tick(at + 10);
		expect_golden(self, "../ui/menu/menu_active_scales_48.pbm", r.fake.pbm(0));

		Rig r64{board_64(), profile_64()};
		currentSong->disabledPresetScales = sentinel_bits();
		(void)r64.open(nodes::active_scales);
		r64.shell.tick(5);
		uint32_t at64 = 10;
		r64.walk_to(4, at64);
		r64.shell.tick(at64 + 10);
		expect_golden(self, "../ui/menu/menu_active_scales_64.pbm", r64.fake.pbm(0));
	});

	it("is byte-identical after twenty quiet ticks, and toggles nothing", _ {
		select_language();
		Rig r;
		ScalesGuard guard;
		currentSong->disabledPresetScales = sentinel_bits();
		(void)r.open(nodes::active_scales);
		r.shell.tick(5);
		// The focused row's side scroller is the one clock-driven thing `list` has, and every scale
		// name fits its box, so nothing may move.
		const std::string first = r.fake.pbm(0);
		for (uint32_t t = 1; t <= 20; t++) {
			r.shell.tick(5 + t * 15);
			expect(r.fake.pbm(0)).to_equal(first);
		}
		expect(currentSong->disabledPresetScales == sentinel_bits()).to_be_true();
	});
});
// clang-format on

/// Three `describe` groups in one TU, so `DELUGE_SPEC` is spelled out with `add_specs`.
int menu_scales_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(menu_scales_table, menu_scales_write, menu_scales_pixels)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
