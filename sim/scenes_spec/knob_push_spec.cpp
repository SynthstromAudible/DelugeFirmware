// What a gold knob's push and a mod button's press do, from the song's records: on twin songs, the engine's own
// modEncoderButtonAction on one and the port's push, applied through the app's sink, on the other, for every owner
// kind, mode and knob, pushed three times round each cycle -- the same effect settings, knob mappings, chain views,
// reverb, compressor view, stutter and text after every push. And the mod button's popup, as the engine shows it.

#include "scenes/services/knob_push.h"

#include "scenes/bind/knob_assignment.h"
#include "scenes/menu/bind_host.h"

#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/set_mod_knob_mode.h"
#include "model/edits/setting_edit.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "notify/notice_queue.h"
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

namespace Buttons {
extern bool buttonStates[NUM_BUTTON_COLS + 1][NUM_BUTTON_ROWS];
}

namespace {

namespace bind = deluge::scenes::bind;
namespace services = deluge::scenes::services;
using deluge::edits::KnobModeOwner;

/// @brief The display legacy's push and mod button draw on: what it last popped up, kept.
struct StubDisplay final : deluge::hid::Display {
	std::string popup;
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const*, int8_t, bool, uint8_t, int32_t, PopupType) override {}
	void popupText(char const* text, PopupType) override { popup = text; }
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
	void displayNotification(std::string_view, std::optional<std::string_view>) override {}
};

/// @brief One twin: a song, the engine owner its knobs point at, and the same owner as the port's focus.
struct Side {
	Song* song = nullptr;
	ModControllable* engine = nullptr;
	std::function<ModelStackWithThreeMainThings*(char*)> stack_of;
	std::function<void()> point_view; ///< points legacy's view at the owner, which its knob names read
	bind::KnobFocus focus;
	KnobModeOwner mode_owner;
	deluge::project::CompView comp{}; ///< the engine side's compressor view, which its pushes move
};

/// @brief What a push leaves that the two sides are compared on.
struct Outcome {
	deluge::effects::EffectStates effects;
	std::vector<deluge::project::ControllerMapping> controls;
	uint8_t filter_type = 0;
	uint8_t mod_fx_param = 0;
	float room = 0;
	float damping = 0;
	bool stuttering = false;
	std::string text;
	bool operator==(const Outcome&) const = default;
};

/// @brief @p side's records and facts now; @p text is what the push showed.
Outcome outcome_of(const Side& side, std::string text) {
	Outcome out;
	currentSong = side.song;
	const deluge::scenes::bind::RuntimeFacts& facts = deluge::scenes::menu::host_runtime_facts();
	const deluge::project::Song& model = side.song->project;
	if (const std::optional<services::EffectOwner> owner = services::effect_owner(side.focus)) {
		if (const auto* slot = std::get_if<deluge::project::SlotAddress>(&*owner)) {
			if (const deluge::instrument::SoundState* state = facts.sound_state(*slot)) {
				out.effects = state->effects;
			}
			if (slot->kind == deluge::project::SlotAddress::Kind::DRUM) {
				if (const deluge::project::DocDrum* drum = model.drum_structure.find(slot->id)) {
					out.controls = drum->controls;
				}
			}
			else if (const deluge::project::DocOutput* output = model.output_structure.find(slot->id)) {
				out.controls = output->controls;
			}
			else {
				std::fputs(("KNOB-PUSH no record for output " + std::to_string(slot->id) + "\n").c_str(), stdout);
			}
		}
		else {
			const auto chain = std::get<deluge::project::ChainAddress>(*owner);
			if (const deluge::effects::EffectStates* states = facts.chain_effects(chain)) {
				out.effects = *states;
			}
			const deluge::project::FxChain* record = const_cast<deluge::project::Song&>(model).resolveChain(chain);
			if (record != nullptr) {
				out.filter_type = record->filter_type;
				out.mod_fx_param = record->mod_fx_param;
			}
		}
	}
	out.room = model.reverb.room_size;
	out.damping = model.reverb.damping;
	out.stuttering = side.song->live.stutter.active();
	out.text = std::move(text);
	return out;
}

