/// @file
/// @brief The menu's shortcut pads over the ports' fakes, with a model song of the spec's own and no legacy song behind
///        the menu: where a SHIFT-held pad lands under each shortcut layout the device fake holds, which source pads
///        blink for a param the sound fake patches, and the SHIFT overlay following the device fake's
///        `features/shortcutOverlay` row.

#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/menu_state.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/shortcut_blink.h"
#include "ui/scenes/menu/shortcuts.h"

#include "instruments/classic/cables.h"
#include "instruments/classic/sound_state.h"
#include "project/device_settings.h"
#include "project/features.h"
#include "synth/modulation/cable.h"
#include "synth/modulation/patch_source.h"
#include "ui/shell/scene.h"

#include "support/fake_menu_shell.h"
#include "support/record_state.h"

#include "cppspec.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>

#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::project::DevicePath;
using deluge::project::Feature;
using deluge::project::ShortcutOverlay;
using deluge::project::ShortcutsVersion;
using deluge::synth::PatchSource;
using deluge::ui::scenes::menu::kNoBlink;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::spec::FakeHostRig;
using deluge::ui::scenes::spec::FakeMenuShell;
using deluge::ui::shell::PadRegions;
using device_path = deluge::project::device_path;

namespace {

namespace nodes = deluge::ui::scenes::menu::nodes;

/// The sample pad that differs between the layouts: interpolation under V3, the sample's end point under V1.
constexpr uint8_t kSamplePadX = 0, kSamplePadY = 6;
/// The X source's pad, which only the V3 layout names.
constexpr uint8_t kXSourcePadX = 14, kXSourcePadY = 7;
/// The first global LFO's pad, which both layouts name.
constexpr uint8_t kLfoSourcePadY = 5;

/// @brief Hold @p menu_value in the device fake's row at @p path, as a write through the device port stores it, and
///        forget the store, so an example sees only the stores the menu makes.
void hold_device(FakeHostRig& fakes, DevicePath path, int32_t menu_value) {
	(void)fakes.host.device.write(path, menu_value);
	fakes.host.device.stores.clear();
}

/// @brief The synth's first source a loaded sample, so the menu lists the sample's own pages.
void hold_sample_synth(FakeHostRig& fakes) {
	fakes.hold_synth(deluge::test::with_kind(deluge::instruments::classic::SoundState{},
	                                         deluge::instruments::classic::SourceId::A,
	                                         deluge::instruments::classic::OscType::SAMPLE));
	fakes.host.sound.loaded[FakeHostRig::kSynthSlot.id] = 0b01;
}

/// @brief The sound fake patching @p sources to the low-pass cutoff.
void patch_to_cutoff(FakeHostRig& fakes, std::initializer_list<PatchSource> sources) {
	const auto cutoff = deluge::instruments::classic::cable_target_of(*nodes::lpf_freq.bind.param.id);
	for (const PatchSource source : sources) {
		fakes.host.sound.cable_list.push_back({.depth = deluge::instruments::classic::cable_depth_of(source, cutoff)});
	}
}

/// @brief The source pads @p plan blinks, as "x,y" in column-then-row order.
std::string blinking_sources(const menu::BlinkPlan& plan) {
	std::string out;
	for (uint8_t sx = 0; sx < 2; sx++) {
		for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
			if (plan.sources[sx][y].freq_mask != kNoBlink) {
				out += std::to_string(menu::kShortcutCols - 2 + sx) + "," + std::to_string(y) + " ";
			}
		}
	}
	return out;
}

} // namespace

