#include "instruments/classic/cables.h"
#include "support/printers/cable_route_printer.h"
#include "synth/modulation/cable.h"

#include <cppspec.hpp>

#include "support/printers/param_id_printer.h"

using namespace deluge::instruments::classic;
using deluge::plugin::ParamId;

// clang-format off
describe cables("the Deluge instrument's patch-cable words and depth ids", ${
	it("names every source as the file does, and reads an unknown one as none", _ {
		expect(to_string(PatchSource::LFO_GLOBAL_2)).to_equal(std::string_view("lfo3"));
		expect(to_string(PatchSource::LFO_LOCAL_1)).to_equal(std::string_view("lfo2"));
		expect(to_string(PatchSource::SIDECHAIN)).to_equal(std::string_view("compressor"));
		expect(to_string(PatchSource::ENVELOPE_0)).to_equal(std::string_view("envelope1"));
		for (uint8_t s = 0; s <= static_cast<uint8_t>(PatchSource::NONE); ++s) {
			expect(static_cast<int>(parse_patch_source(to_string(static_cast<PatchSource>(s))))).to_equal(static_cast<int>(s));
		}
		expect(to_string(parse_patch_source("wobble"))).to_equal(std::string_view("none"));
	});

	it("reads a polarity, unknown as bipolar", _ {
		expect(to_string(parse_polarity("unipolar"))).to_equal(std::string_view("unipolar"));
		expect(to_string(parse_polarity("x"))).to_equal(std::string_view("bipolar"));
		expect(to_string(Polarity::UNIPOLAR)).to_equal(std::string_view("unipolar"));
	});

	it("pins the depth id of a cable to a param, and of cables controlling depths", _ {
		// LFO1 (0) -> pitch (25): flag | 25 | (0+1)<<8
		auto pitchDepth = cable_depth_id(PatchSource::LFO_GLOBAL_1, ParamId{25});
		expect(pitchDepth->value).to_equal(0x4000'0119u);
		// envelope2 (4) controlling that depth: | (4+1)<<14
		auto nested = cable_depth_id(PatchSource::ENVELOPE_1, *pitchDepth);
		expect(nested->value).to_equal(0x4001'4119u);
		// a third level: velocity (12) | (12+1)<<20
		auto third = cable_depth_id(PatchSource::VELOCITY, *nested);
		expect(third->value).to_equal(0x40D1'4119u);
		// no fourth
		expect(cable_depth_id(PatchSource::NOTE, *third)).not_().to_have_value();
	});

	it("routes a depth id back to its source and destination", _ {
		auto nested = *cable_depth_id(PatchSource::ENVELOPE_1, *cable_depth_id(PatchSource::LFO_GLOBAL_1, ParamId{25}));
		auto route = cable_route(nested);
		expect(to_string(route->source)).to_equal(std::string_view("envelope2"));
		expect(route->destination.value).to_equal(cable_depth_id(PatchSource::LFO_GLOBAL_1, ParamId{25})->value);
		auto inner = cable_route(route->destination);
		expect(to_string(inner->source)).to_equal(std::string_view("lfo1"));
		expect(inner->destination.value).to_equal(25u);
		expect(cable_route(ParamId{25})).not_().to_have_value();
	});

	it("keeps depth ids apart from param ids, unresolved destinations included", _ {
		expect(is_cable_depth(ParamId{116})).to_be_false();
		expect(is_cable_depth(*cable_depth_id(PatchSource::NONE, kUnknownDestination))).to_be_true();
		expect(cable_route(*cable_depth_id(PatchSource::X, kUnknownDestination))->destination.value)
		    .to_equal(kUnknownDestination.value);
		expect(cable_depth_id(PatchSource::X, ParamId{300})).not_().to_have_value();
	});

	it("reads the depth id of a packed cable word, one source byte per level and 0xFF for none", _ {
		// pitch (25) itself, no sources
		expect(cable_depth_of_word(0xFFFF'FF19u).value).to_equal(25u);
		// LFO1 (0) onto pitch: 25 | 0 << 8
		expect(cable_depth_of_word(0xFFFF'0019u).value).to_equal(0x4000'0119u);
		// envelope2 (4) onto that depth: 25 | 0 << 8 | 4 << 16
		expect(cable_depth_of_word(0xFF04'0019u).value).to_equal(0x4001'4119u);
		// velocity (12) onto that: 25 | 0 << 8 | 4 << 16 | 12 << 24
		expect(cable_depth_of_word(0x0C04'0019u).value).to_equal(0x40D1'4119u);
	});

	it("packs a depth id into the runtime's cable word, the inverse of reading it", _ {
		expect(cable_word_of(ParamId{25})).to_equal(0xFFFF'FF19u);
		expect(cable_word_of(ParamId{0x4000'0119u})).to_equal(0xFFFF'0019u);
		expect(cable_word_of(ParamId{0x4001'4119u})).to_equal(0xFF04'0019u);
		expect(cable_word_of(ParamId{0x40D1'4119u})).to_equal(0x0C04'0019u);
	});

	it("names the depth id of the cable from a source onto a destination", _ {
		using deluge::synth::CableTarget;
		expect(cable_depth_of(PatchSource::LFO_GLOBAL_1, CableTarget::param(25)).value).to_equal(0x4000'0119u);
		expect(cable_depth_of(PatchSource::ENVELOPE_1, CableTarget::depth_of(25, PatchSource::LFO_GLOBAL_1)).value)
		    .to_equal(0x4001'4119u);
	});

	it("keeps the unknown destination at 89, an id no param has", _ {
		expect(kUnknownDestination.value).to_equal(89u);
		expect(runtime_slot(kUnknownDestination)).not_().to_have_value();
	});

	it("names a sound-wide param in a cable word by its runtime index and in a depth id by its id, both ways", _ {
		using deluge::synth::CableTarget;
		// the post-FX volume (id 45) is the runtime's 48; LFO1 (0) onto its depth's sidechain-sourced range
		const CableTarget post_fx = cable_target_of(param::kVolumePostFX);
		expect(static_cast<uint32_t>(post_fx.param_index())).to_equal(48u);
		const ParamId depth = cable_depth_of(PatchSource::LFO_GLOBAL_1, post_fx);
		expect(depth.value).to_equal(cable_depth_id(PatchSource::LFO_GLOBAL_1, param::kVolumePostFX)->value);
		expect(cable_word_of(depth)).to_equal(post_fx.add_source(PatchSource::LFO_GLOBAL_1).word);
		const uint32_t nested = CableTarget::depth_of_depth(48, PatchSource::SIDECHAIN, PatchSource::ENVELOPE_1).word;
		expect(cable_depth_of_word(nested).value)
		    .to_equal(cable_depth_id(PatchSource::ENVELOPE_1, *cable_depth_id(PatchSource::SIDECHAIN, param::kVolumePostFX))
		                  ->value);
		expect(cable_word_of(cable_depth_of_word(nested))).to_equal(nested);
		// a DX7 param is the runtime's 45 to 47
		expect(static_cast<uint32_t>(cable_target_of(param::kDx7Pitch).param_index())).to_equal(47u);
		expect(knob_param_of(CableTarget::param(48)).value).to_equal(param::kVolumePostFX.value);
		expect(cable_root(depth).value).to_equal(param::kVolumePostFX.value);
	});

	it("names the param a mod knob's descriptor word points at, by the instrument's id", _ {
		using deluge::synth::CableTarget;
		// a per-voice param is its own index, an unpatched one kUnpatchedFirstId (90) on from its runtime index: the id
		// is the index
		expect(knob_param_of(CableTarget::param(25)).value).to_equal(25u);
		expect(knob_param_of(CableTarget::param(91)).value).to_equal(91u);
		// a cable is its depth id
		expect(knob_param_of(CableTarget::depth_of(25, PatchSource::LFO_GLOBAL_1)).value).to_equal(0x4000'0119u);
		expect(knob_param_of(CableTarget::depth_of_depth(25, PatchSource::LFO_GLOBAL_1, PatchSource::ENVELOPE_1)).value)
		    .to_equal(0x4001'4119u);
	});
});

CPPSPEC_SPEC(cables)
