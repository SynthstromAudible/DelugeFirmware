/// @file
/// @brief Who gets a MIDI message offered for learn: `PlaybackHandler`'s four dispatchers, the base `UI` answer, and
///        `SoundEditor`'s.
///
/// The expectations are `main`'s dispatch, split where it is decided:
///
/// - `PlaybackHandler` offers a note-on, a program change, a non-MPE CC and a non-MPE pitch bend to the current UI and
///   stops when it answers true. It never offers a note-off, an MPE member channel's CC or an MPE pitch bend.
/// - The base `UI` answer is what `main`'s dispatchers did for a UI with no learn of its own: outside learn mode,
///   nothing (the message plays); in learn mode, the view's learn for a note-on, a program change and a CC, and the
///   message is used up; a pitch bend is never used.
/// - `SoundEditor` learns into the focused item in learn mode with Shift up. A note-on learns whatever Shift does,
///   and an item that does not use it hands it to the view's learn. A program change or CC with Shift held gets the
///   base answer; a pitch bend with Shift held plays.

#include "gui/menu_item/menu_item.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/oled_canvas/canvas.h"
#include "io/midi/learned_midi.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "spec/support/spec_main.h"

#include <optional>

using namespace CppSpec;

namespace {

/// @brief A cable this spec owns: `MIDICable` is abstract and no shipped subclass builds without a backend.
struct SpecCable final : ::MIDICable {
	void writeToFlash(uint8_t*) override {}
	char const* getDisplayName() override { return "learn dispatch"; }
	void sendMessage(MIDIMessage) override {}
	void sendSysex(const uint8_t*, int32_t) override {}
	[[nodiscard]] size_t sendBufferSpace() const override { return 0; }

protected:
	void writeReferenceAttributesToFile(Serializer&) override {}
};

SpecCable& cable() {
	static SpecCable instance;
	return instance;
}

/// @brief Which learn message reached a UI or an item, and with what.
enum class Got { Nothing, NoteOn, ProgramChange, Cc, PitchBend, Knob };

struct Record {
	Got got = Got::Nothing;
	int32_t channel = -1;
	int32_t number = -1;
};

/// @brief A current UI that records every learn offer and answers `answer`.
struct SpecUI final : UI {
	Record seen;
	bool answer = true;

	bool opened() override { return true; }
	bool noteOnReceivedForMidiLearn(MIDICable&, int32_t channel, int32_t note, int32_t) override {
		seen = {Got::NoteOn, channel, note};
		return answer;
	}
	bool pcReceivedForMidiLearn(MIDICable&, int32_t channel, int32_t program) override {
		seen = {Got::ProgramChange, channel, program};
		return answer;
	}
	bool ccReceivedForMidiLearn(MIDICable&, int32_t channel, int32_t cc, int32_t) override {
		seen = {Got::Cc, channel, cc};
		return answer;
	}
	bool pitchBendReceivedForMidiLearn(MIDICable&, int32_t channel, uint8_t, uint8_t) override {
		seen = {Got::PitchBend, channel, -1};
		return answer;
	}
	void renderOLED(deluge::hid::display::oled_canvas::Canvas&) override {}
	UIType getUIType() override { return UIType::NONE; }
};

/// @brief The focused menu item: records the learn it was given; `learnNoteOn` answers `uses_note`.
struct SpecItem final : MenuItem {
	Record seen;
	bool uses_note = true;

