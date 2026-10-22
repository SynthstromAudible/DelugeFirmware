// A menu's own questions -- which items apply, how many ranges a source has and which one the picker starts on, which
// file the sample browser opens on, where a kit row's play direction lives -- are answered from the record, the
// runtime facts and the scenes' view state, never from the engine's objects.

#include "scenes/bind/write_sink.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/multi_range.h"
#include "scenes/menu/nodes/sound_actions.h"
#include "scenes/menu/relevance.h"
#include "scenes/services/view_state.h"

#include "deluge-project/setting_group.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/multi_range/multi_range.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <string>

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
namespace nodes = deluge::scenes::menu::nodes;
using deluge::serialization::Value;

namespace {

/// @brief Give source @p s of @p sound three sample ranges, recorded as a gesture records them.
void give_three_ranges(::Sound& sound, int32_t s) {
	deluge::edits::settingFor(sound, s == 0 ? "osc1/type" : "osc2/type", Value::makeScalar("sample"));
	(void)sound.sources[s].getOrCreateFirstRange();
	sound.sources[s].ranges.getElement(0)->topNote = 40;
	sound.sources[s].ranges.insertMultiRange(1)->topNote = 60;
	sound.sources[s].ranges.insertMultiRange(2)->topNote = 32767;
	deluge::edits::recordRanges(sound, s);
}

/// @brief The cursor the range picker opens on over @p ctx.
int32_t picker_cursor(const MenuContext& ctx) {
	menu::MenuState state{};
	nodes::kMultiRangeItem.on_focus(ctx, state);
	return state.range_choice;
}

} // namespace

