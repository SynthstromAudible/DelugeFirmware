#include "effects/global_effectable/params.h"
#include "instruments/deluge/params.h"

#include <array>
#include <cppspec.hpp>
#include <limits>
#include <string_view>

using namespace deluge::global_effectable;
using deluge::plugin::ParamId;
using deluge::plugin::ParamScope;

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
namespace plugin = deluge::plugin;

/// The rows written out field for field: the oracle the table's rows must equal, however the table builds them.
constexpr std::array<plugin::ParamInfo, 43> kRecordedRows{{

    {.id = param::kStutterRate, .file_name = "stutterRate"},
    {.id = param::kBass, .file_name = "bass", .flags = plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kTreble, .file_name = "treble", .flags = plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kBassFreq, .file_name = "bassFreq"},
    {.id = param::kTrebleFreq, .file_name = "trebleFreq"},
    {.id = param::kSampleRateReduction,
     .file_name = "sampleRateReduction",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kBitcrushAmount, .file_name = "bitcrushAmount", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kModFXOffset, .file_name = "modFXOffset"},
    {.id = param::kModFXFeedback, .file_name = "modFXFeedback", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kCompressorShape, .file_name = "compressorShape", .default_value = -601295438},
    {.id = param::kCompressorThreshold, .file_name = "compressorThreshold", .mapping = plugin::MappingKind::UNIPOLAR},
    {.id = param::kArpGate, .file_name = "arpGate", .default_value = std::numeric_limits<int32_t>::max()},
    {.id = param::kRhythm, .file_name = "rhythm", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kSequenceLength, .file_name = "sequenceLength", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kChordPolyphony, .file_name = "chordPolyphony", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kRatchetAmount, .file_name = "ratchetAmount", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kNoteProbability,
     .file_name = "noteProbability",
     .default_value = std::numeric_limits<int32_t>::max()},
    {.id = param::kReverseProbability,
     .file_name = "reverseProbability",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kBassProbability,
     .file_name = "bassProbability",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kSwapProbability,
     .file_name = "swapProbability",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kGlideProbability,
     .file_name = "glideProbability",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kChordProbability,
     .file_name = "chordProbability",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kRatchetProbability,
     .file_name = "ratchetProbability",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kSpreadGate, .file_name = "spreadGate", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kSpreadOctave, .file_name = "spreadOctave", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kSpreadVelocity, .file_name = "spreadVelocity", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kModFXRate, .file_name = "modFXRate", .default_value = -536870912},
    {.id = param::kModFXDepth, .file_name = "modFXDepth"},
    {.id = param::kDelayRate, .file_name = "delayRate", .flags = plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kDelayFeedback,
     .file_name = "delayFeedback",
     .default_value = std::numeric_limits<int32_t>::min(),
     .flags = plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kArpRate, .file_name = "arpRate"},
    {.id = param::kPan,
     .file_name = "pan",
     .mapping = plugin::MappingKind::PAN,
     .flags = plugin::ParamFlag::CENTRED | plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kLpfFrequency, .file_name = "lpfFrequency", .default_value = std::numeric_limits<int32_t>::max()},
    {.id = param::kLpfResonance, .file_name = "lpfResonance", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kLpfMorph, .file_name = "lpfMorph", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kHpfFrequency, .file_name = "hpfFrequency", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kHpfResonance, .file_name = "hpfResonance", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kHpfMorph, .file_name = "hpfMorph", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kReverbAmount, .file_name = "reverbAmount", .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kVolume, .file_name = "volume"},
    {.id = param::kSidechainCompressorVolume,
     .file_name = "sidechainCompressorVolume",
     .default_value = std::numeric_limits<int32_t>::min()},
    {.id = param::kPitchAdjust,
     .file_name = "pitchAdjust",
     .flags = plugin::ParamFlag::CENTRED | plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kTempo,
     .file_name = "tempo",
     .flags = plugin::ParamFlag::NO_UNDO | plugin::ParamFlag::FLOAT_INTERPOLATION},
}};

bool same_row(const plugin::ParamInfo& a, const plugin::ParamInfo& b) {
	return a.id == b.id && a.file_name == b.file_name && a.scope == b.scope && a.units == b.units
	       && a.default_value == b.default_value && a.range == b.range && a.mapping == b.mapping && a.flags == b.flags;
}
} // namespace

// clang-format off
describe params("the provisional global-effectable param table", ${
	it("pins every row's id and file name, in id order", _ {
		for (size_t i = 0; i < kPinned.size(); ++i) {
			expect(kParams[i].id.value).to_equal(kPinned[i].id);
			expect(kParams[i].file_name).to_equal(kPinned[i].name);
		}
	});

	it("pins every row's runtime index: the unpatched index, from 90", _ {
		for (const Pinned& row : kPinned) {
			expect(runtime_index(ParamId{row.id})).to_have_value();
			expect(static_cast<uint32_t>(*runtime_index(ParamId{row.id}))).to_equal(row.id - 90);
			expect(param_id(static_cast<uint8_t>(row.id - 90)).value).to_equal(row.id);
		}
		expect(runtime_index(ParamId{89})).not_().to_have_value();
		expect(runtime_index(ParamId{133})).not_().to_have_value();
	});

	it("shares the instrument's ids for the params both have", _ {
		for (uint32_t id = 90; id <= 115; ++id) {
			expect(deluge::plugin::find_param(kParams, ParamId{id})->file_name)
			    .to_equal(deluge::plugin::find_param(deluge::instrument::kParams, ParamId{id})->file_name);
		}
	});

	it("marks pan and pitch centred, and nothing else", _ {
		for (const auto& row : kParams) {
			expect(row.flags.has(deluge::plugin::ParamFlag::CENTRED)).to_equal(row.id.value == 121 || row.id.value == 131);
			expect(static_cast<int>(row.scope)).to_equal(static_cast<int>(ParamScope::PER_SOUND));
		}
	});

	it("starts a new instance with its params' defaults", _ {
		expect(deluge::plugin::find_param(kParams, param::kModFXRate)->default_value).to_equal(-536870912);
		expect(deluge::plugin::find_param(kParams, param::kArpGate)->default_value)
		    .to_equal(std::numeric_limits<int32_t>::max());
		expect(deluge::plugin::find_param(kParams, param::kPan)->default_value).to_equal(0);
	});

	it("has one row per name", _ {
		for (const auto& row : kParams) {
			expect(deluge::plugin::find_param(kParams, row.file_name)).to_equal(&row);
		}
	});

	it("keeps every row's fields as recorded", _ {
		for (size_t i = 0; i < kRecordedRows.size(); ++i) {
			expect(same_row(kParams[i], kRecordedRows[i])).to_equal(true);
		}
	});
});

CPPSPEC_SPEC(params)