	bool learnNoteOn(MIDICable&, int32_t channel, int32_t note) override {
		seen = {Got::NoteOn, channel, note};
		return uses_note;
	}
	void learnProgramChange(MIDICable&, int32_t channel, int32_t program) override {
		seen = {Got::ProgramChange, channel, program};
	}
	void learnCC(MIDICable&, int32_t channel, int32_t cc, int32_t) override { seen = {Got::Cc, channel, cc}; }
	void learnKnob(MIDICable*, int32_t which, int32_t, int32_t channel) override { seen = {Got::Knob, channel, which}; }
};

constexpr uint8_t kPoisonChannel = 0xEE;

/// @brief The view's learn target: the PLAY command held for learn, its slot poisoned so a write shows.
struct ViewLearnHeld {
	::LearnedMIDI& slot = midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::PLAY)];
	::LearnedMIDI saved = slot;
	MidiLearn saved_thing = view.thingPressedForMidiLearn;
	::LearnedMIDI* saved_learned = view.learnedThing;

	ViewLearnHeld() {
		view.thingPressedForMidiLearn = MidiLearn::PLAY_BUTTON;
		view.learnedThing = &slot;
		slot.cable = nullptr;
		slot.channelOrZone = kPoisonChannel;
		slot.noteOrCC = 0;
	}
	~ViewLearnHeld() {
		slot = saved;
		view.thingPressedForMidiLearn = saved_thing;
		view.learnedThing = saved_learned;
	}
	[[nodiscard]] bool written() const { return slot.channelOrZone != kPoisonChannel; }
};

/// @brief Learn mode and Shift for one example, put back after it.
struct Mode {
	int32_t saved_mode = currentUIMode;
	explicit Mode(bool learning, bool shift = false) {
		currentUIMode = learning ? UI_MODE_MIDI_LEARN : UI_MODE_NONE;
		if (shift) {
			Buttons::commandToggleShift(true);
		}
	}
	~Mode() {
		Buttons::commandToggleShift(false);
		currentUIMode = saved_mode;
	}
};

/// @brief A song with no tracks, and @p ui as the only open UI, for one example. A message the dispatcher passes on
///        reaches no instrument.
struct Current {
	Song* saved_song = currentSong;
	Song* song = deluge::scenes::spec::detail::make_bare_song();
	explicit Current(UI& ui) {
		currentSong = song;
		nullifyUIs();
		setRootUILowLevel(&ui);
	}
	~Current() {
		nullifyUIs();
		currentSong = saved_song;
	}
};

/// @brief `soundEditor` focused on @p item for one example.
struct EditorOn {
	uint8_t saved_depth = soundEditor.navigationDepth;
	MenuItem* saved_item = soundEditor.menuItemNavigationRecord[0];
	explicit EditorOn(MenuItem& item) {
		soundEditor.navigationDepth = 0;
		soundEditor.menuItemNavigationRecord[0] = &item;
	}
	~EditorOn() {
		soundEditor.navigationDepth = saved_depth;
		soundEditor.menuItemNavigationRecord[0] = saved_item;
	}
};

/// @brief An MPE lower zone on the cable's input for one example: channels 1..@p last are member channels.
struct MpeZone {
	MIDIPort& port = cable().ports[MIDI_DIRECTION_INPUT_TO_DELUGE];
	uint8_t saved = port.mpeLowerZoneLastMemberChannel;
	explicit MpeZone(uint8_t last) { port.mpeLowerZoneLastMemberChannel = last; }
	~MpeZone() { port.mpeLowerZoneLastMemberChannel = saved; }
};

} // namespace