// clang-format off
describe menu_shortcuts_fakes("the menu's shortcut pads over the ports' fakes and a model song", ${
	it("lands the sample pad on interpolation under the device fake's V3 layout, and names the end point under V1", _ {
		FakeMenuShell r;
		hold_sample_synth(r.fakes);
		hold_device(r.fakes, device_path::kShortcutsVersion, std::to_underlying(ShortcutsVersion::V3));
		const MenuContext ctx = r.open_synth(nodes::sound_root);
		r.shift_pad(kSamplePadX, kSamplePadY, 100);
		expect(menu_scene().focused_value()).to_equal(&nodes::sample0_interpolation);

		// The lookup reads the layout from the device port the context binds, so the fake's V1 moves the pad. V1 is
		// checked at the lookup rather than pressed: every cell only V1 has is a sample marker item, and landing on one
		// opens `MarkerScene`, which reads the host's ports and waveforms rather than the fakes.
		hold_device(r.fakes, device_path::kShortcutsVersion, std::to_underlying(ShortcutsVersion::V1));
		expect(menu::shortcuts_version(ctx)).to_equal(ShortcutsVersion::V1);
		expect(menu::shortcut_node(ctx.root, menu::shortcuts_version(ctx), false, kSamplePadX, kSamplePadY))
		    .to_equal(&nodes::sample0_end);
		expect(r.fakes.host.device.stores.size()).to_equal(size_t{0});
	});

	it("opens the X source's cable from its pad under V3, and swallows the pad under V1, which names no X", _ {
		for (const ShortcutsVersion version : {ShortcutsVersion::V3, ShortcutsVersion::V1}) {
			FakeMenuShell r;
			hold_device(r.fakes, device_path::kShortcutsVersion, std::to_underlying(version));
			(void)r.open_synth(nodes::lpf_freq);
			r.shift_pad(kXSourcePadX, kXSourcePadY, 100);
			if (version == ShortcutsVersion::V3) {
				expect(menu_scene().focused()).to_equal(&nodes::patch_cable_strength_regular);
				expect(menu_scene().state().walk.patch_source).to_equal(PatchSource::X);
			}
			else {
				expect(menu_scene().focused_value()).to_equal(&nodes::lpf_freq);
			}
		}
	});

	it("blinks the pads of the sources the sound fake patches to the focused param, X only under V3", _ {
		FakeHostRig fakes;
		patch_to_cutoff(fakes, {PatchSource::LFO_GLOBAL_1, PatchSource::X});
		const MenuContext ctx = fakes.synth_context();
		menu::MenuState st{};
		st.path[0] = &nodes::lpf_freq;

		hold_device(fakes, device_path::kShortcutsVersion, std::to_underlying(ShortcutsVersion::V3));
		const menu::BlinkPlan v3 = menu::plan_blinks(ctx, st, nodes::lpf_freq);
		expect(v3.param.valid).to_be_true();
		expect(blinking_sources(v3)).to_equal(std::string{"14,5 14,7 "});
		// A patched param's sources flash every fourth cycle.
		expect(v3.sources[0][kLfoSourcePadY].freq_mask).to_equal(uint8_t{3});

		hold_device(fakes, device_path::kShortcutsVersion, std::to_underlying(ShortcutsVersion::V1));
		const menu::BlinkPlan v1 = menu::plan_blinks(ctx, st, nodes::lpf_freq);
		expect(blinking_sources(v1)).to_equal(std::string{"14,5 "});

		// With nothing patched, nothing blinks in the source column.
		fakes.host.sound.cable_list.clear();
		expect(menu::plan_blinks(ctx, st, nodes::lpf_freq).any_source()).to_be_false();
	});

	it("shows the SHIFT overlay as the device fake's shortcutOverlay row says, and not at all with it Off", _ {
		const DevicePath overlay = device_path::feature(Feature::SHORTCUT_OVERLAY);
		for (const ShortcutOverlay setting : {ShortcutOverlay::MENU_ONLY, ShortcutOverlay::OFF}) {
			FakeMenuShell r;
			hold_device(r.fakes, overlay, std::to_underlying(setting));
			const MenuContext ctx = r.open_synth(nodes::lpf_freq);
			const bool shown = setting != ShortcutOverlay::OFF;
			expect(ctx.shortcut_overlay).to_equal(shown);
			expect(menu_scene().pad_regions()).to_equal(PadRegions::None);
			r.shift(true, 100);
			expect(menu_scene().pad_regions()).to_equal(shown ? PadRegions::Main : PadRegions::None);
			if (shown) {
				// Every column is greyed under the overlay.
				expect(menu_scene().greyout_mask().cols).to_equal(uint32_t{0xFFFFFFFF});
			}
			r.shift(false, 200);
			expect(menu_scene().pad_regions()).to_equal(PadRegions::None);
		}
	});
});
// clang-format on

DELUGE_SPEC(menu_shortcuts_fakes_spec, menu_shortcuts_fakes)
