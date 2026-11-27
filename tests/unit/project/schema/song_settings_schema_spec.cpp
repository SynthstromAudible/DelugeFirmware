#include "project/schema/song_settings_schema.h"
#include "serialization/parse_json.h"
#include "serialization/schema/codec.h"
#include "serialization/write_json.h"
#include "support/printers/groove_printer.h"
#include "support/printers/schema_report_printer.h"
#include "support/printers/song_settings_printer.h"
#include "support/schema/generator.h"
#include "support/schema/round_trip.h"

#include <cppspec.hpp>

#include <cstdlib>

#include <string>
#include <vector>

using namespace deluge::project;
using namespace deluge::project::schema;
using deluge::serialization::parseJson;
using deluge::serialization::write_json;
namespace sch = deluge::serialization::schema;

namespace {
template <typename R>
R through_json(sch::Schema<R> schema, const R& record) {
	return sch::decode(schema, *parseJson(write_json(sch::encode(schema, record))))->value;
}
} // namespace

// clang-format off
describe song_settings_schema("the song's settings", ${
	it("write the tempo as one 64-bit number", _ {
		Transport transport;
		transport.samples_per_timer_tick_big = (uint64_t{1} << 40) + 5;
		transport.inside_world_tick_magnitude = 2;
		transport.sync_scaling_clip_id = 9;
		const auto written = sch::encode(kTransportSchema, transport);
		expect(written.find("samplesPerTimerTick")->str()).to_equal(std::to_string((uint64_t{1} << 40) + 5));
		expect(through_json(kTransportSchema, transport)).to_equal(transport);
	});

	it("clamp a swing past its range", _ {
		const auto back = sch::decode(kGrooveSchema, *parseJson(R"({"swingAmount": 60, "swingInterval": 4})"));
		expect(back->value.swing_amount).to_equal(int8_t{49});
		expect(back->value.swing_interval).to_equal(deluge::plugin::SyncLevel::EIGHTH);
		expect(back->report.notes).to_equal(std::vector<sch::LoadNote>{{sch::LoadNote::Kind::CLAMPED, "swingAmount"}});
	});

	it("round-trip a scale's key, notes and disabled scales", _ {
		ScaleSettings scale;
		scale.key.rootNote = 5;
		scale.key.modeNotes = deluge::music::NoteSet(uint16_t{0b101010110101});
		scale.user_scale_notes = deluge::music::NoteSet(uint16_t{0b000010010001});
		scale.disabled_preset_scales.set(3);
		const auto written = sch::encode(kScaleSchema, scale);
		expect(written.find("modeNotes")->str()).to_equal(std::to_string(0b101010110101));
		expect(through_json(kScaleSchema, scale)).to_equal(scale);
	});

	it("round-trip the reverb's reals exactly", _ {
		ReverbSettings reverb;
		reverb.room_size = 0.123F;
		reverb.width = 1.0F / 3.0F;
		reverb.model = ReverbModel::DIGITAL;
		reverb.sidechain_sync = deluge::plugin::SyncLevel::SIXTEENTH;
		expect(sch::encode(kReverbSchema, reverb).find("model")->str()).to_equal(std::string{"digital"});
		expect(through_json(kReverbSchema, reverb)).to_equal(reverb);
	});

	it("round-trip generated records", _ {
		for (uint32_t seed = 1; seed <= 200; ++seed) {
			expect(sch::test::round_trip(kTransportSchema, sch::test::Generator{seed}.record(kTransportDesc)).error_or(std::string{})).to_equal(std::string{});
			expect(sch::test::round_trip(kGrooveSchema, sch::test::Generator{seed}.record(kGrooveDesc)).error_or(std::string{})).to_equal(std::string{});
			expect(sch::test::round_trip(kScaleSchema, sch::test::Generator{seed}.record(kScaleDesc)).error_or(std::string{})).to_equal(std::string{});
			expect(sch::test::round_trip(kReverbSchema, sch::test::Generator{seed}.record(kReverbDesc)).error_or(std::string{})).to_equal(std::string{});
		}
	});
});
// clang-format on

int song_settings_schema_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(song_settings_schema).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
