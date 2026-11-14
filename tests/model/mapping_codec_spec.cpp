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
/// @brief An owner's learned knobs as the codec reads them from a file: a sound's old volume names land on the volume
///        the runtime turns, and a knob naming a cable's pre-3.2 "range" is left out.

#include "deluge-project/mapping_codec.h"
#include "deluge-project/mapping_table.h"
#include "effects/global_effectable/naming.h"
#include "effects/global_effectable/params.h"
#include "instruments/deluge/cables.h"
#include "instruments/deluge/naming.h"
#include "instruments/deluge/params.h"
#include "serialization/parse_xml.h"

#include <cppspec.hpp>
#include <string_view>
#include <vector>

using namespace deluge::project;
using deluge::instrument::PatchSource;
using deluge::serialization::Value;
namespace ip = deluge::instrument::param;

namespace {

constexpr ControlsAddress kSound{ControlsAddress::Kind::OUTPUT, 4};

/// The `name` element of @p xml.
Value element(std::string_view xml, std::string_view name) {
	return *deluge::serialization::parseXml(xml)->find(name);
}

/// The params, in order, of the knobs @p xml (a `sound` element) holds, as a sound's owner decodes them.
std::vector<uint32_t> sound_knob_params(std::string_view xml) {
	const Value sound = element(xml, "sound");
	std::vector<uint32_t> params;
	const Value* midiKnobs = sound.find("midiKnobs");
	if (midiKnobs == nullptr) {
		return params;
	}
	for (const Mapping& knob : decode_knobs(*midiKnobs, kSound, deluge::instrument::kNaming)) {
		params.push_back(std::get<OwnerParamTarget>(knob.target).param.id.value);
	}
	return params;
}

/// The id of the depth of a cable from @p source to @p param.
uint32_t depth(PatchSource source, deluge::plugin::ParamId param) {
	return deluge::instrument::cable_depth_id(source, param)->value;
}

} // namespace

