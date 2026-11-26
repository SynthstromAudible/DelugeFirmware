// The sound menu's hook-built items write by address: an oscillator's transpose (a sample source's range, else the
// source's own setting, each sound drum of a whole kit by its own kind), the sample interpolation, the stutter's
// direction (inheriting the song's stutter on "use song") and quantize, and the play direction (a kit row's note row,
// else the clip). Each is a request to the menu's sink; with the app's sink, the record holds the result.

#include "gui/ui/menu_view_of.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/menu/bind_host.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/nodes/sound_actions.h"
#include "ui/scenes/menu/nodes/sound_fx.h"
#include "ui/scenes/menu/nodes/sound_osc.h"

#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "project/setting_group.h"

#include "fakes.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"
#include "support/record_state.h"

#include "support/menu_context.h"
#include "support/spec_main.h"

#include <optional>
#include <string>
#include <vector>

using namespace deluge::ui::scenes;
using deluge::ui::scenes::menu::MenuContext;
namespace nodes = deluge::ui::scenes::menu::nodes;
using deluge::serialization::Value;

namespace {

/// @brief Make source 0 of @p sound a sample source with one range, recorded as a gesture records it.
void give_sample_range(::Sound& sound) {
	deluge::edits::settingFor(sound, "osc1/type", Value::makeScalar("sample"));
	(void)sound.getOrCreateFirstRange(0);
	deluge::edits::recordRanges(sound, 0);
}

/// @brief The sound's state, by its slot.
const deluge::instruments::classic::SoundState* state_of(const MenuContext& ctx) {
	return ctx.addresses.slot ? menu::host_ports().sound->sound_state(*ctx.addresses.slot) : nullptr;
}

/// @brief The sound at @p slot.
const deluge::instruments::classic::SoundState* state_at(deluge::project::SlotAddress slot) {
	return menu::host_ports().sound->sound_state(slot);
}

} // namespace

