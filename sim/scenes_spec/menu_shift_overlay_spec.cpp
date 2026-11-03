/// @file
/// @brief The SHIFT shortcut colour overlay under a sound menu: when it shows, what it paints, and that it takes and
///        gives back the main pads and the greyout with SHIFT, as `main`'s `SoundEditor::renderMainPads` does.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/patch_cables.h"
#include "scenes/menu/nodes/patching.h"
#include "scenes/menu/pads.h"
#include "scenes/menu/shortcut_overlay.h"
#include "scenes/profile.h"
#include "scenes/registry.h"
#include "scenes/slicing/slicer_scene.h"

#include "gfx/grid/palette.h"
#include "notify/dirty_set.h"
#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "modulation/params/param.h"

#include "support/menu_rig.h"
#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"
#include "spec/ui/support/frames.h"

#include "cppspec.hpp"

#include <functional>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::MenuScene;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::PadRegions;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

namespace nodes = deluge::scenes::menu::nodes;
namespace params = deluge::modulation::params;
namespace colours = deluge::gui::colours;

constexpr uint8_t kShiftX = 8, kShiftY = 0;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	const BoardProfile& profile = deluge_profile();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};
	uint32_t now = 0;

	Rig() = default;
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	~Rig() { spec::reset_menu_singleton(shell); }

	MenuContext menu_context() const {
		return menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                         /*kit_global_fx=*/false);
	}
	/// @brief Open the menu on @p at, after @p adjust has had the context.
	void open(const Node& at, const std::function<void(MenuContext&)>& adjust = {}) {
		MenuContext ctx = menu_context();
		if (adjust) {
			adjust(ctx);
		}
		(void)menu::assign_node_ids(MenuScene::roots());
		menu_scene().open(at, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(at)});
		tick();
	}
	void tick(uint32_t dt = 10) { shell.tick(now += dt); }
	void shift(bool down) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_BUTTON, kShiftX, kShiftY, static_cast<int16_t>(down ? 255 : 0)}, now += 10);
		tick();
	}
	RGB pad_colour(uint8_t x, uint8_t y) {
		const auto& g = fake.grid().grid_storage;
		return g.has_value() ? g->at(x, y) : RGB{};
	}
};

/// Whether either layer's source on pad (14 + sx, y) passes @p allowed.
bool either_layer(uint8_t sx, uint8_t y, const std::function<bool(PatchSource)>& allowed) {
	for (const bool second : {false, true}) {
		const PatchSource s = menu::patching::mod_source_shortcut(sx, y, second);
		if (s < kLastPatchSource && allowed(s)) {
			return true;
		}
	}
	return false;
}

} // namespace