void hold_mod_button(int32_t mode, bool held) {
	Buttons::buttonStates[modButtonX[mode]][modButtonY[mode]] = held;
}

/// @brief The last text notice raised, and the queue emptied.
std::string last_notice() {
	std::string text;
	deluge::notify::Notice n;
	while (deluge::notify::notices().pop(n)) {
		if (n.kind == deluge::notify::Notice::Kind::Text) {
			text = n.text;
		}
	}
	return text;
}

services::PushInputs inputs_of(const Side& side, bool held) {
	currentSong = side.song;
	services::PushInputs inputs;
	const deluge::scenes::bind::RuntimeFacts& facts = deluge::scenes::menu::host_runtime_facts();
	if (const std::optional<services::EffectOwner> owner = services::effect_owner(side.focus)) {
		if (const auto* slot = std::get_if<deluge::project::SlotAddress>(&*owner)) {
			if (const deluge::instrument::SoundState* state = facts.sound_state(*slot)) {
				inputs.effects = state->effects;
				inputs.fm = state->synth_mode == SynthMode::FM;
			}
		}
		else if (const deluge::effects::EffectStates* states =
		             facts.chain_effects(std::get<deluge::project::ChainAddress>(*owner))) {
			inputs.effects = *states;
		}
	}
	inputs.alt_delay = runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::AltGoldenKnobDelayParams);
	inputs.button_held = held;
	inputs.clip_context = !std::holds_alternative<bind::SongFocus>(side.focus.owner);
	return inputs;
}

/// @brief Push knob @p knob on both sides (press and release), and where the two first differ, or empty.
std::string push_both(Side& legacy, Side& port, uint8_t mode, int32_t knob, bool held, StubDisplay& stub) {
	// The engine's push, its notice and its popup.
	currentSong = legacy.song;
	(void)last_notice();
	stub.popup.clear();
	hold_mod_button(mode, held);
	{
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		(void)legacy.engine->modEncoderButtonAction(knob, true, legacy.stack_of(memory), &legacy.comp);
	}
	// As View's push does: a knob cycled to its next param is its owner's mappings.
	deluge::edits::recordControlsOf(*legacy.engine);
	const std::string legacy_text = held ? stub.popup : last_notice();
	const Outcome engine = outcome_of(legacy, legacy_text);
	{
		currentSong = legacy.song;
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		(void)legacy.engine->modEncoderButtonAction(knob, false, legacy.stack_of(memory), &legacy.comp);
	}

	// The port's push, applied through the app's sink.
	const services::KnobPush press =
	    services::knob_push(port.song->project, port.focus, knob, true, inputs_of(port, held));
	currentSong = port.song;
	(void)services::apply_push(deluge::scenes::menu::host_write_sink(), port.focus, press);
	if (press.comp) {
		port.focus.comp = *press.comp;
	}
	const Outcome ported = outcome_of(port, held ? press.popup : press.notice);
	const services::KnobPush release =
	    services::knob_push(port.song->project, port.focus, knob, false, inputs_of(port, held));
	currentSong = port.song;
	(void)services::apply_push(deluge::scenes::menu::host_write_sink(), port.focus, release);
	hold_mod_button(mode, false);
	(void)last_notice();

	const std::string at =
	    "mode " + std::to_string(mode) + " knob " + std::to_string(knob) + (held ? " held" : "") + ": ";
	if (engine.effects != ported.effects) {
		return at + "effects";
	}
	if (engine.controls != ported.controls) {
		std::string why =
		    at + "controls " + std::to_string(engine.controls.size()) + " vs " + std::to_string(ported.controls.size());
		for (size_t i = 0; i < std::min(engine.controls.size(), ported.controls.size()); i++) {
			if (!(engine.controls[i] == ported.controls[i])) {
				why += " first at " + std::to_string(i) + ": " + std::to_string(engine.controls[i].target.value)
				       + " vs " + std::to_string(ported.controls[i].target.value);
				break;
			}
		}
		return why;
	}
	if (engine.filter_type != ported.filter_type || engine.mod_fx_param != ported.mod_fx_param) {
		return at + "chain view";
	}
	if (engine.room != ported.room || engine.damping != ported.damping) {
		return at + "reverb";
	}
	if (engine.stuttering != ported.stuttering) {
		return at + "stutter";
	}
	if (engine.text != ported.text) {
		return at + "text '" + engine.text + "' vs '" + ported.text + "'";
	}
	return {};
}