// clang-format off
describe mapping_codec("an owner's learned knobs as a file holds them", ${
	it("points a sound's knob on a volume param at the volume after the effects", _ {
		const std::vector<uint32_t> params = sound_knob_params(R"(<sound><midiKnobs>
			<midiKnob channel="0" ccNumber="1" relative="0" controlsParam="volume" />
			<midiKnob channel="0" ccNumber="2" relative="0" controlsParam="volumePostReverbSend" />
			<midiKnob channel="0" ccNumber="3" relative="0" controlsParam="volumePostFX" />
			</midiKnobs></sound>)");
		expect(params).to_equal(std::vector<uint32_t>{ip::kVolumePostFX.value, ip::kVolumePostFX.value,
		                                              ip::kVolumePostFX.value});
	});

	it("points a sound's knob on a sidechain cable's volume depth at the cable to the volume after the reverb send", _ {
		const std::vector<uint32_t> params = sound_knob_params(R"(<sound><midiKnobs>
			<midiKnob channel="0" ccNumber="1" relative="0" controlsParam="volume" patchAmountFromSource="compressor" />
			<midiKnob channel="0" ccNumber="2" relative="0" controlsParam="volumePostReverbSend" patchAmountFromSource="compressor" />
			<midiKnob channel="0" ccNumber="3" relative="0" controlsParam="volumePostFX" patchAmountFromSource="compressor" />
			</midiKnobs></sound>)");
		const uint32_t sidechain = depth(PatchSource::SIDECHAIN, ip::kVolumePostReverbSend);
		expect(params).to_equal(std::vector<uint32_t>{sidechain, sidechain, sidechain});
	});

	it("points a sound's knob on another cable's volume depth at the cable to the voice volume", _ {
		const std::vector<uint32_t> params = sound_knob_params(R"(<sound><midiKnobs>
			<midiKnob channel="0" ccNumber="1" relative="0" controlsParam="volumePostFX" patchAmountFromSource="lfo1" />
			<midiKnob channel="0" ccNumber="2" relative="0" controlsParam="volumePostReverbSend" patchAmountFromSource="velocity" />
			<midiKnob channel="0" ccNumber="3" relative="0" controlsParam="volumePostFX" patchAmountFromSource="lfo1" patchAmountFromSecondSource="velocity" />
			<midiKnob channel="0" ccNumber="4" relative="0" controlsParam="volume" patchAmountFromSource="compressor" patchAmountFromSecondSource="lfo1" />
			</midiKnobs></sound>)");
		// A depth's depth follows the source nearest the param, as the runtime's fix-up reads it.
		expect(params).to_equal(std::vector<uint32_t>{
		    depth(PatchSource::LFO_GLOBAL_1, ip::kVolume), depth(PatchSource::VELOCITY, ip::kVolume),
		    deluge::instrument::cable_depth_id(PatchSource::VELOCITY,
		                                       *deluge::instrument::cable_depth_id(PatchSource::LFO_GLOBAL_1, ip::kVolume))
		        ->value,
		    deluge::instrument::cable_depth_id(
		        PatchSource::LFO_GLOBAL_1, *deluge::instrument::cable_depth_id(PatchSource::SIDECHAIN, ip::kVolumePostReverbSend))
		        ->value});
	});

	it("leaves a sound's knob on any other param as the file names it", _ {
		const std::vector<uint32_t> params = sound_knob_params(R"(<sound><midiKnobs>
			<midiKnob channel="0" ccNumber="1" relative="0" controlsParam="pan" />
			<midiKnob channel="0" ccNumber="2" relative="0" controlsParam="pitch" patchAmountFromSource="compressor" />
			</midiKnobs></sound>)");
		expect(params).to_equal(std::vector<uint32_t>{ip::kPan.value, depth(PatchSource::SIDECHAIN, ip::kPitch)});
	});

	it("leaves out a sound's knob on a cable's range, and keeps the knobs around it", _ {
		const std::vector<uint32_t> params = sound_knob_params(R"(<sound><midiKnobs>
			<midiKnob channel="0" ccNumber="1" relative="0" controlsParam="pan" />
			<midiKnob channel="0" ccNumber="2" relative="0" controlsParam="range" />
			<midiKnob channel="0" ccNumber="3" relative="0" controlsParam="range" patchAmountFromSource="lfo1" />
			<midiKnob channel="0" ccNumber="4" relative="0" controlsParam="volumePostFX" />
			</midiKnobs></sound>)");
		expect(params).to_equal(std::vector<uint32_t>{ip::kPan.value, ip::kVolumePostFX.value});
	});

	it("leaves out a kit's knob on a cable's range, and leaves its volume as the file names it", _ {
		const Value kit = element(R"(<kit><midiKnobs>
			<midiKnob channel="0" ccNumber="1" relative="0" controlsParam="range" />
			<midiKnob channel="0" ccNumber="2" relative="0" controlsParam="volume" />
			</midiKnobs></kit>)", "kit");
		const std::vector<Mapping> knobs = decode_knobs(*kit.find("midiKnobs"), ControlsAddress{ControlsAddress::Kind::OUTPUT, 2},
		                                                deluge::global_effectable::kNaming);
		expect(knobs.size()).to_equal(size_t{1});
		expect(knobs.empty() ? 0U : std::get<OwnerParamTarget>(knobs[0].target).param.id.value)
		    .to_equal(deluge::global_effectable::param::kVolume.value);
	});

	it("reads each knob's source and owner, and a song's table takes the same knobs", _ {
		const Value sound = element(R"(<sound><midiKnobs>
			<midiKnob mpeZone="upper" ccNumber="74" relative="1" controlsParam="volume"><device port="din" /></midiKnob>
			</midiKnobs></sound>)", "sound");
		const std::vector<Mapping> knobs = decode_knobs(*sound.find("midiKnobs"), kSound, deluge::instrument::kNaming);
		expect(knobs.size()).to_equal(size_t{1});
		if (knobs.size() != 1) {
			return;
		}
		expect(static_cast<int>(knobs[0].source.channel_or_zone)).to_equal(17);
		expect(static_cast<int>(knobs[0].source.note_or_cc)).to_equal(74);
		expect(knobs[0].relative).to_be_true();
		expect(knobs[0].source.device.has_value() && knobs[0].source.device->port == MidiDeviceRef::Port::DIN)
		    .to_be_true();
		expect(knobs[0].target).to_equal(MappingTarget{OwnerParamTarget{kSound, engine_target(ip::kVolumePostFX.value)}});
		MappingTable table;
		decode_mappings(sound, kSound, table, &deluge::instrument::kNaming);
		expect(table.params_of(kSound) == knobs).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(mapping_codec)
