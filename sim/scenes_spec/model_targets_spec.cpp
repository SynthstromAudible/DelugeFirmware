// A model target's write through the app's sink reaches the record its menu's root names: the song's, a track's
// device, a clip's values, an audio clip's, a kit row's note row, a clip's arp or a kit lane's; and the root fills the
// addresses and the instrument the target resolves against. The host's facts answer as the engine does.

#include "gui/ui/menu_view_of.h"
#include "scenes/bind/dispatch.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "deluge-project/setting_group.h"
#include "engine-api/types/setting.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/midi_drum.h"
#include "model/edits/edit.h"
#include "model/edits/set_setting.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "storage/flash_storage.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <string>

using namespace deluge::scenes;
using deluge::engine::SettingContext;
using deluge::project::SettingAddress;
using deluge::project::SettingGroupId;
using deluge::scenes::bind::Target;
using deluge::scenes::menu::MenuContext;
using deluge::serialization::Value;

namespace {

Target model(SettingGroupId group, std::string_view path) {
	return {.kind = Target::Kind::MODEL, .path = path, .group = group};
}

/// Writes menu value @p menu of @p group's row @p path through the app's sink at @p ctx's addresses.
void write(const MenuContext& ctx, SettingGroupId group, std::string_view path, int32_t menu) {
	menu::host_write_sink().write({.target = model(group, path), .where = ctx.addresses, .menu_value = menu});
}

/// The record's value at @p path, as text, or "none".
std::string recorded(Song& song, const SettingAddress& address, std::string_view path) {
	const auto value = deluge::project::read_setting(song.project, address, path);
	return value ? value->str() : "none";
}

/// The text the row stores for menu value @p menu.
std::string stored_text(SettingGroupId group, std::string_view path, int32_t menu) {
	const auto& row = *deluge::engine::find_setting(deluge::project::table_of(group), path);
	const auto stored = deluge::engine::from_menu(row, menu);
	std::array<char, 16> scratch{};
	if (row.kind == deluge::engine::SettingKind::CHOICE) {
		return std::string{deluge::engine::stored_text(row, stored->first, scratch).value_or("?")};
	}
	return std::to_string(stored->first);
}

MenuContext song_root(Song& song) {
	return menu::context_for(song, deluge::gui::menuViewOf(nullptr), 0, false, true, true, false);
}

MenuContext clip_root(Song& song, Clip* clip) {
	return menu::context_for(song, deluge::gui::menuViewOf(clip), 0, false, false, false, false);
}

} // namespace

