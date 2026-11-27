#include "arp/params.h"
#include "controls_test_plugins.h"
#include "instruments/classic/cables.h"
#include "instruments/classic/params.h"
#include "project/controls_codec.h"
#include "serialization/parse_xml.h"

#include <cppspec.hpp>

#include "support/printers/param_target_printer.h"

using namespace deluge::project;
using deluge::plugin::ParamId;
using deluge::serialization::Value;
namespace ip = deluge::instruments::classic::param;

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
		auto controls = decode_controls(sound, deluge::instruments::classic::kNaming);
		expect(controls.size()).to_equal(size_t{4});
		expect(controls[2].target.id.value)
		    .to_equal(deluge::instruments::classic::cable_depth_id(deluge::instruments::classic::PatchSource::LFO_GLOBAL_1, ip::kPitch)->value);
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
		expect(decode_controls(sound, deluge::instruments::classic::kNaming)[0].target.id.value).to_equal(ip::kVolumePostFX.value);
	});

	it("reads a plain arp name as the arp's param, and arpRate with a source as the cable to its row", _ {
		Value sound = element(R"(<sound><modKnobs>
			<modKnob controlsParam="arpGate" /><modKnob controlsParam="arpRate" />
			<modKnob controlsParam="arpRate" patchAmountFromSource="lfo1" /></modKnobs></sound>)", "sound");
		const auto controls = decode_controls(sound, deluge::instruments::classic::kNaming);
		expect(controls.size()).to_equal(size_t{3});
		if (controls.size() != 3) {
			return;
		}
		using deluge::project::ParamSpace;
		using deluge::project::ParamTarget;
		expect(controls[0].target).to_equal(ParamTarget{.space = ParamSpace::ARP, .id = deluge::arp::param::kGate});
		expect(controls[1].target).to_equal(ParamTarget{.space = ParamSpace::ARP, .id = deluge::arp::param::kRate});
		expect(controls[2].target)
		    .to_equal(deluge::project::engine_target(deluge::instruments::classic::cable_depth_id(
		        deluge::instruments::classic::PatchSource::LFO_GLOBAL_1, ip::kArpRate)->value));

		Value again = Value::makeMap();
		encode_controls(controls, deluge::instruments::classic::kNaming, again);
		const auto reread = decode_controls(again, deluge::instruments::classic::kNaming);
		expect(reread.size()).to_equal(controls.size());
		for (size_t i = 0; i < controls.size() && i < reread.size(); ++i) {
			expect(reread[i].target).to_equal(controls[i].target);
		}
	});

	it("round-trips a sound's knobs through its element", _ {
		Value sound = element(R"(<sound><modKnobs><modKnob controlsParam="pan" />
			<modKnob controlsParam="pitch" patchAmountFromSource="lfo1" patchAmountFromSecondSource="envelope2" />
			</modKnobs></sound>)", "sound");
		auto controls = decode_controls(sound, deluge::instruments::classic::kNaming);
		Value again = Value::makeMap();
		encode_controls(controls, deluge::instruments::classic::kNaming, again);
		const auto reread = decode_controls(again, deluge::instruments::classic::kNaming);
		expect(reread.size()).to_equal(controls.size());
		for (size_t i = 0; i < controls.size(); ++i) {
			expect(reread[i].target).to_equal(controls[i].target);
			const auto& was = controls[i].control;
			const auto& is = reread[i].control;
			expect(static_cast<int>(is.button)).to_equal(static_cast<int>(was.button));
			expect(static_cast<int>(is.knob)).to_equal(static_cast<int>(was.knob));
		}
	});
});

CPPSPEC_SPEC(controls)
