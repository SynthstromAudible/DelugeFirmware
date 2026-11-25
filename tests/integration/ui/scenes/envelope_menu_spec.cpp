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
/// @brief The envelope pages' mode, curve and follow items, and what the modes do to the page: the segments renamed and
/// hidden, as main's `envelope::Segment`, `Mode`, `Curve` and `Follow` (`gui/menu_item/envelope/` there).

#include "gui/l10n/l10n.h"
#include "gui/l10n/param_names.h"
#include "instruments/classic/params.h"
#include "instruments/classic/state_access.h"
#include "processing/sound/sound.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/present.h"

#include "support/descriptor_fixtures.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace spec = deluge::ui::scenes::spec;
namespace menu = deluge::ui::scenes::menu;
namespace nodes = deluge::ui::scenes::menu::nodes;
using deluge::plugin::ParamKind;
using deluge::synth::EnvelopeMode;
using menu::MenuContext;
using menu::Node;

namespace {

struct SynthSong {
	spec::SongFixture song = spec::make_synth_song();
	SynthSong() {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		song.song->setCurrentClip(song.clip);
	}
	[[nodiscard]] MenuContext ctx() const {
		return spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
	}
	void set(std::string_view path, std::string_view word) const {
		sound_setting(*spec::menu_engine(ctx(), *song.song).sound, path, word);
	}
	[[nodiscard]] deluge::synth::EnvelopeState envelope(size_t e) const {
		return deluge::instruments::classic::envelope(spec::menu_engine(ctx(), *song.song).sound->state(), e);
	}
};

std::string name(const Node& node, const MenuContext& ctx) {
	menu::NodeText scratch;
	return std::string{menu::display_text(node, menu::ValueTextUse::Name, ctx, scratch)};
}
std::string title(const Node& node, const MenuContext& ctx) {
	menu::NodeText scratch;
	return std::string{menu::display_text(node, menu::ValueTextUse::Title, ctx, scratch)};
}
/// The names of the items a list of @p page shows.
std::vector<std::string> listed(const Node& page, const MenuContext& ctx) {
	std::array<const Node*, menu::kMaxDynamicChildren> kids{};
	const size_t n = menu::bindings::listed_children(page, ctx, kids, menu::bindings::ListFor::Rows);
	std::vector<std::string> out;
	for (size_t i = 0; i < n; ++i) {
		out.push_back(name(*kids[i], ctx));
	}
	return out;
}
using Names = std::vector<std::string>;

} // namespace