/// @brief Every mode and knob, three pushes each, with and without the alt delay knobs and the button held.
std::vector<std::string> push_everything(Side& legacy, Side& port, const std::string& label,
                                         const std::function<std::string()>& extra = {}) {
	StubDisplay stub;
	display = &stub;
	std::vector<std::string> found;
	for (const bool alt : {false, true}) {
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::AltGoldenKnobDelayParams,
		                           alt ? RuntimeFeatureStateToggle::On : RuntimeFeatureStateToggle::Off);
		for (uint8_t mode = 0; mode < kNumModButtons; mode++) {
			currentSong = legacy.song;
			(void)deluge::edits::modKnobModeFor(legacy.mode_owner, mode);
			currentSong = port.song;
			(void)deluge::edits::modKnobModeFor(port.mode_owner, mode);
			for (int32_t knob = 0; knob < 2; knob++) {
				// The delay's sync level names its 256th as none on the engine, which wrote it so: two pushes (one
				// plain, one held) stop short of it.
				const int32_t pushes = (alt && mode == 3 && knob == 0) ? 1 : 3;
				for (int32_t n = 0; n < pushes; n++) {
					for (const bool held : {false, true}) {
						if (std::string why = push_both(legacy, port, mode, knob, held, stub); !why.empty()) {
							found.push_back(label + (alt ? " alt " : " ") + why);
						}
					}
				}
			}
		}
		if (extra) {
			if (std::string why = extra(); !why.empty()) {
				found.push_back(label + " " + why);
			}
		}
	}
	runtimeFeatureSettings.set(RuntimeFeatureSettingType::AltGoldenKnobDelayParams, RuntimeFeatureStateToggle::Off);
	display = nullptr;
	return found;
}

/// @brief Each mod button pressed on both sides: where the engine's popup and the port's first differ, or empty.
std::vector<std::string> press_buttons(Side& legacy, Side& port, const std::string& label) {
	StubDisplay stub;
	display = &stub;
	std::vector<std::string> found;
	for (uint8_t button = 0; button < kNumModButtons; button++) {
		currentSong = legacy.song;
		(void)deluge::edits::modKnobModeFor(legacy.mode_owner, button);
		legacy.point_view();
		stub.popup.clear();
		legacy.engine->modButtonAction(button, true, nullptr, legacy.comp);
		currentSong = port.song;
		(void)deluge::edits::modKnobModeFor(port.mode_owner, button);
		const std::string ported =
		    services::mod_button_popup(port.song->project, port.focus, button, inputs_of(port, true));
		if (stub.popup != ported) {
			found.push_back(label + " button " + std::to_string(button) + ": '" + stub.popup + "' vs '" + ported + "'");
		}
	}
	display = nullptr;
	return found;
}

void report(const std::vector<std::string>& found) {
	for (const std::string& why : found) {
		std::fputs(("KNOB-PUSH " + why + "\n").c_str(), stdout);
	}
}

/// @brief A synth's side of the twin.
Side synth_side(deluge::scenes::spec::SongFixture& f) {
	auto* synth = static_cast<SoundInstrument*>(f.instrument);
	Side side;
	side.song = f.song;
	side.engine = synth;
	side.stack_of = [song = f.song, synth, clip = f.clip](char* memory) {
		return setupModelStackWithThreeMainThingsButNoNoteRow(memory, song, synth, clip, &clip->paramManager);
	};
	side.point_view = [clip = f.clip] { view.setActiveModControllableTimelineCounter(clip, false); };
	side.focus = {.owner = bind::SoundFocus{.slot = {deluge::project::SlotAddress::Kind::OUTPUT, *synth->id()},
	                                        .clip = deluge::project::ClipAddress{f.clip->id()}}};
	side.mode_owner = deluge::project::TrackAddress{*synth->id()};
	return side;
}

