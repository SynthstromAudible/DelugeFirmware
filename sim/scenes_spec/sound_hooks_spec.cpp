// The sound menu's hook-built items write by address: an oscillator's transpose (a sample source's range, else the
// source's own setting, each sound drum of a whole kit by its own kind), the sample interpolation, the stutter's
// direction (inheriting the song's stutter on "use song") and quantize, and the play direction (a kit row's note row,
// else the clip). Each is a request to the menu's sink; with the app's sink, the record holds the result.

#include "gui/ui/menu_view_of.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/nodes/sound_actions.h"
#include "scenes/menu/nodes/sound_fx.h"
#include "scenes/menu/nodes/sound_osc.h"

#include "deluge-project/setting_group.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <optional>
#include <string>
#include <vector>

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
namespace nodes = deluge::scenes::menu::nodes;
using deluge::serialization::Value;

namespace {

/// @brief Make source 0 of @p sound a sample source with one range, recorded as a gesture records it.
void give_sample_range(::Sound& sound) {
	deluge::edits::settingFor(sound, "osc1/type", Value::makeScalar("sample"));
	(void)sound.sources[0].getOrCreateFirstRange();
	deluge::edits::recordRanges(sound, 0);
}

/// @brief The sound's state, by its slot.
const deluge::instrument::SoundState* state_of(const MenuContext& ctx) {
	return ctx.addresses.slot ? menu::host_runtime_facts().sound_state(*ctx.addresses.slot) : nullptr;
}

/// @brief The sound at @p slot.
const deluge::instrument::SoundState* state_at(deluge::project::SlotAddress slot) {
	return menu::host_runtime_facts().sound_state(slot);
}

} // namespace

