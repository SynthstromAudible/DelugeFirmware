#pragma once

/// @brief `MidiCommandItem` -- the `CustomItem` behind the twelve global-MIDI-command learn items.
///
/// The port of `deluge::gui::menu_item::midi::Command` (`gui/menu_item/midi/command.{h,cpp}`): a
/// `MenuItem` parameterised by one `GlobalMIDICommand`, whose whole job is to bind the next MIDI
/// message the user sends to `midiEngine.globalMIDICommands[that command]`.
///
/// Legacy's twelve instances are the `midi::Command` globals at `gui/ui/menus.cpp:1416-1428`, and
/// this file declares the twelve that replace them one for one. `settings.h` hangs them off
/// `nodes::midi_command_play` .. `nodes::midi_command_shift`, in `midiCommandsMenu`'s own order
/// (`menus.cpp:1430-1436`).
///
/// **What is ported, member by member** (`command.cpp`):
///
///   * `learnNoteOn` -> `learn_note_on`: writes `cable`/`channelOrZone`/`noteOrCC` into the slot.
///   * `learnCC` -> `learn_cc`: `learnNoteOn(cable, channel + IS_A_CC, cc)`, and only when
///     `value != 0` -- legacy ignores the note-off half of a momentary controller.
///   * `learnProgramChange` -> `learn_program_change`: `learnNoteOn(cable, channel + IS_A_PC, pc)`,
///     except on `GlobalMIDICommand::FILL`, which refuses with the `CANT_LEARN_PC` popup.
///   * `unlearnAction` -> `unlearn`: `clear()`s the slot.
///   * `selectEncoderAction` -> the `f.turn()` arm of `render`: **any** select-encoder motion clears
///     the slot. (Legacy really does clear on either direction; the item has no value to walk.)
///   * `drawPixelsForOled` -> `render`.
///   * `shouldBlinkLearnLed` -> `wants_learn_blink`, which is `true` on all twelve.
///   * `allowsLearnMode` has no port: it gates legacy's `UI_MODE_MIDI_LEARN` bookkeeping
///     (`sound_editor.cpp:474-497`), which is UI-mode state this layer does not own. The routing
///     that eventually calls `MenuScene::learn_*` is where the equivalent gate belongs.
///   * `beginSession` is empty in legacy (`command.cpp:30-31`), so `on_focus` keeps its
///     do-nothing default rather than being overridden to an empty body.
///
/// @note **No popup on a successful learn.** Legacy pops "LEARNED"/"UNLEARNED" only when the item
///       is *not* the current menu item (`command.cpp:96-104`, `:110-122`) -- i.e. when a *different*
///       screen's learn press reached it. In this port a learn only ever arrives at the focused
///       item (`MenuScene::learn_*` forwards to `focused()`), so the `renderUIsForOled()` branch is
///       the only reachable one: `Dirty::Oled` is raised instead, and the next render redraws.

#include "scenes/menu/node.h"

#include "definitions_cxx.hpp" // GlobalMIDICommand -- one of the three root-level headers this
                               // layer may name (src/scenes/CMakeLists.txt is the authority)

#include <cstdint>

namespace deluge::scenes::menu::nodes {

/// @brief One global-MIDI-command learn item.
///
/// Not a placeholder (`is_placeholder()` keeps its `false` default): this is the finished item, and
/// `validate_tree` stops reporting these twelve nodes.
class MidiCommandItem final : public CustomItem {
public:
	/// @param command Which slot of `midiEngine.globalMIDICommands` this item binds -- legacy's
	///                `Command::commandNumber` (`midi/command.h:41`), spelled as the real enum
	///                rather than as a number so a swapped pair is a type-checked mistake rather
	///                than a silent one.
	explicit constexpr MidiCommandItem(GlobalMIDICommand command) : command_{command} {}

	void render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;

	bool learn_note_on(::MIDICable& cable, int32_t channel, int32_t note) const override;
	bool learn_cc(::MIDICable& cable, int32_t channel, int32_t cc, int32_t value) const override;
	bool learn_program_change(::MIDICable& cable, int32_t channel, int32_t program) const override;
	void unlearn() const override;
	bool wants_learn_blink() const override { return true; } ///< `command.h:34`, true on all twelve.

	/// @brief Which command this item binds. For specs, and for the twelve-way mapping check.
	constexpr GlobalMIDICommand command() const { return command_; }

private:
	GlobalMIDICommand command_;
};

/// @name The twelve instances, one per learnable `GlobalMIDICommand`.
///
/// `inline const` rather than `inline constexpr`, exactly as `EditNameItem`'s and `ResetItem`'s
/// instances are: the type has a vtable, and only the *addresses* need to be constant expressions,
/// which is all `custom()` asks for.
///
/// Named after the node each backs (`settings.h`), and paired with the legacy global it replaces
/// (`gui/ui/menus.cpp:1416-1428`).
/// @{
inline const MidiCommandItem kMidiCommandPlayItem{GlobalMIDICommand::PLAY}; ///< `playMidiCommand`
inline const MidiCommandItem kMidiCommandRestartItem{
    //
    GlobalMIDICommand::PLAYBACK_RESTART};                                       ///< `playbackRestartMidiCommand`
inline const MidiCommandItem kMidiCommandRecordItem{GlobalMIDICommand::RECORD}; ///< `recordMidiCommand`
inline const MidiCommandItem kMidiCommandTapItem{GlobalMIDICommand::TAP};       ///< `tapMidiCommand`
inline const MidiCommandItem kMidiCommandUndoItem{GlobalMIDICommand::UNDO};     ///< `undoMidiCommand`
inline const MidiCommandItem kMidiCommandRedoItem{GlobalMIDICommand::REDO};     ///< `redoMidiCommand`
inline const MidiCommandItem kMidiCommandLoopItem{GlobalMIDICommand::LOOP};     ///< `loopMidiCommand`
inline const MidiCommandItem kMidiCommandLayeringLoopItem{
    //
    GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING};                           ///< `loopContinuousLayeringMidiCommand`
inline const MidiCommandItem kMidiCommandFillItem{GlobalMIDICommand::FILL}; ///< `fillMidiCommand`
inline const MidiCommandItem kMidiCommandTransposeItem{GlobalMIDICommand::TRANSPOSE}; ///< `transposeMidiCommand`
inline const MidiCommandItem kMidiCommandNextSongItem{GlobalMIDICommand::NEXT_SONG};  ///< `nextSongMidiCommand`
inline const MidiCommandItem kMidiCommandShiftItem{GlobalMIDICommand::SHIFT};         ///< `shiftMidiCommand`
/// @}

} // namespace deluge::scenes::menu::nodes