// clang-format off
describe menu_records("a menu's questions, answered from the record", ${
	it("opens the range picker on the range last chosen for that source, and on the middle one for a new source", _ {
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		give_three_ranges(sound, 0);
		give_three_ranges(sound, 1);
		MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		services::view_state().forget_unless(f.song->project.identity());
		expect(menu::range_count(ctx)).to_equal(3);
		expect(menu::range_top_note(ctx, 1)).to_equal(60);
		menu::select_range(ctx, 2);
		expect(services::view_state().last_range(*ctx.addresses.slot, 0)).to_equal(std::optional<uint16_t>{2});
		const MenuContext again = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		expect(picker_cursor(again)).to_equal(2);
		const MenuContext other = menu::context_for(*f.song, f.clip, 1, false, false, false, false);
		expect(picker_cursor(other)).to_equal(1);
	});

	it("opens the range picker on the middle range when the one remembered is past the end", _ {
		spec::SongFixture f = spec::make_synth_song();
		give_three_ranges(*static_cast<SoundInstrument*>(f.instrument), 0);
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		services::view_state().forget_unless(f.song->project.identity());
		services::view_state().set_last_range(*ctx.addresses.slot, 0, 7);
		expect(picker_cursor(ctx)).to_equal(1);
	});

	it("opens the sample browser on the range's recorded file", _ {
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		deluge::edits::settingFor(sound, "osc1/type", Value::makeScalar("sample"));
		auto* range = sound.sources[0].getOrCreateFirstRange();
		range->getAudioFileHolder()->filePath = "SAMPLES/KICKS/K1.WAV";
		deluge::edits::recordRanges(sound, 0);
		MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		std::string dir;
		std::string name;
		browser::descriptor(browser::BrowserId::Sample).start_at(ctx, dir, name);
		const deluge::instrument::SoundState* state = menu::host_runtime_facts().sound_state(*ctx.addresses.slot);
		expect(state != nullptr && !state->sources[0].ranges.empty()).to_be_true();
		const std::string recorded = state != nullptr && !state->sources[0].ranges.empty()
		                                 ? state->sources[0].ranges[0].file_path
		                                 : std::string{};
		expect(recorded.empty()).to_be_false();
		expect(dir + "/" + name).to_equal(recorded);
	});

	it("answers each sound predicate from the record", _ {
		spec::SongFixture f = spec::make_synth_song();
		auto& sound = *static_cast<SoundInstrument*>(f.instrument);
		const MenuContext subtractive = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		expect(menu::relevance::is_subtractive(subtractive)).to_be_true();
		expect(menu::relevance::is_fm(subtractive)).to_be_false();
		expect(menu::relevance::osc0_file_selector(subtractive)).to_be_true();
		expect(menu::relevance::bend_submenu(subtractive)).to_be_true();
		expect(menu::relevance::is_midi_out(subtractive)).to_be_false();
		expect(menu::relevance::stutter_quantize_allowed(subtractive)).to_equal(
		    !sound.effects().stutter.use_song_stutter);
		deluge::edits::settingFor(sound, "mode", Value::makeScalar("fm"));
		const MenuContext fm = menu::context_for(*f.song, f.clip, 0, false, false, false, false);
		expect(menu::relevance::is_fm(fm)).to_be_true();
		expect(menu::relevance::is_subtractive(fm)).to_be_false();

		spec::SongFixture midi = spec::make_midi_song();
		const MenuContext on_midi =
		    menu::context_for(*midi.song, midi.clip, 0, false, false, false, false);
		expect(menu::relevance::is_midi_out(on_midi)).to_be_true();
	});

	it("writes a MIDI drum row's play direction to its note row", _ {
		spec::SongFixture kit = spec::make_kit_song();
		(void)spec::add_drum<MIDIDrum>(kit);
		MenuContext ctx = menu::context_for(*kit.song, kit.clip, 0, false, false, false, false);
		bind::RecordingSink recording;
		ctx.sink = &recording;
		menu::bindings::write(nodes::sequence_direction, ctx, 1);
		expect(recording.requests.size()).to_equal(size_t{1});
		if (!recording.requests.empty()) {
			expect(recording.requests[0].target.group == deluge::project::SettingGroupId::NOTE_ROW).to_be_true();
		}
	});

	it("reads and writes nothing of a song the app does not play, on every node of every clip root", _ {
		// The menus' sink and facts act on the song the app plays: over any other song every node finds nothing to
		// edit, and a write changes nothing of it.
		spec::SongFixture synth = spec::make_synth_song();
		spec::SongFixture kit = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(kit);
		spec::SongFixture midi = spec::make_midi_song();
		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		spec::SongFixture playing = spec::make_synth_song(); // the song the app plays: none of the above
		currentSong = playing.song;

		struct Case {
			Song* song;
			Clip* clip;
		};
		const Case cases[] = {{synth.song, synth.clip}, {kit.song, kit.clip}, {midi.song, midi.clip},
		                      {audio.song, audio.clip}};
		size_t written = 0;
		for (const Case& c : cases) {
			const MenuContext ctx = menu::context_for(*c.song, c.clip, 0, false, false, false, false);
			expect(ctx.facts == nullptr && ctx.sink == nullptr).to_be_true();
			const deluge::project::DocClip before =
			    *c.song->project.resolveClip(deluge::project::ClipAddress{c.clip->id()});
			const auto swing = c.song->project.groove.swing_interval;
			std::vector<const menu::Node*> stack{menu::MenuScene::roots().begin(), menu::MenuScene::roots().end()};
			while (!stack.empty()) {
				const menu::Node* node = stack.back();
				stack.pop_back();
				if (node == nullptr || node == menu::MenuScene::roots()[static_cast<size_t>(MenuContext::Root::Settings)]) {
					continue;
				}
				for (const menu::Node* child : node->children) {
					stack.push_back(child);
				}
				if (node->bind.target.valid() || node->bind.param.valid() || node->bind.set != nullptr) {
					const menu::IntRange range = menu::bindings::range(*node, ctx);
					const int32_t now = menu::bindings::read(*node, ctx);
					menu::bindings::write(*node, ctx, now < range.max ? now + 1 : range.min);
					written++;
				}
			}
			expect(*c.song->project.resolveClip(deluge::project::ClipAddress{c.clip->id()}) == before).to_be_true();
			expect(c.song->project.groove.swing_interval == swing).to_be_true();
		}
		expect(written > size_t{100}).to_be_true();
		currentSong = playing.song;
	});
});

CPPSPEC_SPEC(menu_records)
