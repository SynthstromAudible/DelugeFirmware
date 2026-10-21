// One instrument setting written across a kit's sound drums as one Edit: each drum the fan-out rule picks takes the
// value, record and runtime alike, and its slot still decodes to its typed state.

#include "engine-api/types/setting.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/settings.h"
#include "engines/deluge/sound_state.h"
#include "model/edits/edit.h"
#include "model/edits/set_kit_setting.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "scenes/menu/nodes/fan_out_rules.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <string>
#include <vector>

using namespace deluge::scenes;
using deluge::serialization::Value;

namespace {

constexpr int32_t kKitDrums = 16;

/// The drums of @p f's kit whose record or runtime disagrees with @p word at @p path, or whose slot does not decode to
/// its typed state.
std::vector<int> disagreeing(spec::SongFixture& f, std::string_view path, std::string_view word) {
	std::vector<int> found;
	int index = 0;
	for (Drum* drum = static_cast<Kit*>(f.instrument)->firstDrum; drum != nullptr; drum = drum->next, ++index) {
		auto& sound = *static_cast<SoundDrum*>(drum);
		const auto held = deluge::instrument::setting_value(*sound.boundState(), path);
		auto* slot = f.song->project.resolveSlot(*deluge::bridge::slotAddressOf(sound));
		deluge::bridge::refreshSlot(sound, *slot); // a cache of the state, refreshed by its readers
		const auto decoded = deluge::instrument::decode_onto(slot->state, *sound.boundState(),
		                                                     {.input_tick_magnitude = slot->tick_magnitude});
		if (!held || held->str() != word || !decoded
		    || deluge::instrument::dump_state(*decoded) != deluge::instrument::dump_state(*sound.boundState())) {
			found.push_back(index);
		}
	}
	return found;
}

spec::SongFixture full_kit() {
	spec::SongFixture f = spec::make_kit_song();
	for (int32_t d = 0; d < kKitDrums; ++d) {
		spec::add_drum<SoundDrum>(f);
	}
	return f;
}

uint32_t kit_id(spec::SongFixture& f) {
	return f.instrument->id().value_or(0);
}

} // namespace

// clang-format off
describe set_kit_setting("one setting across a kit's sound drums, as one Edit", ${
	it("writes voice priority and transpose on every sound drum, record, runtime and slot alike", _ {
		spec::SongFixture f = full_kit();
		deluge::edits::Context ctx{*f.song, f.song->project};
		deluge::edits::SetKitSetting priority{kit_id(f), "voicePriority", Value::makeScalar("2"), {}};
		expect(deluge::edits::apply(ctx, priority) == Error::NONE).to_be_true();
		expect(priority.drums_written()).to_equal(uint32_t(kKitDrums));
		expect(disagreeing(f, "voicePriority", "2")).to_equal(std::vector<int>{});
		deluge::edits::SetKitSetting transpose{kit_id(f), "transpose", Value::makeNumber(7), {}};
		expect(deluge::edits::apply(ctx, transpose) == Error::NONE).to_be_true();
		expect(transpose.drums_written()).to_equal(uint32_t(kKitDrums));
		expect(disagreeing(f, "transpose", "7")).to_equal(std::vector<int>{});
	});

	it("writes only the drums its fan-out rule picks, and passes over a drum that refuses the value", _ {
		spec::SongFixture f = full_kit();
		auto* kit = static_cast<Kit*>(f.instrument);
		auto* mono = static_cast<SoundDrum*>(kit->firstDrum);
		deluge::edits::settingFor(*mono, "polyphonic", Value::makeScalar("mono"));
		for (Drum* drum = mono->next; drum != nullptr; drum = drum->next) {
			deluge::edits::settingFor(*static_cast<SoundDrum*>(drum), "polyphonic", Value::makeScalar("poly"));
		}
		const int before = mono->boundState()->max_voice_count;
		deluge::edits::Context ctx{*f.song, f.song->project};
		deluge::edits::SetKitSetting voices{kit_id(f), "maxVoices", Value::makeNumber(3), menu::fan_out::kPolyphonic};
		expect(deluge::edits::apply(ctx, voices) == Error::NONE).to_be_true();
		expect(voices.drums_written()).to_equal(uint32_t(kKitDrums - 1));
		expect(static_cast<int>(mono->boundState()->max_voice_count)).to_equal(before);
		// A saw drum does not take a sample's reverse: the rest of the kit does, and the Edit succeeds.
		deluge::edits::settingFor(*mono, "osc1/type", Value::makeScalar("saw"));
		for (Drum* drum = mono->next; drum != nullptr; drum = drum->next) {
			deluge::edits::settingFor(*static_cast<SoundDrum*>(drum), "osc1/type", Value::makeScalar("sample"));
		}
		deluge::edits::SetKitSetting reverse{kit_id(f), "osc1/reversed", Value::makeNumber(1), {}};
		expect(deluge::edits::apply(ctx, reverse) == Error::NONE).to_be_true();
		expect(reverse.drums_written() >= uint32_t(kKitDrums - 1)).to_be_true();
	});

	it("refuses a kit the song does not hold, and a value no drum's row takes", _ {
		spec::SongFixture f = full_kit();
		deluge::edits::Context ctx{*f.song, f.song->project};
		deluge::edits::SetKitSetting gone{9999, "voicePriority", Value::makeScalar("2"), {}};
		expect(deluge::edits::apply(ctx, gone) == Error::NONE).to_be_false();
		deluge::edits::SetKitSetting nonsense{kit_id(f), "voicePriority", Value::makeScalar("loud"), {}};
		expect(deluge::edits::apply(ctx, nonsense) == Error::NONE).to_be_false();
	});
});

DELUGE_SPEC(set_kit_setting_spec, set_kit_setting)