// clang-format off
describe sound_hooks("the sound menu's hook-built items, by address", ${
	it("hands an osc transpose to the sink, and a sample source's lands in its range", _ {
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		give_sample_range(sound);
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		bind::RecordingSink recording;
		MenuContext recorded = ctx;
		recorded.sink = &recording;
		menu::bindings::write(nodes::source0_transpose, recorded, 250);
		expect(recording.gestures.size()).to_equal(size_t{1});
		if (!recording.gestures.empty()) {
			expect(recording.gestures[0].kind == bind::GestureRequest::Kind::TRANSPOSE).to_be_true();
			expect(recording.gestures[0].value).to_equal(int64_t{250});
		}

		menu::bindings::write(nodes::source0_transpose, ctx, 250);
		const deluge::instrument::SoundState* state = state_of(ctx);
		expect(state != nullptr && state->sources[0].ranges.size() == 1).to_be_true();
		if (state != nullptr && !state->sources[0].ranges.empty()) {
			// 250 cents up is legacy's 3 semitones less 50 cents (`computeFinalValuesForTranspose`).
			expect(static_cast<int32_t>(state->sources[0].ranges[0].transpose)).to_equal(3);
			expect(static_cast<int32_t>(state->sources[0].ranges[0].cents)).to_equal(-50);
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
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		ctx.affect_entire_held = true;
		menu::bindings::write(nodes::source0_transpose, ctx, 300);
		const deluge::project::SlotAddress sampled_slot{deluge::project::SlotAddress::Kind::DRUM, *sampled->id()};
		const deluge::project::SlotAddress synth_slot{deluge::project::SlotAddress::Kind::DRUM, *synth->id()};
		const deluge::instrument::SoundState* a = state_at(sampled_slot);
		const deluge::instrument::SoundState* b = state_at(synth_slot);
		expect(a != nullptr && !a->sources[0].ranges.empty() && a->sources[0].ranges[0].transpose == 3).to_be_true();
		expect(b != nullptr && b->sources[0].transpose == 3).to_be_true();
	});

	it("writes interpolation as oscN/linearInterpolation through the sink", _ {
		spec::SongFixture f = spec::make_synth_song();
		give_sample_range(*static_cast<SoundInstrument*>(f.instrument));
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		bind::RecordingSink recording;
		ctx.sink = &recording;
		menu::bindings::write(nodes::sample0_interpolation, ctx, 0); // LINEAR
		expect(recording.requests.size()).to_equal(size_t{1});
		if (!recording.requests.empty()) {
			expect(recording.requests[0].target.path == "osc1/linearInterpolation").to_be_true();
		}
	});

	it("copies the song's stutter onto a sound set to 'use song'", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::changeEffect(*f.song, f.song->live.global_effectable, "stutter/quantized", Value::makeNumber(1));
		deluge::edits::changeEffect(*f.song, f.song->live.global_effectable, "stutter/reverse", Value::makeNumber(1));
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		bind::RecordingSink recording;
		MenuContext recorded = ctx;
		recorded.sink = &recording;
		menu::bindings::write(nodes::stutter_direction, recorded, 0); // "use song", offered on a sound
		expect(recording.requests.size() >= size_t{3}).to_be_true();

		menu::bindings::write(nodes::stutter_direction, ctx, 0);
		const deluge::instrument::SoundState* state = state_of(ctx);
		expect(state != nullptr && state->effects.stutter.use_song_stutter).to_be_true();
		expect(state != nullptr && state->effects.stutter.quantized && state->effects.stutter.reversed).to_be_true();
	});

	it("refuses stutter quantize under affect-entire on a row that uses the song's stutter", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		deluge::edits::changeEffect(*f.song, *drum, "stutter/useSongStutter", Value::makeNumber(1));
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		ctx.affect_entire_held = true;
		bind::RecordingSink recording;
		ctx.sink = &recording;
		menu::bindings::write(nodes::stutter_quantize, ctx, 1);
		expect(recording.requests.empty()).to_be_true();
	});

	it("writes the play direction to a kit row's note row, and else to the clip", _ {
		spec::SongFixture kit = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(kit);
		MenuContext on_row = menu::context_for(*kit.song, deluge::gui::menuViewOf(*kit.song, kit.clip), 0, false, false, false, false);
		bind::RecordingSink recording;
		on_row.sink = &recording;
		menu::bindings::write(nodes::sequence_direction, on_row, 2);
		expect(recording.requests.size()).to_equal(size_t{1});
		if (!recording.requests.empty()) {
			expect(recording.requests[0].target.group == deluge::project::SettingGroupId::NOTE_ROW).to_be_true();
			expect(recording.requests[0].menu_value).to_equal(2);
		}

		spec::SongFixture synth = spec::make_synth_song();
		MenuContext on_clip = menu::context_for(*synth.song, deluge::gui::menuViewOf(*synth.song, synth.clip), 0, false, false, false, false);
		bind::RecordingSink clip_recording;
		on_clip.sink = &clip_recording;
		menu::bindings::write(nodes::sequence_direction, on_clip, 1);
		expect(clip_recording.requests.size()).to_equal(size_t{1});
		if (!clip_recording.requests.empty()) {
			expect(clip_recording.requests[0].target.group == deluge::project::SettingGroupId::CLIP).to_be_true();
		}
	});

	it("hands a source's own transpose to the sink with no range, and a sample source's with its range", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::settingFor(*static_cast<SoundInstrument*>(f.instrument), "osc1/type", Value::makeScalar("saw"));
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		bind::RecordingSink recording;
		ctx.sink = &recording;
		menu::bindings::write(nodes::source0_transpose, ctx, 100);
		expect(recording.gestures.size() == 1 && !recording.gestures[0].range.has_value()).to_be_true();

		spec::SongFixture sampled = spec::make_synth_song();
		give_sample_range(*static_cast<SoundInstrument*>(sampled.instrument));
		MenuContext on_range = menu::context_for(*sampled.song, deluge::gui::menuViewOf(*sampled.song, sampled.clip), 0, false, false, false, false);
		bind::RecordingSink range_recording;
		on_range.sink = &range_recording;
		menu::bindings::write(nodes::source0_transpose, on_range, 100);
		expect(range_recording.gestures.size() == 1
		       && range_recording.gestures[0].range == std::optional<uint16_t>{0})
		    .to_be_true();
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
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		ctx.affect_entire_held = true;
		ctx.addresses.affect_entire = menu::bindings::writes_whole_kit(ctx); // as the menu scene keeps it
		const deluge::project::SlotAddress sampled_slot{deluge::project::SlotAddress::Kind::DRUM, *sampled->id()};
		const deluge::project::SlotAddress synth_slot{deluge::project::SlotAddress::Kind::DRUM, *synth->id()};
		expect(menu::bindings::read(nodes::sample0_interpolation, ctx)).to_equal(1); // SMOOTH, the default
		menu::bindings::write(nodes::sample0_interpolation, ctx, 0);                  // LINEAR
		const deluge::instrument::SoundState* a = state_at(sampled_slot);
		const deluge::instrument::SoundState* b = state_at(synth_slot);
		expect(a != nullptr && a->sources[0].interpolation_mode == deluge::engine::InterpolationMode::LINEAR)
		    .to_be_true();
		// A synth source takes no interpolation: legacy's per-drum setting write refuses it too.
		expect(b != nullptr && b->sources[0].interpolation_mode == deluge::engine::InterpolationMode::SMOOTH)
		    .to_be_true();
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
		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		ctx.affect_entire_held = true;
		ctx.addresses.affect_entire = menu::bindings::writes_whole_kit(ctx); // as the menu scene keeps it
		const deluge::project::SlotAddress sampled_slot{deluge::project::SlotAddress::Kind::DRUM, *sampled->id()};
		const deluge::project::SlotAddress synth_slot{deluge::project::SlotAddress::Kind::DRUM, *synth->id()};
		menu::bindings::write(nodes::stutter_direction, ctx, 2); // reversed
		const deluge::instrument::SoundState* a = state_at(sampled_slot);
		const deluge::instrument::SoundState* b = state_at(synth_slot);
		expect(a != nullptr && a->effects.stutter.reversed && !a->effects.stutter.use_song_stutter).to_be_true();
		expect(b != nullptr && b->effects.stutter.reversed && !b->effects.stutter.use_song_stutter).to_be_true();
	});

	it("offers 'use song' on a sound's stutter and not on the song's own", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext on_sound = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, f.clip), 0, false, false, false, false);
		const MenuContext on_song = menu::context_for(*f.song, deluge::gui::menuViewOf(*f.song, nullptr), 0, false, true, true, false);
		std::string_view names[8]{};
		deluge::ui::shell::TextScratch scratch[8]{};
		expect(menu::bindings::names(nodes::stutter_direction, on_sound, names, scratch)).to_equal(size_t{5});
		expect(menu::bindings::names(nodes::stutter_direction, on_song, names, scratch)).to_equal(size_t{4});
	});
});

CPPSPEC_SPEC(sound_hooks)