// clang-format off
describe sound_hooks("the sound menu's hook-built items, by address", ${
	it("hands an osc transpose to the sound port, and a sample source's lands in its range", _ {
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		give_sample_range(sound);
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		bind::test::RecordingSoundWrites recording;
		MenuContext recorded = ctx;
		recorded.ports.sound_writes = &recording;
		menu::bindings::write(nodes::source0_transpose, recorded, 250);
		expect(recording.calls.size()).to_equal(size_t{1});
		if (!recording.calls.empty()) {
			expect(recording.calls[0].kind).to_equal(bind::test::RecordingSoundWrites::Call::Kind::TRANSPOSE);
			expect(recording.calls[0].value).to_equal(int64_t{250});
		}

		menu::bindings::write(nodes::source0_transpose, ctx, 250);
		const deluge::instruments::classic::SoundState* const state = state_of(ctx);
		expect(state).not_().to_be_null();
		const std::vector<deluge::synth::SourceRange> ranges =
		    state != nullptr ? deluge::instruments::classic::source_ranges(*state, deluge::instruments::classic::SourceId::A)
		                     : std::vector<deluge::synth::SourceRange>{};
		if (state != nullptr) {
			expect(ranges.size()).to_equal(1);
		}
		if (!ranges.empty()) {
			// 250 cents up is legacy's 3 semitones less 50 cents (`computeFinalValuesForTranspose`).
			expect(static_cast<int32_t>(ranges[0].tuning.transpose)).to_equal(3);
			expect(static_cast<int32_t>(ranges[0].tuning.cents)).to_equal(-50);
		}
		expect(menu::bindings::read(nodes::source0_transpose, ctx)).to_equal(250);
	});

	it("transposes a whole kit drum by drum, each by its own source's kind", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* sampled = spec::add_drum<SoundDrum>(f);
		SoundDrum* synth = spec::add_drum<SoundDrum>(f);
		(void)spec::add_drum<MIDIDrum>(f);
		give_sample_range(*sampled);
		deluge::edits::settingFor(*synth, "osc1/type", Value::makeScalar("saw"));
		static_cast<Kit*>(f.instrument)->selectedDrum = sampled;
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		ctx.affect_entire_held = true;
		menu::bindings::write(nodes::source0_transpose, ctx, 300);
		const deluge::project::SlotAddress sampled_slot{deluge::project::SlotAddress::Kind::DRUM, *sampled->id()};
		const deluge::project::SlotAddress synth_slot{deluge::project::SlotAddress::Kind::DRUM, *synth->id()};
		const deluge::instruments::classic::SoundState* const a = state_at(sampled_slot);
		const deluge::instruments::classic::SoundState* const b = state_at(synth_slot);
		expect(a).not_().to_be_null();
		if (a != nullptr) {
			const std::vector<deluge::synth::SourceRange> ranges =
			    deluge::instruments::classic::source_ranges(*a, deluge::instruments::classic::SourceId::A);
			expect(ranges.empty()).to_be_false();
			if (!ranges.empty()) {
				expect(ranges[0].tuning.transpose).to_equal(3);
			}
		}
		expect(b).not_().to_be_null();
		if (b != nullptr) {
			expect(deluge::instruments::classic::source_tuning(*b, deluge::instruments::classic::SourceId::A).transpose).to_equal(3);
		}
	});

	it("writes interpolation as oscN/linearInterpolation through the sink", _ {
		spec::SongFixture f = spec::make_synth_song();
		give_sample_range(*static_cast<SoundInstrument*>(f.instrument));
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		bind::test::RecordingWrites recording;
		ctx.ports.writes = &recording;
		menu::bindings::write(nodes::sample0_interpolation, ctx, 0); // LINEAR
		expect(recording.requests.size()).to_equal(size_t{1});
		if (!recording.requests.empty()) {
			expect(recording.requests[0].target.path).to_equal("osc1/linearInterpolation");
		}
	});

	it("copies the song's stutter onto a sound set to 'use song'", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::changeEffect(*f.song, &f.song->live.master_channel, "stutter/quantized", Value::makeNumber(1));
		deluge::edits::changeEffect(*f.song, &f.song->live.master_channel, "stutter/reverse", Value::makeNumber(1));
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		bind::test::RecordingWrites recording;
		MenuContext recorded = ctx;
		recorded.ports.writes = &recording;
		menu::bindings::write(nodes::stutter_direction, recorded, 0); // "use song", offered on a sound
		to_be_at_least(expect(recording.requests.size()), size_t{3});

		menu::bindings::write(nodes::stutter_direction, ctx, 0);
		const deluge::instruments::classic::SoundState* const state = state_of(ctx);
		expect(state).not_().to_be_null();
		if (state != nullptr) {
			expect(deluge::instruments::classic::sound_effects(*state).stutter.use_song_stutter).to_be_true();
			expect(deluge::instruments::classic::sound_effects(*state).stutter.quantized).to_be_true();
			expect(deluge::instruments::classic::sound_effects(*state).stutter.reversed).to_be_true();
		}
	});

	it("refuses stutter quantize under affect-entire on a row that uses the song's stutter", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		deluge::edits::changeEffect(*f.song, drum, "stutter/useSongStutter", Value::makeNumber(1));
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		ctx.affect_entire_held = true;
		bind::test::RecordingWrites recording;
		ctx.ports.writes = &recording;
		menu::bindings::write(nodes::stutter_quantize, ctx, 1);
		expect(recording.requests.empty()).to_be_true();
	});

	it("writes the play direction to a kit row's note row, and else to the clip", _ {
		spec::SongFixture kit = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(kit);
		MenuContext on_row = spec::ctx_of(*kit.song, deluge::gui::menuViewOf(kit.clip), 0, false, false, false, false);
		bind::test::RecordingWrites recording;
		on_row.ports.writes = &recording;
		menu::bindings::write(nodes::sequence_direction, on_row, 2);
		expect(recording.requests.size()).to_equal(size_t{1});
		if (!recording.requests.empty()) {
			expect(recording.requests[0].target.group).to_equal(deluge::project::SettingGroupId::NOTE_ROW);
			expect(recording.requests[0].menu_value).to_equal(2);
		}

		spec::SongFixture synth = spec::make_synth_song();
		MenuContext on_clip = spec::ctx_of(*synth.song, deluge::gui::menuViewOf(synth.clip), 0, false, false, false, false);
		bind::test::RecordingWrites clip_recording;
		on_clip.ports.writes = &clip_recording;
		menu::bindings::write(nodes::sequence_direction, on_clip, 1);
		expect(clip_recording.requests.size()).to_equal(size_t{1});
		if (!clip_recording.requests.empty()) {
			expect(clip_recording.requests[0].target.group).to_equal(deluge::project::SettingGroupId::CLIP);
		}
	});

	it("hands a source's own transpose to the sound port with no range, and a sample source's with its range", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::settingFor(*static_cast<SoundInstrument*>(f.instrument), "osc1/type", Value::makeScalar("saw"));
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		bind::test::RecordingSoundWrites recording;
		ctx.ports.sound_writes = &recording;
		menu::bindings::write(nodes::source0_transpose, ctx, 100);
		expect(recording.calls.size()).to_equal(1);
		if (recording.calls.size() == 1) {
			expect(recording.calls[0].range.has_value()).to_be_false();
		}

		spec::SongFixture sampled = spec::make_synth_song();
		give_sample_range(*static_cast<SoundInstrument*>(sampled.instrument));
		MenuContext on_range = spec::ctx_of(*sampled.song, deluge::gui::menuViewOf(sampled.clip), 0, false, false, false, false);
		bind::test::RecordingSoundWrites range_recording;
		on_range.ports.sound_writes = &range_recording;
		menu::bindings::write(nodes::source0_transpose, on_range, 100);
		expect(range_recording.calls.size()).to_equal(1);
		if (range_recording.calls.size() == 1) {
			expect(range_recording.calls[0].range).to_equal(std::optional<uint16_t>{0});
		}
	});

	it("sets a whole kit's interpolation on each sound drum that takes it", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* sampled = spec::add_drum<SoundDrum>(f);
		SoundDrum* synth = spec::add_drum<SoundDrum>(f);
		(void)spec::add_drum<MIDIDrum>(f);
		give_sample_range(*sampled);
		give_sample_range(*synth);
		deluge::edits::settingFor(*synth, "osc1/type", Value::makeScalar("saw"));
		static_cast<Kit*>(f.instrument)->selectedDrum = sampled;
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		ctx.affect_entire_held = true;
		ctx.addresses.affect_entire = menu::bindings::writes_whole_kit(ctx); // as the menu scene keeps it
		const deluge::project::SlotAddress sampled_slot{deluge::project::SlotAddress::Kind::DRUM, *sampled->id()};
		const deluge::project::SlotAddress synth_slot{deluge::project::SlotAddress::Kind::DRUM, *synth->id()};
		expect(menu::bindings::read(nodes::sample0_interpolation, ctx)).to_equal(1); // SMOOTH, the default
		menu::bindings::write(nodes::sample0_interpolation, ctx, 0);                  // LINEAR
		const deluge::instruments::classic::SoundState* const a = state_at(sampled_slot);
		const deluge::instruments::classic::SoundState* const b = state_at(synth_slot);
		expect(a).not_().to_be_null();
		if (a != nullptr) {
			expect(deluge::instruments::classic::sample_playback(*a, deluge::instruments::classic::SourceId::A).interpolation).to_equal(deluge::plugin::InterpolationMode::LINEAR);
		}
		// A synth source takes no interpolation: legacy's per-drum setting write refuses it too.
		expect(b).not_().to_be_null();
		if (b != nullptr) {
			expect(deluge::instruments::classic::sample_playback(*b, deluge::instruments::classic::SourceId::A).interpolation).to_equal(deluge::plugin::InterpolationMode::SMOOTH);
		}
	});

	it("sets a whole kit's stutter direction on each sound drum", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* sampled = spec::add_drum<SoundDrum>(f);
		SoundDrum* synth = spec::add_drum<SoundDrum>(f);
		(void)spec::add_drum<MIDIDrum>(f);
		give_sample_range(*sampled);
		give_sample_range(*synth);
		deluge::edits::settingFor(*synth, "osc1/type", Value::makeScalar("saw"));
		static_cast<Kit*>(f.instrument)->selectedDrum = sampled;
		MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		ctx.affect_entire_held = true;
		ctx.addresses.affect_entire = menu::bindings::writes_whole_kit(ctx); // as the menu scene keeps it
		const deluge::project::SlotAddress sampled_slot{deluge::project::SlotAddress::Kind::DRUM, *sampled->id()};
		const deluge::project::SlotAddress synth_slot{deluge::project::SlotAddress::Kind::DRUM, *synth->id()};
		menu::bindings::write(nodes::stutter_direction, ctx, 2); // reversed
		const deluge::instruments::classic::SoundState* const a = state_at(sampled_slot);
		const deluge::instruments::classic::SoundState* const b = state_at(synth_slot);
		expect(a).not_().to_be_null();
		if (a != nullptr) {
			expect(deluge::instruments::classic::sound_effects(*a).stutter.reversed).to_be_true();
			expect(deluge::instruments::classic::sound_effects(*a).stutter.use_song_stutter).to_be_false();
		}
		expect(b).not_().to_be_null();
		if (b != nullptr) {
			expect(deluge::instruments::classic::sound_effects(*b).stutter.reversed).to_be_true();
			expect(deluge::instruments::classic::sound_effects(*b).stutter.use_song_stutter).to_be_false();
		}
	});

	it("offers 'use song' on a sound's stutter and not on the song's own", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext on_sound = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		const MenuContext on_song = spec::ctx_of(*f.song, deluge::gui::menuViewOf(nullptr), 0, false, true, true, false);
		std::string_view names[8]{};
		deluge::ui::shell::TextScratch scratch[8]{};
		expect(menu::bindings::names(nodes::stutter_direction, on_sound, names, scratch)).to_equal(size_t{5});
		expect(menu::bindings::names(nodes::stutter_direction, on_song, names, scratch)).to_equal(size_t{4});
	});
});

CPPSPEC_SPEC(sound_hooks)