// clang-format off
describe envelope_menu("an envelope's page", ${
	it("lists main's items for each mode, by the names the mode gives them", _ {
		const SynthSong s;
		expect(listed(nodes::env2, s.ctx())).to_equal(Names{"Attack", "Decay", "Sustain", "Release", "Mode", "Curve"});
		s.set("envelope2Settings/mode", "ahd");
		expect(listed(nodes::env2, s.ctx())).to_equal(Names{"Attack", "Hold", "Decay", "Mode", "Curve"});
		s.set("envelope2Settings/mode", "ar");
		expect(listed(nodes::env2, s.ctx())).to_equal(Names{"Attack", "Level", "Release", "Mode", "Curve"});
		s.set("envelope2Settings/mode", "mpe");
		expect(listed(nodes::env2, s.ctx())).to_equal(Names{"Attack", "Level", "Release", "Mode", "Curve", "Follow"});
		// The other envelopes keep their own mode's
		expect(listed(nodes::env1, s.ctx())).to_equal(Names{"Attack", "Decay", "Sustain", "Release", "Mode", "Curve"});
	});

	it("titles each segment for the mode, with the envelope's number", _ {
		const SynthSong s;
		expect(title(nodes::env3_decay, s.ctx())).to_equal(std::string{"Env3 decay"});
		expect(title(nodes::env3_mode, s.ctx())).to_equal(std::string{"Env3 mode"});
		expect(title(nodes::env3_curve, s.ctx())).to_equal(std::string{"Env3 curve"});
		expect(title(nodes::env3_follow, s.ctx())).to_equal(std::string{"Env3 follow"});
		s.set("envelope3Settings/mode", "ahd");
		expect(title(nodes::env3_decay, s.ctx())).to_equal(std::string{"Env3 hold"});
		expect(title(nodes::env3_release, s.ctx())).to_equal(std::string{"Env3 decay"});
		s.set("envelope3Settings/mode", "mpe");
		expect(title(nodes::env3_sustain, s.ctx())).to_equal(std::string{"Env3 level"});
		expect(title(nodes::env3_release, s.ctx())).to_equal(std::string{"Env3 release"});
	});

	it("keeps every segment's column on the horizontal page, whatever the mode", _ {
		const SynthSong s;
		s.set("envelope1Settings/mode", "ahd");
		std::array<const Node*, menu::kMaxDynamicChildren> kids{};
		const MenuContext ctx = s.ctx();
		const size_t n = menu::bindings::listed_children(nodes::env1, ctx, kids, menu::bindings::ListFor::Columns);
		expect(n).to_equal(size_t{7});
		expect(kids[2]).to_equal(&nodes::env1_sustain);
		expect(kids[4]).to_equal(&nodes::env1_mode);
	});

	it("reads and writes the mode, the curve and the follow input", _ {
		const SynthSong s;
		expect(menu::bindings::read(nodes::env1_curve, s.ctx())).to_equal(32);
		menu::bindings::write(nodes::env1_mode, s.ctx(), 3);
		menu::bindings::write(nodes::env1_curve, s.ctx(), -50);
		menu::bindings::write(nodes::env1_follow, s.ctx(), 1);
		expect(s.envelope(0)).to_equal(deluge::synth::EnvelopeState{
		    .mode = EnvelopeMode::MPE, .follow = deluge::synth::EnvelopeFollow::Y, .curve = -50});
		const menu::IntRange range = menu::bindings::range(nodes::env1_curve, s.ctx());
		expect(range.min).to_equal(-50);
		expect(range.max).to_equal(50);
	});

	it("draws the graph over the segments' page alone", _ {
		expect(nodes::env1.page_draw).not_().to_be_null();
		expect(static_cast<int>(nodes::env1.page_draw_columns)).to_equal(4);
	});
});

describe envelope_param_names("an envelope param's name", ${
	it("follows the envelope's mode", _ {
		namespace classic = deluge::instruments::classic;
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		const auto at = [](uint32_t param, EnvelopeMode mode, bool brief) {
			deluge::l10n::EnvelopeModes modes{};
			modes[1] = mode;
			const auto got = deluge::l10n::envelope_param_name_at(ParamKind::PATCHED, param, modes, brief);
			return got ? std::string{*got} : std::string{"(usual)"};
		};
		const uint32_t decay = uint32_t{classic::runtime_index(classic::param::kEnv1Decay)} + 1;
		const uint32_t sustain = uint32_t{classic::runtime_index(classic::param::kEnv1Sustain)} + 1;
		const uint32_t release = uint32_t{classic::runtime_index(classic::param::kEnv1Release)} + 1;
		expect(at(decay, EnvelopeMode::ADSR, false)).to_equal(std::string{"(usual)"});
		expect(at(decay, EnvelopeMode::AHD, false)).to_equal(std::string{"Env2 hold"});
		expect(at(release, EnvelopeMode::AHD, false))
		    .to_equal(std::string{deluge::l10n::param_name_at(ParamKind::PATCHED, decay)});
		expect(at(release, EnvelopeMode::AHD, true)).to_equal(std::string{"Env2 decay"});
		expect(at(sustain, EnvelopeMode::AHD, false)).to_equal(std::string{"(usual)"});
		for (const EnvelopeMode mode : {EnvelopeMode::AR, EnvelopeMode::MPE}) {
			expect(at(sustain, mode, false)).to_equal(std::string{"Env2 level"});
			expect(at(decay, mode, false)).to_equal(std::string{"(usual)"});
		}
		// Envelope 1 is still ADSR
		expect(at(classic::runtime_index(classic::param::kEnv1Decay), EnvelopeMode::AHD, false)).to_equal(std::string{"(usual)"});
	});
});
// clang-format on

DELUGE_SPEC(envelope_menu_spec, envelope_menu, envelope_param_names)
