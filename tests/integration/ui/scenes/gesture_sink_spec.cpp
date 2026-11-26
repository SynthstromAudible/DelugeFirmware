// A scene's gestures go to the app's sink as typed requests, each applied as its Edit by address: a name is a text
// row of its record; a clear is one undo step; a recolour writes the clip's record; an audio clip's marker moves in
// its record; a source's ranges are its slot's; and a request on an address that does not resolve is refused.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/bind/target.h"
#include "ui/scenes/menu/context.h"

#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "project/setting_group.h"

#include "support/printers/sound_record_printer.h"
#include "support/slot_record.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "instruments/classic/state_access.h"

#include "support/menu_context.h"
#include "support/spec_main.h"

#include <optional>
#include <string>
#include <vector>

using namespace deluge::ui::scenes;
using deluge::synth::SourceRange;
using deluge::ui::scenes::bind::Target;
using deluge::ui::scenes::menu::MenuContext;
namespace params = deluge::modulation::params;

namespace {

constexpr Target name_of(deluge::project::SettingGroupId group) {
	return {.kind = Target::Kind::MODEL, .path = "name", .group = group};
}

/// The name @p group's record holds at @p where, or empty.
std::string recorded_name(Song& song, deluge::project::SettingGroupId group, const bind::Addresses& where) {
	const std::optional<deluge::project::SettingAddress> address = bind::setting_address(name_of(group), where);
	if (!address) {
		return {};
	}
	const std::optional<deluge::serialization::Value> value =
	    deluge::project::read_setting(song.project, *address, "name");
	return value ? value->str() : std::string{};
}

/// The audio clip's LPF frequency lane.
deluge::project::ParamAddress audio_lpf(const spec::AudioSongFixture& audio) {
	return {.owner = deluge::project::OwnerId::clip(audio.clip->id()),
	        .space = deluge::project::ParamSpace::ENGINE,
	        .id = deluge::bridge::paramIdOf(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ)};
}

} // namespace