// clang-format off
describe learn_dispatch_offers("PlaybackHandler offers learnable messages to the current UI", $ {
	it("offers a note-on, in or out of learn mode", _ {
		SpecUI ui;
		Current current{ui};
		for (const bool learning : {false, true}) {
			Mode mode{learning};
			ui.seen = {};
			bool thru = false;
			playbackHandler.noteMessageReceived(cable(), true, 2, 60, 100, &thru);
			expect(ui.seen.got == Got::NoteOn).to_be_true();
			expect(ui.seen.number).to_equal(60);
		}
	});

	it("never offers a note-off, so a note held across learn mode does not stick", _ {
		SpecUI ui;
		Current current{ui};
		Mode mode{true};
		bool thru = false;
		playbackHandler.noteMessageReceived(cable(), false, 2, 60, 0, &thru);
		expect(ui.seen.got == Got::Nothing).to_be_true();
	});

	it("offers a program change, in or out of learn mode", _ {
		SpecUI ui;
		Current current{ui};
		for (const bool learning : {false, true}) {
			Mode mode{learning};
			ui.seen = {};
			playbackHandler.programChangeReceived(cable(), 3, 7);
			expect(ui.seen.got == Got::ProgramChange).to_be_true();
			expect(ui.seen.channel).to_equal(3);
			expect(ui.seen.number).to_equal(7);
		}
	});

	it("offers a CC on a channel outside any MPE zone", _ {
		SpecUI ui;
		Current current{ui};
		Mode mode{true};
		bool thru = false;
		playbackHandler.midiCCReceived(cable(), 15, 74, 64, &thru);
		expect(ui.seen.got == Got::Cc).to_be_true();
		expect(ui.seen.number).to_equal(74);
	});

	it("does not offer a CC on an MPE member channel", _ {
		SpecUI ui;
		Current current{ui};
		MpeZone zone{4};
		Mode mode{true};
		bool thru = false;
		playbackHandler.midiCCReceived(cable(), 2, 74, 64, &thru);
		expect(ui.seen.got == Got::Nothing).to_be_true();
	});

	it("offers a pitch bend outside any MPE zone, and not an MPE one", _ {
		SpecUI ui;
		Current current{ui};
		Mode mode{true};
		bool thru = false;
		playbackHandler.pitchBendReceived(cable(), 15, 0, 64, &thru);
		expect(ui.seen.got == Got::PitchBend).to_be_true();

		MpeZone zone{4};
		ui.seen = {};
		playbackHandler.pitchBendReceived(cable(), 2, 0, 64, &thru);
		expect(ui.seen.got == Got::Nothing).to_be_true();
	});

	it("stops at a UI that answers true: the view's learn does not also run", _ {
		SpecUI ui;
		Current current{ui};
		Mode mode{true};
		ViewLearnHeld held;
		bool thru = false;
		playbackHandler.noteMessageReceived(cable(), true, 2, 60, 100, &thru);
		playbackHandler.programChangeReceived(cable(), 3, 7);
		playbackHandler.midiCCReceived(cable(), 15, 74, 64, &thru);
		expect(held.written()).to_be_false();
	});
});

describe learn_dispatch_base("the base UI answer is the view's learn, in learn mode only", $ {
	it("outside learn mode uses nothing, and the view's learn is not reached", _ {
		Current current{sessionView};
		Mode mode{false};
		ViewLearnHeld held;
		UI& ui = sessionView;
		expect(ui.noteOnReceivedForMidiLearn(cable(), 2, 60, 100)).to_be_false();
		expect(ui.pcReceivedForMidiLearn(cable(), 2, 7)).to_be_false();
		expect(ui.ccReceivedForMidiLearn(cable(), 2, 74, 64)).to_be_false();
		expect(ui.pitchBendReceivedForMidiLearn(cable(), 2, 0, 64)).to_be_false();
		expect(held.written()).to_be_false();
	});

	it("in learn mode a note-on reaches the view's learn and is used", _ {
		Current current{sessionView};
		Mode mode{true};
		ViewLearnHeld held;
		UI& ui = sessionView;
		expect(ui.noteOnReceivedForMidiLearn(cable(), 2, 60, 100)).to_be_true();
		expect(held.slot.channelOrZone).to_equal(2);
		expect(held.slot.noteOrCC).to_equal(60);
	});

	it("in learn mode a CC reaches the view's learn as a note-on on channel + IS_A_CC, and is used", _ {
		Current current{sessionView};
		Mode mode{true};
		ViewLearnHeld held;
		UI& ui = sessionView;
		expect(ui.ccReceivedForMidiLearn(cable(), 2, 74, 64)).to_be_true();
		expect(held.slot.channelOrZone).to_equal(2 + IS_A_CC);
		expect(held.slot.noteOrCC).to_equal(74);
	});

	it("in learn mode a program change is used, and a pitch bend is not", _ {
		Current current{sessionView};
		Mode mode{true};
		UI& ui = sessionView;
		expect(ui.pcReceivedForMidiLearn(cable(), 2, 7)).to_be_true();
		expect(ui.pitchBendReceivedForMidiLearn(cable(), 2, 0, 64)).to_be_false();
	});
});

