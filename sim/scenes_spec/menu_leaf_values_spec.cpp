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
#include "model/project_bridge/patch_cable_words.h"
#include "modulation/arpeggiator_rhythms.h"
#include "modulation/params/param.h"
#include "modulation/sync.h"
#include "processing/engines/cv_engine.h"
#include "storage/flash_storage.h"
#include "synth/modulation/cable.h"
#include "synth/modulation/patch_source.h"

#include "support/forwarding_facts.h"
#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"
#include "spec/ui/support/frames.h"

#include "cppspec.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

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

/// @brief What @p n's full screen names @p value (`Binding::leaf_text`), or empty for its digits.
std::string screen_text(const Node& n, const MenuContext& ctx, int32_t value) {
	if (n.bind.leaf_text == nullptr) {
		return {};
	}
	char buf[32];
	const menu::ValueText vt = n.bind.leaf_text(ctx, value, menu::ValueTextUse::Column, buf);
	return vt.fallback ? std::string{} : std::string{buf, vt.length};
}

void set_song_magnitude(Rig& r, int32_t magnitude) {
	r.song.song->project.transport.inside_world_tick_magnitude = magnitude;
	r.song.song->project.transport.inside_world_tick_magnitude_offset_from_bpm = 0;
}

std::string name_of(const Node& n, const MenuContext& ctx, int32_t value) {
	char buf[32];
	return std::string{menu::sync_level_name(n, ctx, value, buf)};
}

int32_t list_cursor() {
	return menu_scene().state().list[menu_scene().state().depth].cursor;
}

int decimal_pos() {
	return menu_scene().state().decimal_pos;
}

/// @brief Every node the tree numbers, once each.
std::vector<const Node*> every_node() {
	std::vector<const Node*> out;
	const uint16_t count = menu::assign_node_ids(nodes::kAllRoots);
	for (uint16_t id = 1; id <= count; id++) {
		if (const Node* n = menu::node_by_id(id); n != nullptr) {
			out.push_back(n);
		}
	}
	return out;
}