// clang-format off
describe gesture_sink("a scene's gestures through the app's sink and its clip and sound ports", ${
	it("writes a track's, a clip's and a lane's name into the record", _ {
		spec::SongFixture synth = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = synth.song;
		const MenuContext on_synth = spec::ctx_of(*synth.song, deluge::gui::menuViewOf(synth.clip), 0, false, false, false, false);
		bind::WriteSink& sink = *deluge::gui::shell_host::host_ports().writes;
		using deluge::project::SettingGroupId;
		expect(sink.write_text(name_of(SettingGroupId::TRACK), on_synth.addresses, "BASSLINE")).to_be_true();
		expect(sink.write_text(name_of(SettingGroupId::CLIP), on_synth.addresses, "VERSE")).to_be_true();
		expect(recorded_name(*synth.song, SettingGroupId::TRACK, on_synth.addresses)).to_equal(std::string{"BASSLINE"});
		expect(recorded_name(*synth.song, SettingGroupId::CLIP, on_synth.addresses)).to_equal(std::string{"VERSE"});

		spec::SongFixture kit = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(kit);
		currentSong = kit.song;
		const MenuContext on_row = spec::ctx_of(*kit.song, deluge::gui::menuViewOf(kit.clip), 0, false, false, false, false);
		expect(sink.write_text(name_of(SettingGroupId::LANE), on_row.addresses, "KICK")).to_be_true();
		expect(recorded_name(*kit.song, SettingGroupId::LANE, on_row.addresses)).to_equal(std::string{"KICK"});
		currentSong = saved;
	});

	it("clears a clip's automation as one undo step, which restores it", _ {
		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		Song* const saved = currentSong;
		currentSong = audio.song;
		audio.song->setCurrentClip(audio.clip);
		const deluge::project::ParamAddress lpf = audio_lpf(audio);
		expect(deluge::gui::shell_host::host_ports().controls_sink->write_param(lpf, 0x20000000, 0, 24, deluge::ui::controls::Resolution::KNOB)).to_be_true();
		expect(audio.song->project.resolveParam(lpf)->nodes.empty()).to_be_false();
		changeRootUI(&sessionView); // an undo step opens only with a view as the current UI
		actionLogger.deleteAllLogs();
		expect(deluge::gui::shell_host::host_ports().clip_writes->clear_clip(deluge::project::ClipAddress{audio.clip->id()},
		                                                              /*clear_automation=*/true,
		                                                              /*clear_sequence_and_mpe=*/false))
		    .to_be_true();
		expect(audio.song->project.resolveParam(lpf)->nodes.empty()).to_be_true();
		Action* action = actionLogger.firstAction[BEFORE];
		expect(action).not_().to_be_null();
		if (action != nullptr) {
			expect(action->nextAction).to_be_null();
		}
		expect(actionLogger.revert(BEFORE, false, false)).to_be_true();
		expect(audio.song->project.resolveParam(lpf)->nodes.empty()).to_be_false();
		actionLogger.deleteAllLogs();
		nullifyUIs();
		currentSong = saved;
	});

	it("recolours a clip in its record", _ {
		spec::SongFixture synth = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = synth.song;
		const int16_t before = synth.clip->colourOffset();
		expect(deluge::gui::shell_host::host_ports().clip_writes->recolour_clip(deluge::project::ClipAddress{synth.clip->id()}, 5)).to_be_true();
		const deluge::project::DocClip* doc =
		    synth.song->project.resolveClip(deluge::project::ClipAddress{synth.clip->id()});
		expect(synth.clip->colourOffset()).to_equal(static_cast<int16_t>(before + 5));
		expect(doc).not_().to_be_null();
		if (doc != nullptr) {
			expect(doc->colour_offset).to_equal(static_cast<int16_t>(before + 5));
		}
		currentSong = saved;
	});

	it("moves an audio clip's marker in its record", _ {
		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		Song* const saved = currentSong;
		currentSong = audio.song;
		expect(deluge::gui::shell_host::host_ports().clip_writes->move_audio_clip_marker(deluge::project::ClipAddress{audio.clip->id()},
		                                                              "endSamplePos", 4321))
		    .to_be_true();
		const std::optional<deluge::serialization::Value> end = deluge::project::read_setting(
		    audio.song->project,
		    deluge::project::SettingAddress{.group = deluge::project::SettingGroupId::AUDIO_CLIP,
		                                    .owner = deluge::project::ClipAddress{audio.clip->id()}},
		    "endSamplePos");
		expect(end.has_value()).to_be_true();
		if (end.has_value()) {
			expect(end->str()).to_equal("4321");
		}
		currentSong = saved;
	});

	it("writes a source's ranges by its slot", _ {
		spec::SongFixture synth = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = synth.song;
		const deluge::project::SlotAddress slot{deluge::project::SlotAddress::Kind::OUTPUT, *synth.instrument->id()};
		auto& sound = *static_cast<SoundInstrument*>(synth.instrument);
		deluge::edits::settingFor(sound, "osc1/type", deluge::serialization::Value::makeScalar("sample"));
		(void)sound.getOrCreateFirstRange(0); // the range a zone editor makes, recorded as a gesture does
		deluge::edits::recordRanges(sound, 0);
		expect(deluge::gui::shell_host::host_ports().sound->sound_state(slot)).not_().to_be_null();
		std::vector<SourceRange> ranges = deluge::ui::scenes::spec::first_ranges(slot);
		expect(ranges.size()).to_equal(size_t(1));
		ranges[0].tuning.transpose = 7;
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_ranges(slot, 0, ranges, {})).to_be_true();
		const std::vector<SourceRange> after = deluge::ui::scenes::spec::first_ranges(slot);
		expect(after.empty()).to_be_false();
		if (!after.empty()) {
			expect(after[0].tuning.transpose).to_equal(7);
		}
		currentSong = saved;
	});

	it("takes a marker editor's ranges as an edit of the instrument", _ {
		spec::SongFixture synth = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = synth.song;
		const deluge::project::SlotAddress slot{deluge::project::SlotAddress::Kind::OUTPUT, *synth.instrument->id()};
		auto& sound = *static_cast<SoundInstrument*>(synth.instrument);
		deluge::edits::settingFor(sound, "osc1/type", deluge::serialization::Value::makeScalar("sample"));
		(void)sound.getOrCreateFirstRange(0);
		deluge::edits::recordRanges(sound, 0);
		std::vector<SourceRange> ranges = deluge::ui::scenes::spec::first_ranges(slot);
		ranges[0].zone.start = 100;
		synth.instrument->editedByUser = false;
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_ranges(slot, 0, ranges,
		                                            {.kind = bind::RangesCause::Kind::MARKER,
		                                             .marker = static_cast<int8_t>(MarkerType::START)}))
		    .to_be_true();
		expect(synth.instrument->editedByUser).to_be_true();
		expect(deluge::ui::scenes::spec::first_ranges(slot)[0].zone.start).to_equal(uint64_t{100});
		currentSong = saved;
	});

	it("makes a source's first range by its slot", _ {
		spec::SongFixture synth = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = synth.song;
		const deluge::project::SlotAddress slot{deluge::project::SlotAddress::Kind::OUTPUT, *synth.instrument->id()};
		auto& sound = *static_cast<SoundInstrument*>(synth.instrument);
		deluge::edits::settingFor(sound, "osc1/type", deluge::serialization::Value::makeScalar("sample"));
		expect(deluge::ui::scenes::spec::first_ranges(slot).empty()).to_be_true();
		const std::vector<SourceRange> first{
		    {.top_note = deluge::instruments::classic::kOpenTopNote, .zone = {.end = deluge::instruments::classic::kUnsetEndPos}}};
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_ranges(slot, 0, first, {})).to_be_true();
		expect(deluge::ui::scenes::spec::first_ranges(slot).size()).to_equal(size_t{1});
		expect(std::ssize(sound.sourceResources[0].ranges)).to_equal(1);
		// The range is the one a source makes for itself, as legacy's getOrCreateFirstRange leaves it.
		deluge::edits::settingFor(sound, "osc2/type", deluge::serialization::Value::makeScalar("sample"));
		(void)sound.getOrCreateFirstRange(1);
		const std::vector<SourceRange> legacy = deluge::bridge::rangesOf(sound.sourceResources[1]);
		const std::vector<SourceRange> recorded =
		    deluge::ui::scenes::spec::first_ranges(slot);
		expect(legacy.empty()).to_be_false();
		if (!legacy.empty()) {
			expect(recorded[0]).to_equal(legacy[0]);
		}
		expect(static_cast<int64_t>(recorded[0].zone.end)).to_equal(static_cast<int64_t>(legacy[0].zone.end));
		currentSong = saved;
	});

	it("refuses each request on an address that does not resolve", _ {
		spec::SongFixture synth = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = synth.song;
		bind::WriteSink& sink = *deluge::gui::shell_host::host_ports().writes;
		const deluge::project::ClipAddress nowhere{synth.clip->id() + 1000};
		bind::Addresses lost = spec::ctx_of(*synth.song, deluge::gui::menuViewOf(synth.clip), 0, false, false, false, false).addresses;
		lost.clip = nowhere;
		expect(sink.write_text(name_of(deluge::project::SettingGroupId::CLIP), lost, "X")).to_be_false();
		bind::ClipWrites& clip_writes = *deluge::gui::shell_host::host_ports().clip_writes;
		expect(clip_writes.clear_clip(nowhere, true, true)).to_be_false();
		expect(clip_writes.recolour_clip(nowhere, 1)).to_be_false();
		expect(clip_writes.move_audio_clip_marker(nowhere, "endSamplePos", 1)).to_be_false();
		bind::SoundWrites& sound_writes = *deluge::gui::shell_host::host_ports().sound_writes;
		const deluge::project::SlotAddress lost_slot{deluge::project::SlotAddress::Kind::OUTPUT,
		                                             *synth.instrument->id() + 1000};
		expect(sound_writes.write_ranges(lost_slot, 0, {}, {})).to_be_false();
		currentSong = saved;
	});
});

CPPSPEC_SPEC(gesture_sink)