// clang-format off
describe menu_shift_overlay("scenes::MenuScene -- the SHIFT shortcut overlay", $ {
	it("shows nothing in settings or the note editors, or with the setting Off", _ {
		select_language();
		Rig r;
		MenuContext ctx = r.menu_context();
		expect(menu::overlay_shown(ctx, true)).to_be_true();
		expect(menu::overlay_shown(ctx, false)).to_be_false();
		for (const MenuContext::Root root :
		     {MenuContext::Root::Settings, MenuContext::Root::NoteEditor, MenuContext::Root::NoteRowEditor}) {
			MenuContext other = ctx;
			other.root = root;
			expect(menu::overlay_shown(other, true)).to_be_false();
		}
		ctx.shortcut_overlay = false;
		expect(menu::overlay_shown(ctx, true)).to_be_false();
	});

	it("paints the source column blue where a source may patch to the focused param", _ {
		select_language();
		Rig r;
		const MenuContext ctx = r.menu_context();
		menu::MenuState st{};
		st.path[0] = &nodes::master_volume;
		deluge::ui::test::GridRig rig;
		menu::paint_source_column(rig.frame, ctx, st, &nodes::master_volume);
		size_t blue = 0;
		for (uint8_t sx = 0; sx < 2; sx++) {
			for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
				const bool want = either_layer(sx, y, [&](PatchSource s) {
					return menu::patching::source_may_patch(ctx, s, params::GLOBAL_VOLUME_POST_FX)
					       || menu::patching::source_may_patch(ctx, s, params::LOCAL_VOLUME)
					       || menu::patching::source_may_patch(ctx, s, params::GLOBAL_VOLUME_POST_REVERB_SEND);
				});
				const bool got = rig.frame.peek(menu::kShortcutCols - 2 + sx, y) == colours::blue;
				expect(got).to_equal(want);
				blue += got ? 1 : 0;
			}
		}
		expect(blue > size_t(4)).to_be_true();
	});

	it("uses the range rule while a source pad is held on a cable page", _ {
		select_language();
		Rig r;
		const MenuContext ctx = r.menu_context();
		menu::MenuState st{};
		st.path[0] = &nodes::patch_cable_strength_regular;
		st.walk.patching_param = params::GLOBAL_VOLUME_POST_FX; // a global param: only global sources control a range
		st.walk.patch_source = PatchSource::VELOCITY;
		deluge::ui::test::GridRig rig;
		rig.input.held_pads.set(15, 1, true); // VELOCITY's pad, held
		menu::paint_source_column(rig.frame, ctx, st, &nodes::patch_cable_strength_regular);
		size_t range_cells = 0;
		for (uint8_t sx = 0; sx < 2; sx++) {
			for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
				if (sx == 1 && y == 1) {
					continue; // the held pad itself has no other pad held
				}
				const bool want = either_layer(sx, y, [](PatchSource s) { return s < kFirstLocalSource; });
				expect(rig.frame.peek(menu::kShortcutCols - 2 + sx, y) == colours::blue).to_equal(want);
				range_cells++;
			}
		}
		expect(range_cells).to_equal(size_t(15));
	});

	it("takes the main pads and greys every column while SHIFT is held, keyboard root included", _ {
		select_language();
		Rig r;
		r.open(nodes::lpf_freq, [](MenuContext& ctx) {
			ctx.root_greyout = MenuContext::RootGreyout::Nothing;
			ctx.root_is_keyboard = true;
		});
		expect(menu_scene().pad_regions() == PadRegions::None).to_be_true();
		expect(menu_scene().greyout_mask().cols).to_equal(uint32_t{0});
		r.shift(true);
		expect(menu_scene().pad_regions() == PadRegions::Main).to_be_true();
		expect(menu_scene().greyout_mask().cols).to_equal(uint32_t{0xFFFFFFFF});
		expect(r.pad_colour(8, 7) == RGB{0, 0, 0}).to_be_false(); // the cutoff's filter colour
		r.shift(false);
		expect(menu_scene().pad_regions() == PadRegions::None).to_be_true();
		expect(menu_scene().greyout_mask().cols).to_equal(uint32_t{0});
	});

	it("greys an instrument clip's settings menu as the clip's, sparing the audition column", _ {
		// `getGreyoutColsAndRows` decides by the root UI alone: the settings menu over an instrument clip greys all but
		// the audition column, as the clip's own menus do.
		select_language();
		Rig r;
		MenuContext ctx = r.menu_context();
		ctx.root = MenuContext::Root::Settings;
		ctx.addresses.clip.reset(); // the settings context names no clip
		ctx.root_greyout = MenuContext::RootGreyout::InstrumentClip;
		menu::MenuState st{};
		st.path[0] = &nodes::root_settings;
		expect(menu::greyout_mask(ctx, st).cols).to_equal(uint32_t{0xFFFFFFFE});
	});

	it("stays up while shift is latched", _ {
		select_language();
		Rig r;
		r.shell.lowering().set_shift_is_sticky(true);
		r.open(nodes::lpf_freq);
		r.shift(true);
		r.shift(false); // a tap: latched
		expect(menu_scene().pad_regions() == PadRegions::Main).to_be_true();
		r.shift(true);
		r.shift(false); // a second tap: released
		expect(menu_scene().pad_regions() == PadRegions::None).to_be_true();
		r.shell.lowering().set_shift_is_sticky(false);
	});

	it("is gone the tick a scene is pushed over the menu", _ {
		select_language();
		Rig r;
		r.open(nodes::lpf_freq);
		r.shift(true);
		expect(menu_scene().pad_regions() == PadRegions::Main).to_be_true();
		menu_scene().state_for_spec().walk.pending_scene = SceneId{slicing::kSlicerSceneKind, 0};
		deluge::notify::raise(deluge::notify::Dirty::Oled); // a render, so `after_render` drains the push
		r.tick();
		expect(menu_scene().pad_regions() == PadRegions::None).to_be_true();
	});

	it("keeps the blink flashing over the overlay", _ {
		select_language();
		Rig r;
		r.open(nodes::lpf_freq);
		r.shift(true);
		r.fake.flashes_sent.clear();
		for (int i = 0; i < 30; i++) {
			r.tick(20);
		}
		expect(std::ranges::count(r.fake.flashes_sent, deluge::ui::grid::PadFlash{8, 7, deluge::ui::grid::FlashColour::Default}) > 0)
		    .to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(menu_shift_overlay_spec, menu_shift_overlay)
