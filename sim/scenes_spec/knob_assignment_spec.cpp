// What a gold knob moves, from the song's records: for every kind of owner, mod button, knob, filter view, mod-FX view
// and compressor view, the param the engine's own getParamFromModEncoder names -- and, where it names none on the
// song's compressor button, the compressor setting the knob edits instead.

#include "scenes/bind/knob_assignment.h"

#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/edits/param_address_for.h"
#include "model/edits/set_mod_knob_cc.h"
#include "model/edits/set_mod_knob_mode.h"
#include "model/global_effectable/global_effectable.h"
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

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

namespace bind = deluge::scenes::bind;
using deluge::edits::KnobModeOwner;
using deluge::project::ModKnobCc;

/// @brief Whether @p a and @p b name the same lane.
bool same(const deluge::project::ParamAddress& a, const deluge::project::ParamAddress& b) {
	return a.owner == b.owner && a.space == b.space && a.id == b.id && a.clip_id == b.clip_id;
}

/// @brief Every knob of every mode on one owner, against the engine: where the two first differ, or empty.
/// @param stack_of The engine's stack for the owner, in @p memory.
std::vector<std::string> disagreements(::Song& song, ModControllable& engine, const bind::KnobFocus& focus,
                                       const KnobModeOwner& mode_owner,
                                       const std::function<ModelStackWithThreeMainThings*(char*)>& stack_of,
                                       const std::string& label) {
	std::vector<std::string> found;
	for (uint8_t mode = 0; mode < kNumModButtons; mode++) {
		(void)deluge::edits::modKnobModeFor(mode_owner, mode);
		for (int32_t knob = 0; knob < 2; knob++) {
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			// As a turn asks: a cable the sound's knob names is made, which the record names already.
			ModelStackWithAutoParam* legacy = engine.getParamFromModEncoder(knob, stack_of(memory), true, focus.comp);
			const bind::KnobAssignment port = bind::knob_assignment(song.project, focus, knob);
			const std::string at = label + " mode " + std::to_string(mode) + " knob " + std::to_string(knob) + ": ";
			if (legacy != nullptr && legacy->autoParam != nullptr) {
				const std::optional<deluge::project::ParamAddress> address = deluge::edits::addressFor(*legacy);
				const auto* named = std::get_if<deluge::project::ParamAddress>(&port.target);
				if (!address || named == nullptr || !same(*named, *address)) {
					found.push_back(at + "names another param");
				}
				else if (port.kind != legacy->paramCollection->getParamKind()) {
					found.push_back(at + "another kind");
				}
			}
			else if (std::holds_alternative<deluge::project::ParamAddress>(port.target)) {
				found.push_back(at + "names a param the engine has none for");
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
std::vector<std::string> over_views(GlobalEffectable& ge,
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
	it("names what the engine names on a synth, for every mode and knob", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* synth = static_cast<SoundInstrument*>(f.instrument);
		const bind::KnobFocus focus{.owner = bind::SoundFocus{
		    .slot = {deluge::project::SlotAddress::Kind::OUTPUT, *synth->id()},
		    .clip = deluge::project::ClipAddress{f.clip->id()}}};
		const auto found = disagreements(*f.song, *synth, focus, deluge::project::TrackAddress{*synth->id()},
		    [&](char* memory) {
			    return setupModelStackWithThreeMainThingsButNoNoteRow(memory, f.song, synth, f.clip, &f.clip->paramManager);
		    }, "synth");
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("names what the engine names on a kit row, with the kit's mode", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_kit_song();
		SoundDrum* drum = deluge::scenes::spec::add_drum<SoundDrum>(f);
		Song* const before = currentSong;
		currentSong = f.song;
		auto* kit = static_cast<Kit*>(f.instrument);
		int32_t row_index = 0;
		NoteRow* row = f.clip->getNoteRowForDrum(drum, &row_index);
		expect(row != nullptr).to_be_true();
		if (row != nullptr) {
			const bind::KnobFocus focus{.owner = bind::SoundFocus{
			    .slot = {deluge::project::SlotAddress::Kind::DRUM, *drum->id()},
			    .clip = deluge::project::ClipAddress{f.clip->id()},
			    .row = deluge::project::NoteRowAddress{f.clip->id(), true, static_cast<int32_t>(*drum->id())},
			    .kit = *kit->id()}};
			const auto found = disagreements(*f.song, *drum, focus, deluge::project::TrackAddress{*kit->id()},
			    [&](char* memory) {
				    return setupModelStackWithThreeMainThingsIncludingNoteRow(memory, f.song, f.clip, row_index, row, drum,
				                                                              &row->paramManager);
			    }, "kit row");
			report(found);
			expect(found.empty()).to_be_true();
		}
		currentSong = before;
	});

	it("names what the engine names on a kit's chain, for every filter and mod-FX view", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_kit_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* kit = static_cast<Kit*>(f.instrument);
		const bind::KnobFocus focus{.owner = bind::ChainFocus{
		    .chain = {deluge::project::ChainAddress::Kind::OUTPUT, *kit->id()},
		    .owner = bind::ChainOwner::KIT,
		    .clip = deluge::project::ClipAddress{f.clip->id()}}};
		const auto found = over_views(*kit, [&](const std::string& view) {
			return disagreements(*f.song, *kit, focus, deluge::project::TrackAddress{*kit->id()}, [&](char* memory) {
				return setupModelStackWithThreeMainThingsButNoNoteRow(memory, f.song, kit, f.clip, &f.clip->paramManager);
			}, "kit chain " + view);
		});
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("names what the engine names on an audio track, for every filter and mod-FX view", _ {
		deluge::scenes::spec::AudioSongFixture f = deluge::scenes::spec::make_audio_clip_song();
		Song* const before = currentSong;
		currentSong = f.song;
		AudioOutput* audio = f.output;
		const bind::KnobFocus focus{.owner = bind::ChainFocus{
		    .chain = {deluge::project::ChainAddress::Kind::OUTPUT, *audio->id()},
		    .owner = bind::ChainOwner::AUDIO_TRACK,
		    .clip = deluge::project::ClipAddress{f.clip->id()}}};
		const auto found = over_views(*audio, [&](const std::string& view) {
			return disagreements(*f.song, *audio, focus, deluge::project::TrackAddress{*audio->id()}, [&](char* memory) {
				return setupModelStackWithThreeMainThingsButNoNoteRow(memory, f.song, audio, f.clip, &f.clip->paramManager);
			}, "audio " + view);
		});
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("names what the engine names on the song, for every filter, mod-FX and compressor view", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		GlobalEffectable& ge = f.song->live.global_effectable;
		std::vector<std::string> found;
		for (const bool editing : {false, true}) {
			for (uint8_t param = 0; param < (editing ? 5 : 1); param++) {
				const bind::KnobFocus focus{.owner = bind::SongFocus{}, .comp = {.editing = editing, .param = param}};
				auto more = over_views(ge, [&](const std::string& view) {
					return disagreements(*f.song, ge, focus, deluge::project::SongAddress{}, [&](char* memory) {
						return f.song->setupModelStackWithSongAsTimelineCounter(memory);
					}, "song " + view + (editing ? " comp " + std::to_string(param) : ""));
				});
				found.insert(found.end(), more.begin(), more.end());
			}
		}
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("names the song's compressor setting where its compressor button names no param", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		(void)deluge::edits::modKnobModeFor(deluge::project::SongAddress{}, 4);
		const auto arm = [&](bool editing, uint8_t param, int32_t knob) -> std::string {
			const bind::KnobFocus focus{.owner = bind::SongFocus{}, .comp = {.editing = editing, .param = param}};
			const bind::KnobAssignment a = bind::knob_assignment(f.song->project, focus, knob);
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

	it("names what the engine names on a MIDI track: CCs, bend, the Y axis and aftertouch", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_midi_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* midi = static_cast<MIDIInstrument*>(f.instrument);
		const deluge::project::TrackAddress track{*midi->id()};
		(void)deluge::edits::modKnobCCFor(track, 0, {.kind = ModKnobCc::Kind::NUMBER, .number = 74});
		(void)deluge::edits::modKnobCCFor(track, 1, {.kind = ModKnobCc::Kind::BEND});
		(void)deluge::edits::modKnobCCFor(track, 2, {.kind = ModKnobCc::Kind::AFTERTOUCH});
		(void)deluge::edits::modKnobCCFor(track, 3, {.kind = ModKnobCc::Kind::NUMBER, .number = ModKnobCc::kYAxisNumber});
		const bind::KnobFocus focus{.owner = bind::MidiFocus{.track = track, .clip = deluge::project::ClipAddress{f.clip->id()}}};
		const auto found = disagreements(*f.song, *midi, focus, track, [&](char* memory) {
			return setupModelStackWithThreeMainThingsButNoNoteRow(memory, f.song, midi, f.clip, &f.clip->paramManager);
		}, "midi");
		report(found);
		expect(found.empty()).to_be_true();
		const bind::KnobFocus no_clip{.owner = bind::MidiFocus{.track = track}};
		expect(std::holds_alternative<std::monostate>(bind::knob_assignment(f.song->project, no_clip, 0).target)).to_be_true();
		currentSong = before;
	});

	it("names nothing for a CV track, a non-sound kit row or no focus", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		const bind::KnobFocus none{};
		expect(std::holds_alternative<std::monostate>(bind::knob_assignment(f.song->project, none, 0).target)).to_be_true();
		expect(std::holds_alternative<std::monostate>(bind::knob_assignment(f.song->project, none, 1).target)).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(knob_assignment)
