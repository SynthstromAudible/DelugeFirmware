#include "controls_test_engines.h"
#include "deluge-project/controls_codec.h"
#include "engines/deluge/cables.h"
#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"
#include "serialization/parse_xml.h"

#include <cppspec.hpp>

using namespace deluge::project;
using deluge::engine::ParamId;
using deluge::serialization::Value;
namespace ip = deluge::instrument::param;

namespace {
Value element(std::string_view xml, std::string_view name) {
	return *deluge::serialization::parseXml(xml)->find(name);
}
} // namespace

// clang-format off
describe controls("a sound's mod knobs and learned MIDI knobs as model records", ${
	it("reads mod knobs by button and knob, a cable's depth included", _ {
		Value sound = element(R"(<sound><modKnobs>
			<modKnob controlsParam="pan" /><modKnob controlsParam="volumePostFX" />
			<modKnob controlsParam="pitch" patchAmountFromSource="lfo1" /><modKnob controlsParam="nosuchparam" />
			<modKnob controlsParam="lfo1Rate" /></modKnobs></sound>)", "sound");
		auto controls = decode_controls(sound, deluge::instrument::kNaming);
		expect(controls.size()).to_equal(size_t{4});
		expect(controls[2].target.value)
		    .to_equal(deluge::instrument::cable_depth_id(deluge::instrument::PatchSource::LFO_GLOBAL_1, ip::kPitch)->value);
		const auto& lfo = std::get<ModKnobControl>(controls[2].control);
		expect(static_cast<int>(lfo.button)).to_equal(1);
		expect(static_cast<int>(lfo.knob)).to_equal(0);
		// the unnamed fourth entry is skipped but still takes (1, 1); lfo1Rate is button 2, knob 0
		const auto& rate = std::get<ModKnobControl>(controls[3].control);
		expect(static_cast<int>(rate.button)).to_equal(2);
		expect(static_cast<int>(rate.knob)).to_equal(0);
	});

	it("points a mod knob's volume target where the runtime does", _ {
		Value sound = element(R"(<sound><modKnobs><modKnob controlsParam="volume" /></modKnobs></sound>)", "sound");
		expect(decode_controls(sound, deluge::instrument::kNaming)[0].target.value).to_equal(ip::kVolumePostFX.value);
	});

	it("reads a learned knob with its device, and writes it back as read", _ {
		Value kit = element(R"(<kit><midiKnobs><midiKnob channel="2" ccNumber="74" relative="1"
			controlsParam="lpfFrequency"><device port="din" /></midiKnob></midiKnobs></kit>)", "kit");
		auto controls = decode_controls(kit, deluge::global_effectable::kNaming);
		expect(controls.size()).to_equal(size_t{1});
		const auto& learned = std::get<MidiInputControl>(controls[0].control);
		expect(static_cast<int>(learned.input.channel_or_zone)).to_equal(2);
		expect(static_cast<int>(learned.input.note_or_cc)).to_equal(74);
		expect(learned.relative).to_be_true();
		expect(learned.input.device).to_have_value();
		expect(controls[0].target.value).to_equal(deluge::global_effectable::param::kLpfFrequency.value);
		Value again = Value::makeMap();
		encode_controls(controls, deluge::global_effectable::kNaming, again);
		const auto reread = decode_controls(again, deluge::global_effectable::kNaming);
		expect(reread.size()).to_equal(size_t{1});
		const auto& relearned = std::get<MidiInputControl>(reread[0].control);
		expect(static_cast<int>(relearned.input.note_or_cc)).to_equal(74);
		expect(relearned.relative).to_be_true();
		expect(relearned.input.device).to_have_value();
		expect(reread[0].target.value).to_equal(controls[0].target.value);
	});

	it("reads a knob learned on an MPE zone, and leaves the channel for a zone it does not know", _ {
		Value kit = element(R"(<kit><midiKnobs>
			<midiKnob mpeZone="upper" ccNumber="1" controlsParam="pan" />
			<midiKnob channel="3" mpeZone="sideways" ccNumber="2" controlsParam="pan" /></midiKnobs></kit>)", "kit");
		auto controls = decode_controls(kit, deluge::global_effectable::kNaming);
		expect(controls.size()).to_equal(size_t{2});
		expect(static_cast<int>(std::get<MidiInputControl>(controls[0].control).input.channel_or_zone)).to_equal(17);
		expect(static_cast<int>(std::get<MidiInputControl>(controls[1].control).input.channel_or_zone)).to_equal(3);
	});

	it("round-trips a sound's knobs through its element", _ {
		Value sound = element(R"(<sound><modKnobs><modKnob controlsParam="pan" />
			<modKnob controlsParam="pitch" patchAmountFromSource="lfo1" patchAmountFromSecondSource="envelope2" />
			</modKnobs></sound>)", "sound");
		auto controls = decode_controls(sound, deluge::instrument::kNaming);
		Value again = Value::makeMap();
		encode_controls(controls, deluge::instrument::kNaming, again);
		const auto reread = decode_controls(again, deluge::instrument::kNaming);
		expect(reread.size()).to_equal(controls.size());
		for (size_t i = 0; i < controls.size(); ++i) {
			expect(reread[i].target.value).to_equal(controls[i].target.value);
			const auto& was = std::get<ModKnobControl>(controls[i].control);
			const auto& is = std::get<ModKnobControl>(reread[i].control);
			expect(static_cast<int>(is.button)).to_equal(static_cast<int>(was.button));
			expect(static_cast<int>(is.knob)).to_equal(static_cast<int>(was.knob));
		}
	});
});

CPPSPEC_SPEC(controls)