// clang-format off
describe model_targets("a model target's write reaches the record its menu names", ${
	it("fills a synth clip's addresses and what plays it", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext ctx = clip_root(*f.song, f.clip);
		expect(ctx.addresses.clip.has_value() && ctx.addresses.clip->clip_id == f.clip->id()).to_be_true();
		expect(ctx.addresses.track.has_value() && ctx.addresses.track->output_id == *f.instrument->id()).to_be_true();
		expect(ctx.addresses.instrument).to_equal(SettingContext::Instrument::SYNTH);
		expect(ctx.addresses.lane.has_value()).to_be_false();
	});

	it("fills a kit row's lane and note row", _ {
		spec::SongFixture f = spec::make_kit_song();
		auto* drum = spec::add_drum<SoundDrum>(f);
		const MenuContext ctx = clip_root(*f.song, f.clip);
		expect(ctx.addresses.lane.has_value() && ctx.addresses.lane->drum_id == *drum->id()
		       && ctx.addresses.lane->kit_id == *f.instrument->id())
		    .to_be_true();
		expect(ctx.addresses.note_row.has_value() && ctx.addresses.note_row->is_kit
		       && ctx.addresses.note_row->row_key == static_cast<int32_t>(*drum->id()))
		    .to_be_true();
		expect(ctx.addresses.instrument).to_equal(SettingContext::Instrument::KIT);
	});

	it("writes the song's reverb, and the reverb re-reads it", _ {
		spec::SongFixture f = spec::make_synth_song();
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
		write(song_root(*f.song), SettingGroupId::SONG, "reverb/roomSize", 25);
		expect(recorded(*f.song, {SettingGroupId::SONG, deluge::project::SongAddress{}}, "reverb/roomSize"))
		    .to_equal(stored_text(SettingGroupId::SONG, "reverb/roomSize", 25));
		expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender).to_be_true();
	});

	it("writes a CV track's second source onto the record and the instrument", _ {
		spec::SongFixture f = spec::make_cv_song();
		const MenuContext ctx = clip_root(*f.song, f.clip);
		write(ctx, SettingGroupId::INSTRUMENT_DEVICE, "cv2Source", 2);
		const std::string text = stored_text(SettingGroupId::INSTRUMENT_DEVICE, "cv2Source", 2);
		expect(recorded(*f.song, {SettingGroupId::INSTRUMENT_DEVICE, *ctx.addresses.track}, "cv2Source")).to_equal(text);
		expect(std::to_string(static_cast<int>(static_cast<CVInstrument*>(f.instrument)->getCV2Mode()))).to_equal(text);
	});

	it("writes a MIDI clip's program", _ {
		spec::SongFixture f = spec::make_midi_song();
		const MenuContext ctx = clip_root(*f.song, f.clip);
		write(ctx, SettingGroupId::CLIP_VALUES, "midiPGM", 17);
		expect(recorded(*f.song, {SettingGroupId::CLIP_VALUES, *ctx.addresses.clip}, "midiPGM")).to_equal("17");
	});

	it("writes an audio clip's reverse", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const MenuContext ctx = clip_root(*f.song, f.clip);
		expect(ctx.addresses.instrument).to_equal(SettingContext::Instrument::AUDIO);
		write(ctx, SettingGroupId::AUDIO_CLIP, "reversed", 1);
		expect(recorded(*f.song, {SettingGroupId::AUDIO_CLIP, *ctx.addresses.clip}, "reversed")).to_equal("1");
		expect(f.clip->sampleControls.reversed).to_be_true();
	});

	it("writes a sound setting an audio clip also has onto the audio clip's own row", _ {
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		const MenuContext ctx = clip_root(*f.song, f.clip);
		const Target priority{.kind = Target::Kind::SOUND, .path = "voicePriority", .audio_clip_path = "priority"};
		menu::host_write_sink().write({.target = priority, .where = ctx.addresses, .menu_value = 2});
		expect(recorded(*f.song, {SettingGroupId::AUDIO_CLIP, *ctx.addresses.clip}, "priority"))
		    .to_equal(stored_text(SettingGroupId::AUDIO_CLIP, "priority", 2));
	});

	it("writes a kit row's bend range on its note row", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		const MenuContext ctx = clip_root(*f.song, f.clip);
		write(ctx, SettingGroupId::NOTE_ROW, "bendRange", 9);
		expect(recorded(*f.song, {SettingGroupId::NOTE_ROW, *ctx.addresses.note_row}, "bendRange")).to_equal("9");
	});

	it("writes a synth clip's arp on its clip", _ {
		spec::SongFixture f = spec::make_synth_song();
		const MenuContext ctx = clip_root(*f.song, f.clip);
		write(ctx, SettingGroupId::CLIP_ARP, "arpeggiator/numOctaves", 3);
		expect(recorded(*f.song, {SettingGroupId::CLIP_ARP, *ctx.addresses.clip}, "arpeggiator/numOctaves"))
		    .to_equal(stored_text(SettingGroupId::CLIP_ARP, "arpeggiator/numOctaves", 3));
		expect(static_cast<int32_t>(f.clip->arpSettings.numOctaves))
		    .to_equal(std::stoi(stored_text(SettingGroupId::CLIP_ARP, "arpeggiator/numOctaves", 3)));
	});

	it("writes a kit row's arp on its lane, leaving the kit clip's", _ {
		spec::SongFixture f = spec::make_kit_song();
		auto* drum = spec::add_drum<SoundDrum>(f);
		const MenuContext ctx = clip_root(*f.song, f.clip);
		const std::string kit_before =
		    recorded(*f.song, {SettingGroupId::CLIP_ARP, *ctx.addresses.clip}, "arpeggiator/numOctaves");
		write(ctx, SettingGroupId::CLIP_ARP, "arpeggiator/numOctaves", 4);
		expect(recorded(*f.song, {SettingGroupId::LANE_ARP, *ctx.addresses.lane}, "arpeggiator/numOctaves"))
		    .to_equal(stored_text(SettingGroupId::LANE_ARP, "arpeggiator/numOctaves", 4));
		expect(static_cast<int32_t>(drum->arpSettings.numOctaves))
		    .to_equal(std::stoi(stored_text(SettingGroupId::LANE_ARP, "arpeggiator/numOctaves", 4)));
		expect(recorded(*f.song, {SettingGroupId::CLIP_ARP, *ctx.addresses.clip}, "arpeggiator/numOctaves"))
		    .to_equal(kit_before);
	});

	it("writes a kit row's arp across every lane that takes it, with affect-entire held", _ {
		spec::SongFixture f = spec::make_kit_song();
		auto* sound = spec::add_drum<SoundDrum>(f);
		auto* midi = spec::add_drum<MIDIDrum>(f);
		MenuContext ctx = clip_root(*f.song, f.clip);
		ctx.addresses.affect_entire = true;
		write(ctx, SettingGroupId::CLIP_ARP, "arpeggiator/numOctaves", 4);
		const std::string want = stored_text(SettingGroupId::LANE_ARP, "arpeggiator/numOctaves", 4);
		for (Drum* drum : std::initializer_list<Drum*>{sound, midi}) {
			const deluge::project::LaneAddress lane{*f.instrument->id(), *drum->id()};
			expect(recorded(*f.song, {SettingGroupId::LANE_ARP, lane}, "arpeggiator/numOctaves")).to_equal(want);
		}
	});

	it("seeds a lane's arp record from its drum before writing one of its settings", _ {
		spec::SongFixture f = spec::make_kit_song();
		auto* drum = spec::add_drum<MIDIDrum>(f);
		drum->docDrum_->arp.reset(); // as a lane the song never recorded an arp for
		drum->arpSettings.numOctaves = 3;
		const MenuContext ctx = clip_root(*f.song, f.clip);
		write(ctx, SettingGroupId::CLIP_ARP, "arpeggiator/stepRepeat", 2);
		expect(recorded(*f.song, {SettingGroupId::LANE_ARP, *ctx.addresses.lane}, "arpeggiator/numOctaves"))
		    .to_equal("3");
	});

	it("answers a clip's played bend ranges, a kit row's note tails and an arp's preset as the engine holds them", _ {
		const bind::RuntimeFacts& facts = menu::host_runtime_facts();
		spec::SongFixture midi = spec::make_midi_song();
		const MenuContext on_midi = clip_root(*midi.song, midi.clip);
		const SettingAddress values{SettingGroupId::CLIP_VALUES, *on_midi.addresses.clip};
		auto& params = static_cast<InstrumentClip*>(midi.clip)->paramManager;
		if (params.getExpressionParamSet() == nullptr) {
			expect(facts.played_bend_range(values, 0)).to_equal(static_cast<int32_t>(FlashStorage::defaultBendRange[0]));
			expect(facts.played_bend_range(values, 1)).to_equal(static_cast<int32_t>(FlashStorage::defaultBendRange[1]));
		}
		ExpressionParamSet* expression = params.getOrCreateExpressionParamSet();
		expression->bendRanges[BEND_RANGE_MAIN] = 3;
		expression->bendRanges[BEND_RANGE_FINGER_LEVEL] = 33;
		expect(facts.played_bend_range(values, 0)).to_equal(3);
		expect(facts.played_bend_range(values, 1)).to_equal(33);

		spec::SongFixture kit = spec::make_kit_song();
		auto* drum = spec::add_drum<SoundDrum>(kit);
		const MenuContext row = clip_root(*kit.song, kit.clip);
		char drum_memory[MODEL_STACK_MAX_SIZE];
		expect(facts.allows_note_tails(*row.addresses.slot))
		    .to_equal(drum->allowNoteTails(getModelStackFromSoundDrum(drum_memory, drum)->addSoundFlags(),
		                                   /*disregardSampleLoop=*/true));
		drum->arpSettings.mode = ArpMode::ARP;
		expect(facts.allows_note_tails(*row.addresses.slot)).to_be_true(); // an arp holds its notes

		spec::SongFixture synth = spec::make_synth_song();
		const MenuContext ctx = clip_root(*synth.song, synth.clip);
		const SettingAddress arp{SettingGroupId::CLIP_ARP, *ctx.addresses.clip};
		deluge::edits::Context edits{*synth.song, synth.song->project};
		deluge::edits::SetSetting custom{arp, "arpeggiator/preset", Value::makeScalar("custom")};
		expect(deluge::edits::apply(edits, custom) == Error::NONE).to_be_true();
		expect(static_cast<int>(facts.arp_preset(arp))).to_equal(6);
	});
});

CPPSPEC_SPEC(model_targets)
