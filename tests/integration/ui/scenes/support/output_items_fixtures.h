#pragma once

/// @file
/// @brief The fixtures of the two output items (the specific-output selector and the MIDI device-definition link): an
///        audio clip recording from a specific track in a song of four outputs, a MIDI clip, and the walks and cases
///        both the spec and the menu oracle freezer run.

#include "gui/ui/menu_view_of.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/registry.h"

// `NonAudioInstrument` is named by the output headers without being included.
#include "model/instrument/non_audio_instrument.h"

#include "hid/display/display.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "storage/storage_manager.h"

#include "support/menu_context.h"
#include "support/popup_capture.h"
#include "support/song_fixture.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::ui::scenes::spec::outputitems {

using deluge::ui::scenes::menu::MenuContext;

/// @brief An audio clip whose output records from a specific track, in a song with four outputs.
///
/// The order is the discriminator the whole selector group leans on: the walk must skip the audio track itself
/// (`canRecordFrom` refuses `this`) and the MIDI one (it refuses `MIDI_OUT` outright), so a port that walked raw song
/// indices would land on a different output at almost every step.
///
/// Indices, after `addOutput(..., atStart=false)` appends each one:
///   0 = the `AudioOutput` being edited (**not** recordable -- itself)
///   1 = a synth                       (recordable)
///   2 = a MIDI instrument             (**not** recordable)
///   3 = a second synth                (recordable)
struct ClipRig {
	spec::AudioSongFixture song = spec::make_audio_clip_song();
	/// The song current before the example, put back after it: the fixture's song is made current, so the app's sink
	/// binds it.
	Song* const saved_song = currentSong;
	Instrument* synth_a = nullptr;
	Instrument* midi = nullptr;
	Instrument* synth_b = nullptr;

	ClipRig(const ClipRig&) = delete;
	ClipRig& operator=(const ClipRig&) = delete;

	~ClipRig() {
		currentSong = saved_song;
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	ClipRig() {
		currentSong = song.song;
		song.output->setInputChannel(AudioInputChannel::SPECIFIC_OUTPUT);
		synth_a = add_synth("A");
		midi = add_midi();
		synth_b = add_synth("B");
	}

	Instrument* add_synth(const char* name) {
		ParamManagerForTimeline params;
		auto* synth = static_cast<Instrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
		synth->dirPath = "SYNTHS";
		synth->name = name;
		song.song->addOutput(synth, /*atStart=*/false);
		return synth;
	}

	Instrument* add_midi() {
		auto* m = static_cast<Instrument*>(StorageManager::createNewNonAudioInstrument(OutputType::MIDI_OUT, 0, 0));
		m->name = "M";
		song.song->addOutput(m, /*atStart=*/false);
		return m;
	}

	MenuContext ctx() {
		return spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                    /*kit_global_fx=*/false);
	}

	/// @brief Where in the song the edited output records from: its output index, or -1 for none.
	int32_t recording_from_index() { return index_of(song.output->getOutputRecordingFrom()); }

	/// @brief @p output's index in the song, or -1 for none or an output the song does not hold.
	int32_t index_of(Output* output) {
		for (int32_t i = 0; output != nullptr && i < song.song->getNumOutputs(); i++) {
			if (song.song->getOutputFromIndex(i) == output) {
				return i;
			}
		}
		return -1;
	}
};

/// @brief A MIDI clip, for the device-definition link.
struct MidiRig {
	spec::SongFixture song = spec::make_midi_song();
	Song* const saved_song = currentSong;

	MidiRig(const MidiRig&) = delete;
	MidiRig& operator=(const MidiRig&) = delete;
	MidiRig() { currentSong = song.song; }
	~MidiRig() {
		currentSong = saved_song;
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	MIDIInstrument* instrument() { return static_cast<MIDIInstrument*>(song.instrument); }
	MenuContext ctx() {
		return spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                    /*kit_global_fx=*/false);
	}
};

/// @brief One linked-definition state of the MIDI instrument: the case's name and the file name set on it.
struct LinkedCase {
	const char* name;
	const char* path;
};

/// @brief The three states `Linked::readCurrentValue` is asked in, in order: nothing, a file, nothing again.
inline constexpr std::array<LinkedCase, 3> kLinkedCases{{
    {"empty", ""},
    {"device1", "MIDI_DEVICES/DEFINITION/DEVICE1.XML"},
    {"empty_again", ""},
}};

/// @brief Which output the edited track records from before a walk starts.
enum class Start : uint8_t { None, SynthA, SynthB };

/// @brief One walk of the selector: where it starts and the select-encoder detents it takes, one at a time.
struct Walk {
	const char* name;
	Start start;
	bool extra_synths; ///< Two more synths after the fixture's four outputs (C, D), so five steps fit.
	int32_t detents;   ///< Signed; the walk takes `|detents|` single detents in that direction.
};

/// @brief The walks both sides take, in the spec's order.
inline constexpr std::array<Walk, 5> kWalks{{
    {"skip", Start::SynthA, false, 1},
    {"shift", Start::SynthA, true, 5},
    {"end", Start::SynthA, false, 5},
    {"bottom", Start::SynthA, false, -3},
    {"back", Start::SynthB, false, -1},
}};

/// @brief Put @p rig's stored source on @p start.
inline void store_source(ClipRig& rig, Start start) {
	Output* from = nullptr;
	if (start == Start::SynthA) {
		from = rig.synth_a;
	}
	else if (start == Start::SynthB) {
		from = rig.synth_b;
	}
	rig.song.output->setOutputRecordingFrom(from);
}

} // namespace deluge::ui::scenes::spec::outputitems
