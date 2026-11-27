#include "instruments/classic/codec.h"
#include "instruments/classic/settings.h"
#include "plugin/types/setting.h"
#include "support/model_fixture.h"
#include "ui/scenes/bind/setting_context.h"
#include "ui/scenes/bind/slot_reader.h"

#include "instruments/classic/words.h"
#include "support/record_state.h"
#include <cppspec.hpp>
#include <string>

using namespace deluge::ui::scenes::bind;
using deluge::plugin::SettingContext;
using deluge::plugin::SettingKind;
using deluge::ui::scenes::bind::test::kDrum;
using deluge::ui::scenes::bind::test::kSynth;
using deluge::ui::scenes::bind::test::ModelFixture;

namespace {
/// Every row where what SlotReader gives for @p slot differs from held_setting of the sound's typed state, as
/// "path=got/want"; empty when every row agrees.
std::string reads_unlike_state(ModelFixture& fixture, deluge::project::SlotAddress slot) {
	const SlotReader reader{fixture.song, slot, fixture.host.sound};
	const deluge::instruments::classic::SoundState& state = fixture.host.sound.states.at(slot.id);
	std::string unlike;
	for (const deluge::plugin::SettingInfo& row : deluge::instruments::classic::kSettings) {
		if (row.kind == SettingKind::STRUCTURED || row.kind == SettingKind::TEXT) {
			continue;
		}
		const auto want = deluge::instruments::classic::held_setting(state, row.path);
		const auto got = reader.read(row.path);
		if (got != want) {
			unlike += std::string(row.path) + "=" + (got ? std::to_string(*got) : "none") + "/"
			          + (want ? std::to_string(*want) : "none") + " ";
		}
	}
	return unlike;
}
} // namespace

// clang-format off
describe bind_reader("a sound's settings read through its slot's address", ${
	it("reads every row a synth track and a kit row hold, as the sound's typed state holds it", _ {
		ModelFixture fixture;
		expect(reads_unlike_state(fixture, kSynth)).to_equal(std::string{});
		expect(reads_unlike_state(fixture, kDrum)).to_equal(std::string{});
	});

	it("reads a changed state: a choice, a number, an LFO's sync level and an FM sound's oscillator type", _ {
		ModelFixture fixture;
		deluge::instruments::classic::SoundState state = fixture.host.sound.states.at(kSynth.id);
		deluge::test::set(state, "polyphonic", deluge::instruments::classic::to_string(deluge::instruments::classic::PolyphonyMode::MONO));
		deluge::test::set(state, "unison/num", "4");
		deluge::test::set(state, "lfo1/syncLevel", "5");
		deluge::test::set(state, "osc1/type", deluge::instruments::classic::to_string(deluge::instruments::classic::OscType::SAW));
		deluge::test::set(state, "mode", deluge::instruments::classic::to_string(deluge::instruments::classic::SynthMode::FM));
		fixture.set_state(kSynth, state);
		expect(reads_unlike_state(fixture, kSynth)).to_equal(std::string{});
		const SlotReader reader{fixture.song, kSynth, fixture.host.sound};
		expect(reader.read("osc1/type").value_or(-1)).to_equal(static_cast<int32_t>(deluge::instruments::classic::OscType::SAW));
	});

	it("reads every row the table holds: the host contract's reader never answers nothing for one", _ {
		ModelFixture fixture;
		for (const deluge::project::SlotAddress slot : {kSynth, kDrum}) {
			const SlotReader reader{fixture.song, slot, fixture.host.sound};
			std::string missing;
			for (const deluge::plugin::SettingInfo& row : deluge::instruments::classic::kSettings) {
				if (row.kind != SettingKind::STRUCTURED && row.kind != SettingKind::TEXT && !reader.read(row.path)) {
					missing += std::string(row.path) + " ";
				}
			}
			expect(missing).to_equal(std::string{});
		}
	});

	it("reads nothing for a path the table does not hold", _ {
		ModelFixture fixture;
		const SlotReader reader{fixture.song, kSynth, fixture.host.sound};
		expect(reader.read("no/such/path").has_value()).to_be_false();
		expect(reader.reader()("no/such/path").has_value()).to_be_false();
	});

	it("resolves nothing once the slot is gone", _ {
		ModelFixture fixture;
		fixture.song.drum_structure.find(kDrum.id)->instrument.reset();
		const SlotReader reader{fixture.song, kDrum, fixture.host.sound};
		expect(reader.resolved()).to_be_false();
		expect(reader.read("polyphonic").has_value()).to_be_false();
	});

	it("builds the host contract's context from the slot's kind and the runtime facts", _ {
		ModelFixture fixture;
		fixture.host.audio.inputs = true;
		fixture.host.sound.loaded[kDrum.id] = 0b10;
		const SettingContext synth = setting_context(fixture.song, kSynth, fixture.host.sound, fixture.host.audio);
		expect(synth.owner).to_equal(SettingContext::Owner::TRACK);
		expect(synth.instrument).to_equal(SettingContext::Instrument::SYNTH);
		expect(synth.audio_inputs_present).to_be_true();
		const SettingContext lane = setting_context(fixture.song, kDrum, fixture.host.sound, fixture.host.audio);
		expect(lane.owner).to_equal(SettingContext::Owner::KIT_LANE);
		expect(lane.instrument).to_equal(SettingContext::Instrument::SOUND_DRUM);
		expect(static_cast<int32_t>(lane.loaded_sources)).to_equal(0b10);
		expect(deluge::plugin::context_is_coherent(synth)).to_be_true();
		expect(deluge::plugin::context_is_coherent(lane)).to_be_true();
		expect(fixture.host.sound.asked.size()).to_equal(2u);
	});
});

CPPSPEC_SPEC(bind_reader)
