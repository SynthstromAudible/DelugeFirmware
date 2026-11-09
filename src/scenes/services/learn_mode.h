#pragma once

/// @file
/// @brief `LearnMode` -- the one question the rest of the scenes layer asks MIDI learn.
///
/// **This header is the boundary between `GlobalControls` and `MidiLearn`.** It exists because `View::buttonAction`
/// (`gui/views/view.cpp:141-488`) asks `currentUIMode == UI_MODE_MIDI_LEARN` in three of its arms and then does
/// *transport* work in the same function -- and splitting `View` in two without a seam would mean spelling that test
/// twice, in two services, from two copies of the mode. Duplicating a question in both halves of a
/// split is how `View` reached 2,908 lines in the first place.
///
/// The rule: **the service that owns the MODE owns the arm, and the
/// other asks it.** `MidiLearn` owns `UI_MODE_MIDI_LEARN` (`View::startMIDILearn`
/// `view.cpp`, `View::endMIDILearn`), so it owns every arm whose *whole body*
/// is learn work -- the `TAP_TEMPO` learn branch (`view.cpp:150-168`), the `LEARN` button itself
/// (`:185-196`), and the `PLAY` (`:199-212`) and `RECORD` (`:215-228`) arms that exist only under
/// learn. `GlobalControls` owns the rest and asks `active()` before running the transport arms
/// those two share a button with.
///
/// @note `services::MidiLearn` implements this (`services/midi_learn.h`). `MidiLearn` is routed one
///       step after `GlobalControls` so that the arms this service declines are claimed rather than dropped.

namespace deluge::scenes::services {

/// @brief Is MIDI learn mode engaged?
///
/// One virtual call, deliberately: a `std::function` would allocate, and a raw function pointer
/// would need a context argument that the one implementation does not want. The interface has no
/// state of its own and no ownership -- whoever constructs a `GlobalControls` passes a reference
/// to something that outlives it.
class LearnMode {
public:
	virtual ~LearnMode() = default;

	/// @brief Legacy's `currentUIMode == UI_MODE_MIDI_LEARN` (`View::buttonAction`'s TAP_TEMPO, PLAY and RECORD arms).
	/// @return True while learn mode is engaged.
	virtual bool active() const = 0;
};

/// @brief The "no learn service yet" answer: never active.
class NoLearnMode final : public LearnMode {
public:
	bool active() const override { return false; }
};

/// @brief The single shared `NoLearnMode`, for a `GlobalControls` built without a real one.
///
/// A function-local static for the same reason `shell::null_service()` is one: one instance across
/// every translation unit, with no static-initialisation order to get wrong.
/// @return The shared instance.
inline LearnMode& no_learn_mode() {
	static NoLearnMode instance;
	return instance;
}

} // namespace deluge::scenes::services