describe learn_dispatch_sound_editor("SoundEditor learns into the focused item", $ {
	it("outside learn mode uses nothing and the item learns nothing", _ {
		SpecItem item;
		Current current{soundEditor};
		EditorOn on{item};
		Mode mode{false};
		UI& ui = soundEditor;
		expect(ui.noteOnReceivedForMidiLearn(cable(), 2, 60, 100)).to_be_false();
		expect(ui.pcReceivedForMidiLearn(cable(), 2, 7)).to_be_false();
		expect(ui.ccReceivedForMidiLearn(cable(), 2, 74, 64)).to_be_false();
		expect(ui.pitchBendReceivedForMidiLearn(cable(), 2, 0, 64)).to_be_false();
		expect(item.seen.got == Got::Nothing).to_be_true();
	});

	it("in learn mode with Shift up, each message learns into the item and is used", _ {
		SpecItem item;
		Current current{soundEditor};
		EditorOn on{item};
		Mode mode{true};
		UI& ui = soundEditor;
		expect(ui.noteOnReceivedForMidiLearn(cable(), 2, 60, 100)).to_be_true();
		expect(item.seen.got == Got::NoteOn).to_be_true();
		expect(ui.pcReceivedForMidiLearn(cable(), 3, 7)).to_be_true();
		expect(item.seen.got == Got::ProgramChange).to_be_true();
		expect(item.seen.number).to_equal(7);
		expect(ui.ccReceivedForMidiLearn(cable(), 4, 74, 64)).to_be_true();
		expect(item.seen.got == Got::Cc).to_be_true();
		expect(item.seen.number).to_equal(74);
		expect(ui.pitchBendReceivedForMidiLearn(cable(), 5, 0, 64)).to_be_true();
		expect(item.seen.got == Got::Knob).to_be_true();
		expect(item.seen.number).to_equal(128);
		expect(item.seen.channel).to_equal(5);
	});

	it("a note-on the item does not use goes to the view's learn, and is used", _ {
		SpecItem item;
		item.uses_note = false;
		Current current{soundEditor};
		EditorOn on{item};
		Mode mode{true};
		ViewLearnHeld held;
		UI& ui = soundEditor;
		expect(ui.noteOnReceivedForMidiLearn(cable(), 2, 60, 100)).to_be_true();
		expect(item.seen.got == Got::NoteOn).to_be_true();
		expect(held.slot.noteOrCC).to_equal(60);
	});

	it("with Shift held a note-on still learns into the item", _ {
		SpecItem item;
		Current current{soundEditor};
		EditorOn on{item};
		Mode mode{true, true};
		UI& ui = soundEditor;
		expect(ui.noteOnReceivedForMidiLearn(cable(), 2, 60, 100)).to_be_true();
		expect(item.seen.got == Got::NoteOn).to_be_true();
	});

	it("with Shift held a CC goes to the view's learn, a program change is used, a pitch bend plays", _ {
		SpecItem item;
		Current current{soundEditor};
		EditorOn on{item};
		Mode mode{true, true};
		ViewLearnHeld held;
		UI& ui = soundEditor;
		expect(ui.ccReceivedForMidiLearn(cable(), 2, 74, 64)).to_be_true();
		expect(held.slot.noteOrCC).to_equal(74);
		expect(ui.pcReceivedForMidiLearn(cable(), 2, 7)).to_be_true();
		expect(ui.pitchBendReceivedForMidiLearn(cable(), 2, 0, 64)).to_be_false();
		expect(item.seen.got == Got::Nothing).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(learn_dispatch_spec, learn_dispatch_offers, learn_dispatch_base, learn_dispatch_sound_editor)