/// @brief How many nodes answer @p has.
template <typename F>
size_t count_where(F has) {
	size_t n = 0;
	for (const Node* node : every_node()) {
		n += has(*node) ? 1 : 0;
	}
	return n;
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

// clang-format off
describe leaf_turns("scenes::menu full-screen turn rules", $ {
	it("turns a sync level five steps a detent while shift is held", _ {
		Rig r;
		FlashStorage::recordQuantizeLevel = 1;
		r.open(nodes::record_quantize);
		deluge::ui::test::Rig48 rig;
		rig.input.turn = 1;
		rig.input.shift = true;
		draw(rig, 0);
		expect(menu_scene().state().edit_int).to_equal(6);
	});

	it("wraps the song's swing interval within 1..N-1 and never reaches OFF", _ {
		Rig r;
		constexpr int32_t kTop = static_cast<int32_t>(NUM_SWING_INTERVALS) - 1;
		menu::bindings::write(nodes::swing_interval, r.ctx, kTop);
		r.open(nodes::swing_interval);
		deluge::ui::test::Rig48 up;
		up.input.turn = 1;
		draw(up, 0);
		expect(menu_scene().state().edit_int).to_equal(1);
		deluge::ui::test::Rig48 down;
		down.input.turn = -1;
		draw(down, 10);
		expect(menu_scene().state().edit_int).to_equal(kTop);
	});

	it("wraps the default swing interval the same way", _ {
		Rig r;
		const uint8_t saved = FlashStorage::defaultSwingInterval;
		FlashStorage::defaultSwingInterval = 1;
		r.open(nodes::default_swing_interval);
		deluge::ui::test::Rig48 down;
		down.input.turn = -1;
		draw(down, 0);
		expect(menu_scene().state().edit_int).to_equal(static_cast<int32_t>(NUM_SWING_INTERVALS) - 1);
		FlashStorage::defaultSwingInterval = saved;
	});

	it("wraps a shift turn of the swing interval as the five-step offset", _ {
		Rig r;
		constexpr int32_t kTop = static_cast<int32_t>(NUM_SWING_INTERVALS) - 1;
		menu::bindings::write(nodes::swing_interval, r.ctx, kTop);
		r.open(nodes::swing_interval);
		deluge::ui::test::Rig48 up;
		up.input.turn = 1;
		up.input.shift = true;
		draw(up, 0);
		expect(menu_scene().state().edit_int).to_equal(wrapSwingIntervalSyncLevel(kTop + 5));
	});

	it("turns a quantized stutter rate five options a click while shift is held, then clamps", _ {
		// `stutter::Rate::selectEncoderAction` clamps `idx + offset` on the scaled offset: 37 is option 3 of 0..4.
		Rig r;
		menu::nodes::set_stutter_direction(r.ctx, 1); // the sound's own stutter
		menu::nodes::set_stutter_quantize(r.ctx, 1);
		menu::bindings::write(nodes::stutter_rate, r.ctx, 37);
		r.open(nodes::stutter_rate);
		deluge::ui::test::Rig48 rig;
		rig.input.turn = -1;
		rig.input.shift = true;
		draw(rig, 0);
		expect(menu_scene().state().edit_int).to_equal(0);
	});

	it("turns the patch cable list five rows a click while shift is held", _ {
		// `PatchCables::selectEncoderAction` clamps `currentValue + offset` on the scaled offset.
		Rig r;
		deluge::scenes::bind::CableList cables;
		for (const PatchSource source :
		     {PatchSource::LFO_GLOBAL_1, PatchSource::LFO_LOCAL_1, PatchSource::ENVELOPE_0, PatchSource::ENVELOPE_1,
		      PatchSource::VELOCITY, PatchSource::NOTE, PatchSource::RANDOM}) {
			cables.items[cables.count++].depth =
			    deluge::bridge::cable_depth(source, deluge::synth::CableTarget::param(deluge::modulation::params::LOCAL_LPF_FREQ));
		}
		const deluge::scenes::spec::ForwardingFacts facts{*r.ctx.facts, {.cables = cables}};
		r.ctx.facts = &facts;
		r.open(nodes::patch_cables);
		deluge::ui::test::Rig48 rig;
		rig.input.turn = 1;
		rig.input.shift = true;
		draw(rig, 0);
		expect(list_cursor()).to_equal(5);
	});

	it("turns the source list five rows a click while shift is held", _ {
		// `SourceSelection::selectEncoderAction` steps `offset` times over the allowed sources.
		Rig r;
		r.open(nodes::source_selection_regular);
		deluge::ui::test::Rig48 rig;
		rig.input.turn = 1;
		rig.input.shift = true;
		draw(rig, 0);
		expect(list_cursor()).to_equal(5);
	});

	it("wraps a MIDI bank, sub-bank and program round 0..128, 128 being none", _ {
		for (const Node* n : {&nodes::midi_bank, &nodes::midi_sub_bank, &nodes::midi_pgm}) {
			expect(n->bind.turn != nullptr).to_be_true();
			if (n->bind.turn == nullptr) {
				continue;
			}
			Rig r;
			expect(n->bind.turn(r.ctx, 128, 1).value_or(-99)).to_equal(0);
			expect(n->bind.turn(r.ctx, 0, -1).value_or(-99)).to_equal(128);
			expect(n->bind.turn(r.ctx, 126, 5).value_or(-99)).to_equal(2);
			expect(n->bind.turn(r.ctx, 40, 1).value_or(-99)).to_equal(41);
		}
	});
});
// clang-format on

// clang-format off
describe leaf_texts("scenes::menu full-screen value texts", $ {
	it("names a MIDI preset's 128 NONE on the full screen, and leaves the rest as digits", _ {
		Rig r;
		for (const Node* n : {&nodes::midi_bank, &nodes::midi_sub_bank, &nodes::midi_pgm}) {
			expect(screen_text(*n, r.ctx, 128)).to_equal(std::string{deluge::l10n::get(String::STRING_FOR_NONE)});
			expect(screen_text(*n, r.ctx, 7)).to_equal(std::string{});
		}
	});

	it("names the arpeggiator's rhythm 'index: pattern' in the digits' place, as Rhythm::drawInteger does", _ {
		Rig r;
		std::string want = std::to_string(3) + ": " + arpRhythmPatternNames[3];
		want = want.substr(0, 11); // its 12-byte buffer
		expect(screen_text(nodes::arp_rhythm, r.ctx, 3)).to_equal(want);
		expect(nodes::arp_rhythm.bind.style.label_in_digits).to_be_true();
	});

	it("names a sound's MIDI output channel 0 Off, from l10n as OutputMidiChannel::drawInteger does", _ {
		Rig r;
		expect(screen_text(nodes::output_midi_channel, r.ctx, 0))
		    .to_equal(std::string{deluge::l10n::get(String::STRING_FOR_OFF)});
		expect(screen_text(nodes::output_midi_channel, r.ctx, 5)).to_equal(std::string{});
	});

	it("spells the hold time in milliseconds, and its top as 1 SEC", _ {
		Rig r;
		expect(screen_text(nodes::default_hold_time, r.ctx, 1)).to_equal(std::string{"50 MS"});
		expect(screen_text(nodes::default_hold_time, r.ctx, 19)).to_equal(std::string{"950 MS"});
		expect(screen_text(nodes::default_hold_time, r.ctx, 20)).to_equal(std::string{"1 SEC"});
	});

	it("draws the hold time's text in the huge face where the digits go", _ {
		Rig r;
		const auto saved = FlashStorage::defaultHoldTime;
		FlashStorage::defaultHoldTime = 20;
		r.open(nodes::default_hold_time);
		deluge::ui::test::Rig48 rig;
		draw(rig, 0);
		rig.reference.draw_string_centred("1 SEC", 18 + 5, 18, 20);
		expect(rows_match(rig, 23, 42)).to_be_true();
		FlashStorage::defaultHoldTime = saved;
	});

	it("appends MIN to the screensaver timeout", _ {
		Rig r;
		const auto saved = FlashStorage::screensaverTimeoutMinutes;
		FlashStorage::screensaverTimeoutMinutes = 5;
		r.open(nodes::screensaver_timeout);
		deluge::ui::test::Rig48 rig;
		draw(rig, 0);
		rig.reference.draw_string_centred("5 MIN", 18 + 5, 18, 20);
		expect(rows_match(rig, 23, 42)).to_be_true();
		FlashStorage::screensaverTimeoutMinutes = saved;
	});

	it("names the reverb ducking's AUTO on its full screen", _ {
		Rig r;
		expect(screen_text(nodes::reverb_sidechain_volume, r.ctx, -1))
		    .to_equal(std::string{deluge::l10n::get(String::STRING_FOR_AUTO)});
		expect(screen_text(nodes::reverb_sidechain_volume, r.ctx, 20)).to_equal(std::string{});
	});

	it("draws the default resolution as one number, 96 << value, over seven values", _ {
		Rig r;
		expect(nodes::default_magnitude.kind == menu::Kind::Int).to_be_true();
		expect(menu::bindings::range(nodes::default_magnitude, r.ctx).min).to_equal(0);
		expect(menu::bindings::range(nodes::default_magnitude, r.ctx).max).to_equal(6);
		expect(screen_text(nodes::default_magnitude, r.ctx, 0)).to_equal(std::string{"96"});
		expect(screen_text(nodes::default_magnitude, r.ctx, 2)).to_equal(std::string{"384"});
		expect(screen_text(nodes::default_magnitude, r.ctx, 6)).to_equal(std::string{"6144"});
	});

	it("names a MIDI or CV arpeggiator's rhythm by its pattern", _ {
		Rig r;
		expect(screen_text(nodes::arp_rhythm_midi, r.ctx, 3)).to_equal(std::string{arpRhythmPatternNames[3]});
	});

	it("spells a note's probability with (L) when it latches, its fill and its iterance, on both editors", _ {
		Rig r;
		for (const Node* n : {&nodes::note_editor_probability, &nodes::note_row_probability}) {
			expect(screen_text(*n, r.ctx, 7)).to_equal(std::string{"35%"});
			expect(screen_text(*n, r.ctx, 7 | 128)).to_equal(std::string{"35% (L)"});
		}
		for (const Node* n : {&nodes::note_fill, &nodes::note_row_fill}) {
			expect(screen_text(*n, r.ctx, FillMode::OFF)).to_equal(std::string{"OFF"});
			expect(screen_text(*n, r.ctx, FillMode::FILL)).to_equal(std::string{"FILL"});
			expect(screen_text(*n, r.ctx, FillMode::NOT_FILL)).to_equal(std::string{"NOT FILL"});
		}
		for (const Node* n : {&nodes::note_iterance, &nodes::note_row_iterance}) {
			expect(screen_text(*n, r.ctx, kDefaultIterancePreset)).to_equal(std::string{"OFF"});
			expect(screen_text(*n, r.ctx, kCustomIterancePreset)).to_equal(std::string{"CUSTOM"});
		}
	});
});
// clang-format on

// clang-format off
describe leaf_sync_names("scenes::menu sync-level names", $ {
	it("names the song's swing interval against the song", _ {
		Rig r;
		set_song_magnitude(r, 3);
		expect(name_of(nodes::swing_interval, r.ctx, 5)).to_equal(std::string{"16th-notes"});
	});

	it("names the default swing interval against the default resolution", _ {
		Rig r;
		set_song_magnitude(r, 3);
		const auto saved = FlashStorage::defaultMagnitude;
		FlashStorage::defaultMagnitude = 1;
		expect(name_of(nodes::default_swing_interval, r.ctx, 5)).to_equal(std::string{"4th-notes"});
		FlashStorage::defaultMagnitude = saved;
	});

	it("names record quantize as RelativeToSong does, 3 - value, whatever the song", _ {
		Rig r;
		set_song_magnitude(r, 3);
		expect(name_of(nodes::record_quantize, r.ctx, 7)).to_equal(std::string{"16th-notes"});
		expect(name_of(nodes::record_quantize, r.ctx, 3)).to_equal(std::string{"1-bar"});
		expect(name_of(nodes::record_quantize, r.ctx, 1)).to_equal(std::string{"4-bar"});
		expect(name_of(nodes::record_quantize, r.ctx, 0)).to_equal(std::string{deluge::l10n::get(String::STRING_FOR_OFF)});
	});

	it("draws record quantize's screen the same whatever the song's tick magnitude", _ {
		Rig r;
		const auto saved = FlashStorage::recordQuantizeLevel;
		FlashStorage::recordQuantizeLevel = 7;
		set_song_magnitude(r, 1);
		r.open(nodes::record_quantize);
		deluge::ui::test::Rig48 at1;
		draw(at1, 0);
		set_song_magnitude(r, 3);
		deluge::ui::test::Rig48 at3;
		draw(at3, 10);
		expect(at3.pbm()).to_equal(at1.pbm());
		FlashStorage::recordQuantizeLevel = saved;
	});

	it("notifies a sync level by the same name", _ {
		Rig r;
		set_song_magnitude(r, 3);
		char buf[32];
		expect(std::string{menu::column_notification(nodes::record_quantize, r.ctx, 7, buf)})
		    .to_equal(std::string{"16th-notes"});
	});
});
// clang-format on

// clang-format off
describe tree_rules("scenes::menu every value item's turn and draw rule", $ {
	it("gives its own turn to exactly the items whose legacy selectEncoderAction is their own", _ {
		expect(count_where([](const Node& n) { return n.bind.turn == &nodes::turn_follow_channel; })).to_equal(size_t(19));
		expect(count_where([](const Node& n) { return n.bind.turn == &nodes::stutter_rate_turn; })).to_equal(size_t(1));
		expect(count_where([](const Node& n) { return n.bind.turn == &nodes::swing_interval_turn; })).to_equal(size_t(2));
		expect(count_where([](const Node& n) { return n.bind.turn == &nodes::midi_preset_turn; })).to_equal(size_t(3));
		expect(count_where([](const Node& n) { return n.bind.turn != nullptr; })).to_equal(size_t(25));
	});

	it("starts every Decimal's cursor on 2 but the six legacy overrides to 1", _ {
		expect(count_where([](const Node& n) { return n.kind == menu::Kind::Decimal && n.bind.default_edit_pos == 1; }))
		    .to_equal(size_t(6));
		expect(count_where([](const Node& n) {
			return n.kind == menu::Kind::Decimal && n.bind.default_edit_pos != 1 && n.bind.default_edit_pos != 2;
		})).to_equal(size_t(0));
	});

	it("names special values on exactly the Decimals main draws a word for", _ {
		expect(count_where([](const Node& n) { return n.bind.decimal_word == &nodes::cv_volts_word; })).to_equal(size_t(1));
		expect(count_where([](const Node& n) { return n.bind.decimal_word == &nodes::retrigger_phase_text; })).to_equal(size_t(4));
		expect(count_where([](const Node& n) { return n.bind.decimal_word != nullptr; })).to_equal(size_t(5));
		expect(count_where([](const Node& n) { return n.bind.decimal_restart_from_negative; })).to_equal(size_t(4));
	});

	it("draws its own full-screen text on exactly the items whose legacy draw is their own", _ {
		expect(count_where([](const Node& n) { return n.bind.leaf_text != nullptr; })).to_equal(size_t(21));
		expect(count_where([](const Node& n) { return n.bind.leaf_draw != nullptr; })).to_equal(size_t(19));
		expect(count_where([](const Node& n) { return !n.bind.style.unit.empty(); })).to_equal(size_t(1));
		expect(count_where([](const Node& n) { return n.bind.style.label_in_digits; })).to_equal(size_t(1));
		expect(count_where([](const Node& n) { return n.bind.sync_name_magnitude != nullptr; })).to_equal(size_t(2));
		expect(count_where([](const Node& n) { return n.bind.wrap_in_horizontal; })).to_equal(size_t(2));
		expect(count_where([](const Node& n) { return n.bind.reverse_in_horizontal != nullptr; })).to_equal(size_t(5));
	});

	it("clamps every plain integer and sync level at its top, and turns it five steps under shift", _ {
		// `Integer::selectEncoderAction` and `Enumeration::selectEncoderAction` add and clamp; `SoundEditor` hands
		// either five times the offset while SHIFT is held. Only the items with a turn of their own differ.
		size_t walked = 0;
		for (const Node* n : every_node()) {
			if ((n->kind != menu::Kind::Int && n->kind != menu::Kind::SyncLevel) || n->bind.turn != nullptr ||
			    n->bind.leaf_draw != nullptr) {
				continue;
			}
			Rig r;
			const menu::IntRange range = menu::bindings::range(*n, r.ctx);
			menu::bindings::write(*n, r.ctx, range.max);
			if (menu::bindings::read(*n, r.ctx) != range.max) {
				continue;
			}
			r.open(*n);
			if (menu_scene().state().edit_int != range.max) {
				continue;
			}
			deluge::ui::test::Rig48 up;
			up.input.turn = 1;
			draw(up, 0);
			expect(menu_scene().state().edit_int).to_equal(range.max);
			deluge::ui::test::Rig48 down;
			down.input.turn = -1;
			down.input.shift = true;
			draw(down, 10);
			const int32_t step = n->kind == menu::Kind::Int && range.step != 0 ? range.step : 1;
			expect(menu_scene().state().edit_int).to_equal(std::max(range.min, range.max - 5 * step));
			walked++;
		}
		expect(walked).to_equal(size_t(137));
	});

	it("starts each Decimal's cursor where main does, whichever way it is opened", _ {
		size_t walked = 0;
		for (const Node* n : every_node()) {
			if (n->kind != menu::Kind::Decimal) {
				continue;
			}
			Rig r;
			r.open(*n);
			// `cv::Volts`, `gate::OffTime` and the four `osc::RetriggerPhase`s override it to 1.
			const bool overridden =
			    n == &nodes::cv_volts || n == &nodes::gate_off_time || n == &nodes::osc0_phase ||
			    n == &nodes::osc1_phase || n == &nodes::modulator0_phase_node || n == &nodes::modulator1_phase_node;
			expect(decimal_pos()).to_equal(overridden ? 1 : 2);
			walked++;
		}
		expect(walked).to_equal(size_t(18));
	});
});
// clang-format on

DELUGE_SPEC(menu_leaf_values_spec, tree_rules, leaf_cursor, leaf_horizontal, leaf_words, leaf_turns, leaf_texts,
            leaf_sync_names)
