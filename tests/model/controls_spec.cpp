#include "controls_test_engines.h"
#include "deluge-project/controls_codec.h"
#include "engines/deluge/cables.h"
#include "engines/deluge/params.h"
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
describe controls("a sound's mod knobs as model records", ${
	it("reads mod knobs by button and knob, a cable's depth included", _ {
		Value sound = element(R"(<sound><modKnobs>
			<modKnob controlsParam="pan" /><modKnob controlsParam="volumePostFX" />
			<modKnob controlsParam="pitch" patchAmountFromSource="lfo1" /><modKnob controlsParam="nosuchparam" />
			<modKnob controlsParam="lfo1Rate" /></modKnobs></sound>)", "sound");
		auto controls = decode_controls(sound, deluge::instrument::kNaming);
		expect(controls.size()).to_equal(size_t{4});
		expect(controls[2].target.value)
		    .to_equal(deluge::instrument::cable_depth_id(deluge::instrument::PatchSource::LFO_GLOBAL_1, ip::kPitch)->value);
		const auto& lfo = controls[2].control;
		expect(static_cast<int>(lfo.button)).to_equal(1);
		expect(static_cast<int>(lfo.knob)).to_equal(0);
		// the unnamed fourth entry is skipped but still takes (1, 1); lfo1Rate is button 2, knob 0
		const auto& rate = controls[3].control;
		expect(static_cast<int>(rate.button)).to_equal(2);
		expect(static_cast<int>(rate.knob)).to_equal(0);
	});

	it("points a mod knob's volume target where the runtime does", _ {
		Value sound = element(R"(<sound><modKnobs><modKnob controlsParam="volume" /></modKnobs></sound>)", "sound");
		expect(decode_controls(sound, deluge::instrument::kNaming)[0].target.value).to_equal(ip::kVolumePostFX.value);
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
			const auto& was = controls[i].control;
			const auto& is = reread[i].control;
			expect(static_cast<int>(is.button)).to_equal(static_cast<int>(was.button));
			expect(static_cast<int>(is.knob)).to_equal(static_cast<int>(was.knob));
		}
	});
});

CPPSPEC_SPEC(controls)
