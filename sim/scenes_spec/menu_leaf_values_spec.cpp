/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief A value leaf opened full-screen: where its cursor starts, how a turn moves it, and what it draws -- each
///        legacy `Decimal::getDefaultEditPos`, `selectEncoderAction` and `drawPixelsForOled`/`drawInteger`
///        override, pinned against `main`'s own numbers.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/present.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/dirty_set.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "processing/engines/cv_engine.h"
#include "storage/flash_storage.h"

#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"
#include "spec/ui/support/frames.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
using deluge::ui::shell::SurfaceRole;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

/// @brief The application's shell with every root numbered, and a context on the fixture's synth clip.
///
/// A leaf is opened the way a shortcut opens it alone (HorizontalMenus off): `MenuScene::open` on the leaf. The
/// examples then draw the scene straight into a `Rig48` with the input they want, so no shell tick or overlay sits
/// between a turn and the leaf.
struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	const deluge::ui::shell::BoardProfile& profile = deluge_profile();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, profile};
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};
	MenuContext ctx = menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
	                                    /*kit_global_fx=*/false);

	Rig() {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		deluge::notify::dirty().drain();
		(void)menu::assign_node_ids(nodes::kAllRoots);
	}
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state();
	}

	/// @brief Open @p leaf on its own, as the menu's only level.
	void open(const Node& leaf) {
		menu_scene().open(leaf, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(leaf)});
	}
};

/// @brief Draw the open menu once into @p rig at @p now_ms with the input @p rig holds, then clear that input.
template <typename R>
void draw(R& rig, uint32_t now_ms) {
	rig.begin_tick(now_ms);
	menu_scene().render_oled(SurfaceRole::Main, rig.frame);
	rig.input.turn = 0;
	rig.input.horizontal = 0;
}

/// @brief Do @p rig's canvas and its reference agree on every pixel of rows @p y0 to @p y1?
template <typename R>
bool rows_match(const R& rig, int y0, int y1) {
	for (int y = y0; y <= y1; y++) {
		for (int x = 0; x < 128; x++) {
			if (rig.lit(x, y) != rig.reference_lit(x, y)) {
				return false;
			}
		}
	}
	return true;
}

int decimal_pos() {
	return menu_scene().state().decimal_pos;
}

} // namespace

// clang-format off
describe leaf_cursor("scenes::menu full-screen Decimal cursor", $ {
	it("starts on Decimal::getDefaultEditPos: the units of a two-place value", _ {
		for (const Node* n : {&nodes::source0_transpose, &nodes::master_transpose, &nodes::cv_transpose,
		                      &nodes::audio_clip_transpose}) {
			Rig r;
			r.open(*n);
			expect(decimal_pos()).to_equal(2);
		}
	});

	it("starts one place lower on CV volts, gate off time and retrigger phase, which override it to 1", _ {
		for (const Node* n : {&nodes::cv_volts, &nodes::gate_off_time, &nodes::osc0_phase, &nodes::osc1_phase,
		                      &nodes::modulator0_phase_node, &nodes::modulator1_phase_node}) {
			Rig r;
			r.open(*n);
			expect(decimal_pos()).to_equal(1);
		}
	});

	it("moves the value 1.00 a detent from the start", _ {
		Rig r;
		menu::bindings::write(nodes::cv_transpose, r.ctx, 0);
		r.open(nodes::cv_transpose);
		deluge::ui::test::Rig48 rig;
		rig.input.turn = 1;
		draw(rig, 0);
		expect(menu_scene().state().edit_int).to_equal(100);
	});

});
// clang-format on

// clang-format off
describe leaf_horizontal("scenes::menu full-screen Decimal horizontal turn", $ {
	it("keeps a transpose's horizontal turn for its cursor: a Decimal is not a param's number screen", _ {
		// `Transpose` takes `Decimal::horizontalEncoderAction`; only `Param::horizontalEncoderAction` passes the
		// turn to the root.
		using HorizontalTurn = menu::MenuScene::HorizontalTurn;
		for (const Node* n : {&nodes::source0_transpose, &nodes::modulator0_transpose, &nodes::master_transpose,
		                      &nodes::audio_clip_transpose}) {
			Rig r;
			r.open(*n);
			expect(menu_scene().route_horizontal_turn(-1, false, 10) == HorizontalTurn::Menu).to_be_true();
		}
	});

	it("still passes a param's own number screen's turn to the root", _ {
		using HorizontalTurn = menu::MenuScene::HorizontalTurn;
		Rig r;
		r.open(nodes::lpf_freq);
		expect(menu_scene().route_horizontal_turn(-1, false, 10) == HorizontalTurn::Root).to_be_true();
	});
});
// clang-format on

// clang-format off
describe leaf_words("scenes::menu full-screen Decimal words", $ {
	it("draws CV volts' 0 as Hz/V with no cursor, and keeps its cursor still there", _ {
		Rig r;
		const uint8_t saved = cvEngine.cvChannels[0].voltsPerOctave;
		cvEngine.cvChannels[0].voltsPerOctave = 0;
		r.open(nodes::cv_volts);
		deluge::ui::test::Rig48 rig;
		rig.input.horizontal = -1;
		draw(rig, 0);
		expect(decimal_pos()).to_equal(1);
		rig.reference.draw_string_centred("Hz/V", 20, 18, 20);
		expect(rows_match(rig, 20, 42)).to_be_true();
		cvEngine.cvChannels[0].voltsPerOctave = saved;
	});

	it("draws retrigger phase's free-running value as OFF, and the first detent up lands on 0", _ {
		Rig r;
		menu::bindings::write(nodes::osc0_phase, r.ctx, -1);
		r.open(nodes::osc0_phase);
		deluge::ui::test::Rig48 rig;
		rig.input.horizontal = -1;
		draw(rig, 0);
		expect(decimal_pos()).to_equal(1);
		rig.reference.draw_string_centred(deluge::l10n::get(String::STRING_FOR_OFF), 20, 18, 20);
		expect(rows_match(rig, 20, 42)).to_be_true();

		deluge::ui::test::Rig48 turned;
		turned.input.turn = 1;
		draw(turned, 10);
		expect(menu_scene().state().edit_int).to_equal(0);
	});
});
// clang-format on

DELUGE_SPEC(menu_leaf_values_spec, leaf_cursor, leaf_horizontal, leaf_words)
