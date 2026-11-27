#include "host/chains/params.h"
#include "instruments/classic/params.h"

#include <array>
#include <cppspec.hpp>
#include <format>
#include <limits>
#include <string>
#include <string_view>

using namespace deluge::host::chains;
using deluge::plugin::ParamId;
using deluge::plugin::ParamScope;

namespace {
struct Pinned {
	uint32_t id;
	std::string_view name;
};
/// Every row, forever: an id never changes its name, and a name never changes its id.
constexpr std::array<Pinned, 27> kPinned{{
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
    {101, "modFXRate"},
    {102, "modFXDepth"},
    {103, "delayRate"},
    {104, "delayFeedback"},
    {105, "pan"},
    {106, "lpfFrequency"},
    {107, "lpfResonance"},
    {108, "lpfMorph"},
    {109, "hpfFrequency"},
    {110, "hpfResonance"},
    {111, "hpfMorph"},
    {112, "reverbAmount"},
    {113, "volume"},
    {114, "sidechainCompressorVolume"},
    {115, "pitchAdjust"},
    {116, "tempo"},
}};
namespace plugin = deluge::plugin;

/// The rows written out field for field: the oracle the table's rows must equal, however the table builds them.
constexpr std::array<plugin::ParamInfo, 27> kRecordedRows{{

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
    {.id = param::kModFXRate, .file_name = "modFXRate", .default_value = -536870912},
    {.id = param::kModFXDepth, .file_name = "modFXDepth"},
    {.id = param::kDelayRate, .file_name = "delayRate", .flags = plugin::ParamFlag::MIDDLE_DETENT},
    {.id = param::kDelayFeedback,
     .file_name = "delayFeedback",
     .default_value = std::numeric_limits<int32_t>::min(),
     .flags = plugin::ParamFlag::MIDDLE_DETENT},
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

/// Every field of @p row as text, so a failing comparison names the row and shows the field that differs.
std::string row_text(const plugin::ParamInfo& row) {
	return std::format("{} {} scope {} units {} default {} range [{}, {}] mapping {} flags {:#x}", row.id.value,
	                   row.file_name, static_cast<int>(row.scope), static_cast<int>(row.units), row.default_value,
	                   row.range.min, row.range.max, static_cast<int>(row.mapping), row.flags.bits);
}
} // namespace

// clang-format off
describe params("the host chain's param table", ${
	it("pins every row's id and file name, in id order", _ {
		for (size_t i = 0; i < kPinned.size(); ++i) {
			expect(kParams[i].id.value).to_equal(kPinned[i].id);
			expect(kParams[i].file_name).to_equal(kPinned[i].name);
		}
	});

	it("pins every row's runtime index: the unpatched index, from 90", _ {
		for (const Pinned& row : kPinned) {
			expect(find_runtime_index(ParamId{row.id})).to_have_value();
			expect(static_cast<uint32_t>(*find_runtime_index(ParamId{row.id}))).to_equal(row.id - 90);
			expect(param_id(static_cast<uint8_t>(row.id - 90)).value).to_equal(row.id);
		}
		expect(find_runtime_index(ParamId{89})).not_().to_have_value();
		expect(find_runtime_index(ParamId{117})).not_().to_have_value();
	});

	it("shares the instrument's ids for the params both have", _ {
		for (uint32_t id = 90; id <= 100; ++id) {
			expect(deluge::plugin::find_param(kParams, ParamId{id})->file_name)
			    .to_equal(deluge::plugin::find_param(deluge::instruments::classic::kParams, ParamId{id})->file_name);
		}
	});

	it("marks pan and pitch centred, and nothing else", _ {
		for (const auto& row : kParams) {
			expect(row.flags.has(deluge::plugin::ParamFlag::CENTRED)).to_equal(row.id.value == 105 || row.id.value == 115);
			expect(static_cast<int>(row.scope)).to_equal(static_cast<int>(ParamScope::PER_SOUND));
		}
	});

	it("starts a new instance with its params' defaults", _ {
		expect(deluge::plugin::find_param(kParams, param::kModFXRate)->default_value).to_equal(-536870912);
		expect(deluge::plugin::find_param(kParams, param::kPan)->default_value).to_equal(0);
	});

	it("has one row per name", _ {
		for (const auto& row : kParams) {
			expect(deluge::plugin::find_param(kParams, row.file_name)).to_equal(&row);
		}
	});

	it("keeps every row's fields as recorded", _ {
		for (size_t i = 0; i < kRecordedRows.size(); ++i) {
			expect(row_text(kParams[i])).to_equal(row_text(kRecordedRows[i]));
		}
	});

	it("names each effect's params by their ids, in the effect's order", _ {
		expect(kFilterIds[0].value).to_equal(106U);
		expect(kFilterIds[5].value).to_equal(111U);
		expect(kModFxIds[0].value).to_equal(101U);
		expect(kModFxIds[2].value).to_equal(97U);
		expect(kEqIds[3].value).to_equal(94U);
		expect(kBitcrushIds[0].value).to_equal(96U);
		expect(kBitcrushIds[1].value).to_equal(95U);
	});
});

CPPSPEC_SPEC(params)
