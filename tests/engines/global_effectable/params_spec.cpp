#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"

#include <array>
#include <cppspec.hpp>
#include <limits>
#include <string_view>

using namespace deluge::global_effectable;
using deluge::engine::ParamId;
using deluge::engine::ParamScope;

namespace {
struct Pinned {
	uint32_t id;
	std::string_view name;
};
/// Every row, forever: an id never changes its name, and a name never changes its id.
constexpr std::array<Pinned, 43> kPinned{{
    {90, "stutterRate"},
    {91, "bass"},
    {92, "treble"},
    {93, "bassFreq"},
    {94, "trebleFreq"},
    {95, "sampleRateReduction"},
    {96, "bitcrushAmount"},
    {97, "modFXOffset"},
    {98, "modFXFeedback"},
    {99, "compressorShape"},
    {100, "compressorThreshold"},
    {101, "arpGate"},
    {102, "rhythm"},
    {103, "sequenceLength"},
    {104, "chordPolyphony"},
    {105, "ratchetAmount"},
    {106, "noteProbability"},
    {107, "reverseProbability"},
    {108, "bassProbability"},
    {109, "swapProbability"},
    {110, "glideProbability"},
    {111, "chordProbability"},
    {112, "ratchetProbability"},
    {113, "spreadGate"},
    {114, "spreadOctave"},
    {115, "spreadVelocity"},
    {116, "modFXRate"},
    {117, "modFXDepth"},
    {118, "delayRate"},
    {119, "delayFeedback"},
    {120, "arpRate"},
    {121, "pan"},
    {122, "lpfFrequency"},
    {123, "lpfResonance"},
    {124, "lpfMorph"},
    {125, "hpfFrequency"},
    {126, "hpfResonance"},
    {127, "hpfMorph"},
    {128, "reverbAmount"},
    {129, "volume"},
    {130, "sidechainCompressorVolume"},
    {131, "pitchAdjust"},
    {132, "tempo"},
}};
} // namespace

// clang-format off
describe params("the provisional global-effectable param table", ${
	it("pins every row's id and file name, in id order", _ {
		for (size_t i = 0; i < kPinned.size(); ++i) {
			expect(kParams[i].id.value).to_equal(kPinned[i].id);
			expect(kParams[i].file_name == kPinned[i].name).to_equal(true);
		}
	});

	it("pins every row's runtime index: the unpatched index, from 90", _ {
		for (const Pinned& row : kPinned) {
			expect(runtime_index(ParamId{row.id}).has_value()).to_equal(true);
			expect(static_cast<uint32_t>(*runtime_index(ParamId{row.id}))).to_equal(row.id - 90);
			expect(param_id(static_cast<uint8_t>(row.id - 90)).value).to_equal(row.id);
		}
		expect(runtime_index(ParamId{89}).has_value()).to_equal(false);
		expect(runtime_index(ParamId{133}).has_value()).to_equal(false);
	});

	it("shares the instrument's ids for the params both have", _ {
		for (uint32_t id = 90; id <= 115; ++id) {
			expect(deluge::engine::find_param(kParams, ParamId{id})->file_name
			       == deluge::engine::find_param(deluge::instrument::kParams, ParamId{id})->file_name).to_equal(true);
		}
	});

	it("marks pan and pitch bipolar, and nothing else", _ {
		for (const auto& row : kParams) {
			expect(row.bipolar).to_equal(row.id.value == 121 || row.id.value == 131);
			expect(row.scope == ParamScope::PER_SOUND).to_equal(true);
		}
	});

	it("starts a new instance with its params' defaults", _ {
		expect(deluge::engine::find_param(kParams, param::kModFXRate)->default_value).to_equal(-536870912);
		expect(deluge::engine::find_param(kParams, param::kArpGate)->default_value)
		    .to_equal(std::numeric_limits<int32_t>::max());
		expect(deluge::engine::find_param(kParams, param::kPan)->default_value).to_equal(0);
	});

	it("has one row per name", _ {
		for (const auto& row : kParams) {
			expect(deluge::engine::find_param(kParams, row.file_name) == &row).to_equal(true);
		}
	});
});

CPPSPEC_SPEC(params)
