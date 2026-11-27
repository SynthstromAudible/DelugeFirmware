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

// The param display names, keyed by the neutral kinds and ids: the words the knob popups, the patch-cable menu and the
// knob-push labels show.

#include "gui/l10n/l10n.h"
#include "gui/l10n/param_names.h"
#include "host/chains/params.h"
#include "instruments/classic/params.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <string>
#include <string_view>

using namespace deluge;
using plugin::ParamKind;

namespace {
void select_language() {
	l10n::chosenLanguage = &l10n::built_in::english;
}

std::string text(std::string_view view) {
	return std::string{view};
}
} // namespace

// clang-format off
describe param_names_cases("param display names", ${
	it("names a patched param in the current language", _ {
		select_language();
		expect(text(l10n::param_name(ParamKind::PATCHED, instruments::classic::param::kLpfFrequency))).to_equal("LPF frequency");
	});
	it("names an unpatched sound param, the shared ones and portamento", _ {
		select_language();
		expect(text(l10n::param_name(ParamKind::UNPATCHED_SOUND, instruments::classic::param::kPortamento))).to_equal("Portamento");
		expect(text(l10n::param_name(ParamKind::UNPATCHED_SOUND, instruments::classic::param::kStutterRate)))
		    .to_equal(text(l10n::getView(l10n::String::STRING_FOR_STUTTER_RATE)));
	});
	it("names a global effectable's param by the host chain's ids", _ {
		select_language();
		expect(text(l10n::param_name(ParamKind::UNPATCHED_GLOBAL, host::chains::param::kTempo))).to_equal("Tempo");
	});
	it("names the expression axes", _ {
		select_language();
		expect(text(l10n::param_name(ParamKind::EXPRESSION, instruments::classic::kExpressionPitchBend))).to_equal("Pitch bend");
	});
	it("answers None for an id the kind does not hold and for a kind with no names", _ {
		select_language();
		expect(text(l10n::param_name(ParamKind::PATCHED, instruments::classic::param::kStutterRate))).to_equal("None");
		expect(text(l10n::param_name(ParamKind::MIDI, plugin::ParamId{3}))).to_equal("None");
		expect(text(l10n::param_name_at(ParamKind::UNPATCHED_SOUND, 255))).to_equal("None");
		expect(text(l10n::param_name_at(ParamKind::PATCHED, static_cast<uint32_t>(-1)))).to_equal("None");
	});
	it("names a runtime index as the id it numbers", _ {
		select_language();
		expect(text(l10n::param_name_at(ParamKind::UNPATCHED_SOUND, 11))).to_equal("Portamento");
		expect(text(l10n::param_name_at(ParamKind::PATCHED, 24))).to_equal("LPF frequency");
	});
	it("renames the morph params after what the ladder filters make them", _ {
		select_language();
		using effects::FilterMode;
		expect(text(l10n::param_name(ParamKind::PATCHED, instruments::classic::param::kLpfMorph, FilterMode::TRANSISTOR_24DB,
		                             FilterMode::OFF))).to_equal("LPF drive");
		expect(text(l10n::param_name(ParamKind::UNPATCHED_GLOBAL, host::chains::param::kHpfMorph, FilterMode::OFF,
		                             FilterMode::HPLADDER))).to_equal("HPF FM");
		expect(text(l10n::param_name(ParamKind::PATCHED, instruments::classic::param::kLpfMorph, FilterMode::SVF_BAND,
		                             FilterMode::OFF))).to_equal(text(l10n::param_name(ParamKind::PATCHED, instruments::classic::param::kLpfMorph)));
	});
	it("gives a patched param's short name, keeping the table's own wording", _ {
		expect(text(l10n::patched_param_short_name(instruments::classic::param::kOscAVolume))).to_equal("Osc1 level");
		expect(text(l10n::patched_param_short_name(instruments::classic::param::kModulator2Pitch))).to_equal("Mod1 pitch");
		expect(text(l10n::patched_param_short_name(instruments::classic::param::kLpfMorph, effects::FilterMode::TRANSISTOR_12DB,
		                                           effects::FilterMode::OFF))).to_equal("LPF Drive");
		expect(text(l10n::patched_param_short_name_at(54))).to_equal("Arp. rate");
	});
	it("gives no short name to an id that is not a patched param", _ {
		expect(l10n::patched_param_short_name(instruments::classic::param::kStutterRate).empty()).to_be_true();
		expect(l10n::patched_param_short_name_at(55).empty()).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(param_names_spec, param_names_cases)