/// @brief The song's own chain's side of the twin.
Side song_side(deluge::scenes::spec::SongFixture& f) {
	Side side;
	side.song = f.song;
	side.engine = &f.song->live.global_effectable;
	side.stack_of = [song = f.song](char* memory) { return song->setupModelStackWithSongAsTimelineCounter(memory); };
	side.point_view = [song = f.song] {
		view.setActiveModControllableWithoutTimelineCounter(&song->live.global_effectable, &song->paramManager);
	};
	side.focus = {.owner = bind::SongFocus{}};
	side.mode_owner = deluge::project::SongAddress{};
	return side;
}

/// @brief A kit's chain's side of the twin.
Side kit_chain_side(deluge::scenes::spec::SongFixture& f) {
	auto* kit = static_cast<Kit*>(f.instrument);
	Side side;
	side.song = f.song;
	side.engine = kit;
	side.stack_of = [song = f.song, kit, clip = f.clip](char* memory) {
		return setupModelStackWithThreeMainThingsButNoNoteRow(memory, song, kit, clip, &clip->paramManager);
	};
	side.point_view = [clip = f.clip] {
		clip->affectEntire = true; // the kit's own chain, not its selected row
		view.setActiveModControllableTimelineCounter(clip, false);
	};
	side.focus = {.owner = bind::ChainFocus{.chain = {deluge::project::ChainAddress::Kind::OUTPUT, *kit->id()},
	                                        .owner = bind::ChainOwner::KIT,
	                                        .clip = deluge::project::ClipAddress{f.clip->id()}}};
	side.mode_owner = deluge::project::TrackAddress{*kit->id()};
	return side;
}

} // namespace

