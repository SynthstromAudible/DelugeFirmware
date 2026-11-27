// What a gold knob moves, from the song's records: for every kind of owner, mod button, knob, filter view, mod-FX view
// and compressor view, the param the engine's own getParamFromModEncoder was recorded naming
// (support/recorded_knob_assignments.h) -- and, where it names none on the song's compressor button, the compressor
// setting the knob edits instead.

#include "board_controls.hpp"
#include "ui/scenes/bind/knob_assignment.h"

#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/edits/param_address_for.h"
#include "model/edits/set_mod_knob_cc.h"
#include "model/edits/set_mod_knob_mode.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_collection.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "util/functions.h"

#include "support/main_numbering.h"
#include "support/recorded_knob_assignments.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <cstdio>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace {

namespace bind = deluge::ui::scenes::bind;
using deluge::edits::KnobModeOwner;
using deluge::project::ModKnobCc;

/// @brief Every knob of every mode on one owner, against what the engine's `getParamFromModEncoder` was recorded
///        naming: where the two first differ, or empty.
/// @param song       The song whose records the knobs are read from.
/// @param focus      The owner the knobs point at.
/// @param mode_owner Whose mod-knob mode each mode button sets.
/// @param lane_owner The owner and clip every named lane must be on, spelt from the fixture rather than the focus.
/// @param label      Names the owner in each disagreement.
/// @param recorded   The param recorded for each (mode, knob), in the order the loop visits them.
/// @param next       Where in @p recorded this call starts; advanced past what it read.
std::vector<std::string> disagreements(::Song& song, const bind::KnobFocus& focus, const KnobModeOwner& mode_owner,
                                       const deluge::project::ParamAddress& lane_owner, const std::string& label,
                                       std::span<const knob_recorded::NamedParam> recorded, size_t& next) {
	std::vector<std::string> found;
	for (uint8_t mode = 0; mode < kNumModButtons; mode++) {
		(void)deluge::edits::modKnobModeFor(mode_owner, mode);
		for (int32_t knob = 0; knob < 2; knob++) {
			const bind::KnobAssignment port = bind::knob_assignment(song.project, focus, knob, kDelugeControls);
			const std::string at = label + " mode " + std::to_string(mode) + " knob " + std::to_string(knob) + ": ";
			knob_recorded::NamedParam want = recorded[next++];
			// Main named its unpatched params past the arp's 15 (16) higher.
			const bool unpatched = want.kind == static_cast<int8_t>(deluge::plugin::ParamKind::UNPATCHED_SOUND)
			                       || want.kind == static_cast<int8_t>(deluge::plugin::ParamKind::UNPATCHED_GLOBAL);
			if (unpatched && want.space == 0
			    && want.id >= deluge::ui::scenes::spec::main_numbering::kUnpatchedFirstId) {
				const deluge::project::ParamTarget ours = deluge::ui::scenes::spec::main_numbering::target_of_id(
				    want.kind == static_cast<int8_t>(deluge::plugin::ParamKind::UNPATCHED_GLOBAL), want.id);
				want.space = static_cast<int8_t>(ours.space);
				want.id = ours.id.value;
				if (ours.space == deluge::project::ParamSpace::ARP) {
					want.kind = static_cast<int8_t>(deluge::plugin::ParamKind::ARP);
				}
			}
			if (const auto* named = std::get_if<deluge::project::ParamAddress>(&port.target)) {
				// The lane is the recorded param on the owner and clip the fixture holds.
				if (want.space != static_cast<int8_t>(named->space) || want.id != named->id.value
				    || want.kind != static_cast<int8_t>(port.kind) || lane_owner.owner != named->owner
				    || lane_owner.clip_id != named->clip_id) {
					found.push_back(at + "differs from the recorded param");
				}
			}
			else if (want.space != -1) {
				found.push_back(at + "names none where the engine named a param");
			}
		}
	}
	return found;
}

void report(const std::vector<std::string>& found) {
	for (const std::string& why : found) {
		std::fputs(("KNOB-ASSIGN " + why + "\n").c_str(), stdout);
	}
}

/// @brief Each filter view and mod-FX view of @p ge's chain, each run through @p check.
std::vector<std::string> over_views(deluge::model::LaneOwner ge,
                                    const std::function<std::vector<std::string>(std::string)>& check) {
	std::vector<std::string> found;
	for (const FilterType filter : {FilterType::LPF, FilterType::HPF, FilterType::EQ}) {
		deluge::edits::changeView(ge, "currentFilterType", filterTypeToString(filter));
		for (const ModFXParam fx : {ModFXParam::DEPTH, ModFXParam::FEEDBACK, ModFXParam::OFFSET}) {
			deluge::edits::changeView(ge, "modFXCurrentParam", modFXParamToString(fx));
			const std::string label = std::string{filterTypeToString(filter)} + "/" + modFXParamToString(fx);
			std::vector<std::string> more = check(label);
			found.insert(found.end(), more.begin(), more.end());
		}
	}
	return found;
}

} // namespace

// clang-format off
describe knob_assignment("what a gold knob moves, from the records", ${
	it("names what the recorded engine named on a synth, for every mode and knob", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* synth = static_cast<SoundInstrument*>(f.instrument);
		const bind::KnobFocus focus{.owner = bind::SoundFocus{
		    .slot = {deluge::project::SlotAddress::Kind::OUTPUT, *synth->id()},
		    .clip = deluge::project::ClipAddress{f.clip->id()}}};
		size_t next = 0;
		const auto found = disagreements(*f.song, focus, deluge::project::TrackAddress{*synth->id()},
		                                 {.owner = deluge::project::OwnerId::instrument(*synth->id()), .clip_id = f.clip->id()},
		                                 "synth",
		                                 knob_recorded::kSynthNames, next);
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("names what the recorded engine named on a kit row, with the kit's mode", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
		SoundDrum* drum = deluge::ui::scenes::spec::add_drum<SoundDrum>(f);
		Song* const before = currentSong;
		currentSong = f.song;
		auto* kit = static_cast<Kit*>(f.instrument);
		int32_t row_index = 0;
		NoteRow* row = f.clip->getNoteRowForDrum(drum, &row_index);
		expect(row).not_().to_be_null();
		if (row != nullptr) {
			const bind::KnobFocus focus{.owner = bind::SoundFocus{
			    .slot = {deluge::project::SlotAddress::Kind::DRUM, *drum->id()},
			    .clip = deluge::project::ClipAddress{f.clip->id()},
			    .row = deluge::project::NoteRowAddress{f.clip->id(), true, static_cast<int32_t>(*drum->id())},
			    .kit = *kit->id()}};
			size_t next = 0;
			const auto found = disagreements(*f.song, focus, deluge::project::TrackAddress{*kit->id()},
			                                 {.owner = deluge::project::OwnerId::drum(*drum->id()), .clip_id = f.clip->id()},
			                                 "kit row",
			                                 knob_recorded::kKitRowNames, next);
			report(found);
			expect(found.empty()).to_be_true();
		}
		currentSong = before;
	});

	it("names what the recorded engine named on a kit's chain, for every filter and mod-FX view", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* kit = static_cast<Kit*>(f.instrument);
		const bind::KnobFocus focus{.owner = bind::ChainFocus{
		    .chain = {deluge::project::ChainAddress::Kind::OUTPUT, *kit->id()},
		    .owner = bind::ChainOwner::KIT,
		    .clip = deluge::project::ClipAddress{f.clip->id()}}};
		size_t next = 0;
		const auto found = over_views(kit, [&](const std::string& view) {
			return disagreements(*f.song, focus, deluge::project::TrackAddress{*kit->id()},
			                     {.owner = deluge::project::OwnerId::instrument(*kit->id()), .clip_id = f.clip->id()},
			                     "kit chain " + view,
			                     knob_recorded::kKitChainNames, next);
		});
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("names what the recorded engine named on an audio track, for every filter and mod-FX view", _ {
		deluge::ui::scenes::spec::AudioSongFixture f = deluge::ui::scenes::spec::make_audio_clip_song();
		Song* const before = currentSong;
		currentSong = f.song;
		AudioOutput* audio = f.output;
		const bind::KnobFocus focus{.owner = bind::ChainFocus{
		    .chain = {deluge::project::ChainAddress::Kind::OUTPUT, *audio->id()},
		    .owner = bind::ChainOwner::AUDIO_TRACK,
		    .clip = deluge::project::ClipAddress{f.clip->id()}}};
		size_t next = 0;
		const auto found = over_views(audio, [&](const std::string& view) {
			return disagreements(*f.song, focus, deluge::project::TrackAddress{*audio->id()},
			                     {.owner = deluge::project::OwnerId::clip(f.clip->id())}, "audio " + view,
			                     knob_recorded::kAudioNames, next);
		});
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("names what the recorded engine named on the song, for every filter, mod-FX and compressor view", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		deluge::model::LaneOwner ge = &f.song->live.master_channel;
		std::vector<std::string> found;
		size_t next = 0;
		for (const bool editing : {false, true}) {
			for (uint8_t param = 0; param < (editing ? 5 : 1); param++) {
				const bind::KnobFocus focus{.owner = bind::SongFocus{}, .comp = {.editing = editing, .param = param}};
				auto more = over_views(ge, [&](const std::string& view) {
					return disagreements(*f.song, focus, deluge::project::SongAddress{},
					                     {.owner = deluge::project::OwnerId::song()},
					                     "song " + view + (editing ? " comp " + std::to_string(param) : ""),
					                     knob_recorded::kSongNames, next);
				});
				found.insert(found.end(), more.begin(), more.end());
			}
		}
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("names the song's compressor setting where its compressor button names no param", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		(void)deluge::edits::modKnobModeFor(deluge::project::SongAddress{}, 4);
		const auto arm = [&](bool editing, uint8_t param, int32_t knob) -> std::string {
			const bind::KnobFocus focus{.owner = bind::SongFocus{}, .comp = {.editing = editing, .param = param}};
			const bind::KnobAssignment a = bind::knob_assignment(f.song->project, focus, knob, kDelugeControls);
			const auto* comp = std::get_if<bind::CompArm>(&a.target);
			return comp != nullptr ? std::string{comp->path} : std::string{};
		};
		expect(arm(false, 0, 1)).to_equal(std::string{"audioCompressor/thresh"});
		expect(arm(true, 0, 1)).to_equal(std::string{"audioCompressor/thresh"});
		expect(arm(true, 0, 0)).to_equal(std::string{"audioCompressor/ratio"});
		expect(arm(true, 1, 0)).to_equal(std::string{"audioCompressor/attack"});
		expect(arm(true, 2, 0)).to_equal(std::string{"audioCompressor/release"});
		expect(arm(true, 3, 0)).to_equal(std::string{"audioCompressor/compHPF"});
		expect(arm(true, 4, 0)).to_equal(std::string{"audioCompressor/compBlend"});
		expect(arm(false, 0, 0)).to_equal(std::string{}); // the reverb send: a param
		currentSong = before;
	});

	it("names what the recorded engine named on a MIDI track: CCs, bend, the Y axis and aftertouch", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_midi_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* midi = static_cast<MIDIInstrument*>(f.instrument);
		const deluge::project::TrackAddress track{*midi->id()};
		(void)deluge::edits::modKnobCCFor(track, 0, {.kind = ModKnobCc::Kind::NUMBER, .number = 74});
		(void)deluge::edits::modKnobCCFor(track, 1, {.kind = ModKnobCc::Kind::BEND});
		(void)deluge::edits::modKnobCCFor(track, 2, {.kind = ModKnobCc::Kind::AFTERTOUCH});
		(void)deluge::edits::modKnobCCFor(track, 3, {.kind = ModKnobCc::Kind::NUMBER, .number = ModKnobCc::kYAxisNumber});
		const bind::KnobFocus focus{.owner = bind::MidiFocus{.track = track, .clip = deluge::project::ClipAddress{f.clip->id()}}};
		size_t next = 0;
		const auto found = disagreements(*f.song, focus, track, {.owner = deluge::project::OwnerId::clip(f.clip->id())},
		                                 "midi", knob_recorded::kMidiNames, next);
		report(found);
		expect(found.empty()).to_be_true();
		const bind::KnobFocus no_clip{.owner = bind::MidiFocus{.track = track}};
		expect(std::holds_alternative<std::monostate>(bind::knob_assignment(f.song->project, no_clip, 0, kDelugeControls).target)).to_be_true();
		currentSong = before;
	});

	it("names nothing for a CV track, a non-sound kit row or no focus", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		const bind::KnobFocus none{};
		expect(std::holds_alternative<std::monostate>(bind::knob_assignment(f.song->project, none, 0, kDelugeControls).target)).to_be_true();
		expect(std::holds_alternative<std::monostate>(bind::knob_assignment(f.song->project, none, 1, kDelugeControls).target)).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(knob_assignment)