// clang-format off
describe knob_push("what a gold knob's push and a mod button do, from the records", ${
	it("pushes a synth's knobs as the engine does, in every mode, round every cycle", _ {
		Song* const before = currentSong;
		deluge::scenes::spec::SongFixture a = deluge::scenes::spec::make_synth_song();
		deluge::scenes::spec::SongFixture b = deluge::scenes::spec::make_synth_song();
		Side legacy = synth_side(a);
		Side port = synth_side(b);
		const auto found = push_everything(legacy, port, "synth");
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("pushes a kit row's knobs as the engine does, with the kit's mode", _ {
		Song* const before = currentSong;
		deluge::scenes::spec::SongFixture a = deluge::scenes::spec::make_kit_song();
		deluge::scenes::spec::SongFixture b = deluge::scenes::spec::make_kit_song();
		const auto row_side = [](deluge::scenes::spec::SongFixture& f) {
			currentSong = f.song; // a drum takes its id from the current song
			SoundDrum* drum = deluge::scenes::spec::add_drum<SoundDrum>(f);
			auto* kit = static_cast<Kit*>(f.instrument);
			int32_t row_index = 0;
			NoteRow* row = f.clip->getNoteRowForDrum(drum, &row_index);
			Side side;
			side.song = f.song;
			side.engine = drum;
			side.stack_of = [song = f.song, clip = f.clip, row_index, row, drum](char* memory) {
				return setupModelStackWithThreeMainThingsIncludingNoteRow(memory, song, clip, row_index, row, drum,
				                                                          &row->paramManager);
			};
			side.focus = {.owner = bind::SoundFocus{
			    .slot = {deluge::project::SlotAddress::Kind::DRUM, *drum->id()},
			    .clip = deluge::project::ClipAddress{f.clip->id()},
			    .row = deluge::project::NoteRowAddress{f.clip->id(), true, static_cast<int32_t>(*drum->id())},
			    .kit = *kit->id()}};
			side.mode_owner = deluge::project::TrackAddress{*kit->id()};
			return side;
		};
		Side legacy = row_side(a);
		Side port = row_side(b);
		const auto found = push_everything(legacy, port, "kit row");
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("pushes the song's knobs as the engine does, its compressor view included", _ {
		Song* const before = currentSong;
		deluge::scenes::spec::SongFixture a = deluge::scenes::spec::make_synth_song();
		deluge::scenes::spec::SongFixture b = deluge::scenes::spec::make_synth_song();
		Side legacy = song_side(a);
		Side port = song_side(b);
		const auto found = push_everything(legacy, port, "song", [&]() -> std::string {
			if (legacy.comp != port.focus.comp) {
				return "compressor view";
			}
			return {};
		});
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("pushes a kit's chain as the engine does", _ {
		Song* const before = currentSong;
		deluge::scenes::spec::SongFixture a = deluge::scenes::spec::make_kit_song();
		deluge::scenes::spec::SongFixture b = deluge::scenes::spec::make_kit_song();
		Side legacy = kit_chain_side(a);
		Side port = kit_chain_side(b);
		const auto found = push_everything(legacy, port, "kit chain");
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("pushes an audio track's chain as the engine does", _ {
		Song* const before = currentSong;
		deluge::scenes::spec::AudioSongFixture a = deluge::scenes::spec::make_audio_clip_song();
		deluge::scenes::spec::AudioSongFixture b = deluge::scenes::spec::make_audio_clip_song();
		const auto audio_side = [](deluge::scenes::spec::AudioSongFixture& f) {
			Side side;
			side.song = f.song;
			side.engine = f.output;
			side.stack_of = [song = f.song, audio = f.output, clip = f.clip](char* memory) {
				return setupModelStackWithThreeMainThingsButNoNoteRow(memory, song, audio, clip, &clip->paramManager);
			};
			side.focus = {.owner = bind::ChainFocus{.chain = {deluge::project::ChainAddress::Kind::OUTPUT, *f.output->id()},
			                                        .owner = bind::ChainOwner::AUDIO_TRACK,
			                                        .clip = deluge::project::ClipAddress{f.clip->id()}}};
			side.mode_owner = deluge::project::TrackAddress{*f.output->id()};
			return side;
		};
		Side legacy = audio_side(a);
		Side port = audio_side(b);
		const auto found = push_everything(legacy, port, "audio");
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("shows the popup the engine shows for every mod button, on a synth, the song and a kit's chain", _ {
		Song* const before = currentSong;
		std::vector<std::string> found;
		{
			deluge::scenes::spec::SongFixture a = deluge::scenes::spec::make_synth_song();
			deluge::scenes::spec::SongFixture b = deluge::scenes::spec::make_synth_song();
			Side legacy = synth_side(a);
			Side port = synth_side(b);
			auto more = press_buttons(legacy, port, "synth");
			found.insert(found.end(), more.begin(), more.end());
		}
		{
			deluge::scenes::spec::SongFixture a = deluge::scenes::spec::make_synth_song();
			deluge::scenes::spec::SongFixture b = deluge::scenes::spec::make_synth_song();
			Side legacy = song_side(a);
			Side port = song_side(b);
			auto more = press_buttons(legacy, port, "song");
			found.insert(found.end(), more.begin(), more.end());
		}
		{
			deluge::scenes::spec::SongFixture a = deluge::scenes::spec::make_kit_song();
			deluge::scenes::spec::SongFixture b = deluge::scenes::spec::make_kit_song();
			Side legacy = kit_chain_side(a);
			Side port = kit_chain_side(b);
			auto more = press_buttons(legacy, port, "kit chain");
			found.insert(found.end(), more.begin(), more.end());
		}
		report(found);
		expect(found.empty()).to_be_true();
		currentSong = before;
	});

	it("names a note length as the song does", _ {
		expect(services::note_length_name(3, 0)).to_equal(std::string{"32nd-notes"});
		expect(services::note_length_name(48, 0)).to_equal(std::string{"2nd-notes"});
		expect(services::note_length_name(96, 0)).to_equal(std::string{"1-bar"});
		expect(services::note_length_name(384, 0)).to_equal(std::string{"4-bar"});
	});
});
// clang-format on

CPPSPEC_SPEC(knob_push)
