#pragma once

/// @brief `MidiCommandItem` -- the `CustomItem` behind the twelve global-MIDI-command learn items.
///
/// Mirrors main's `deluge::gui::menu_item::midi::Command` (`origin/main:src/deluge/gui/menu_item/midi/command.h`): a
/// `MenuItem` parameterised by one global command, whose whole job is to bind the next MIDI message the user sends to
/// that command. The binding is read through `bind::MidiDevices::global_command` and written through
/// `bind::LearnSink::learn_global`.
///
/// Legacy's twelve instances are the `midi::Command` globals (`playbackRestartMidiCommand` .. `shiftMidiCommand` in
/// `origin/main:src/deluge/gui/ui/menus.cpp`), and this file declares the twelve that replace them one for one.
/// `nodes/settings.h` hangs them off `nodes::midi_command_play` .. `nodes::midi_command_shift`, in `midiCommandsMenu`'s
/// own order (`origin/main:src/deluge/gui/ui/menus.cpp`).
///
/// **What is ported, member by member** (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`):
///
///   * `learnNoteOn` -> `learn_note_on`: learns the device, the channel or zone, and the note.
///   * `learnCC` -> `learn_cc`: a note-on learn at `channel + project::kFirstCcChannel`, and only when
///     `value != 0` -- legacy ignores the note-off half of a momentary controller.
///   * `learnProgramChange` -> `learn_program_change`: a note-on learn at
///     `channel + project::kFirstProgramChangeChannel`, except on `GlobalCommand::FILL`, which refuses with the
///     `CANT_LEARN_PC` popup.
///   * `unlearnAction` -> `unlearn`: unlearns the command.
///   * `selectEncoderAction` -> `handle`'s select-turn arm: **any** select-encoder motion unlearns the command.
///     (Legacy really does clear on either direction; the item has no value to walk.)
///   * `drawPixelsForOled` -> `render`.
///   * `shouldBlinkLearnLed` -> `wants_learn_blink`, which is `true` on all twelve.
///   * `allowsLearnMode` has no port: it gates legacy's `UI_MODE_MIDI_LEARN` bookkeeping (`SoundEditor::buttonAction`
///     in `origin/main:src/deluge/gui/ui/sound_editor.cpp`); `MenuLearn::offer`, which calls the learn members, is
///     where the equivalent gate is.
///   * `beginSession` is empty in legacy (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`), so `on_focus` keeps
///     its do-nothing default rather than being overridden to an empty body.
///
/// @note **The popup is the menu's.** Legacy pops "LEARNED"/"UNLEARNED" only when the item is *not* the
///       current menu item -- a learn reached through its list -- and redraws otherwise. Here the item raises
///       `Dirty::Oled`, and `MenuLearn` pops when the item it learned into is not the focused one.

#include "scenes/menu/node.h"

#include "project/device_settings.h"

#include <cstdint>

namespace deluge::scenes::menu::nodes {

/// @brief One global-MIDI-command learn item.
///
/// Not a placeholder (`is_placeholder()` keeps its `false` default): this is the finished item, and
/// `validate_tree` stops reporting these twelve nodes.
class MidiCommandItem final : public CustomItem {
public:
	/// @param command Which global command this item binds -- legacy's `Command::commandNumber`
	///                (`origin/main:src/deluge/gui/menu_item/midi/command.h`), spelled as the enum rather than as a
	///                number so a swapped pair is a type-checked mistake rather than a silent one.
	explicit constexpr MidiCommandItem(deluge::project::GlobalCommand command) : command_{command} {}

	void render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const override;
	/// @brief The select press, which leaves the item. The select turn is not claimed, so it reaches `handle` as an
	///        event, where it unlearns the command.
	deluge::ui::widgets::core::InputClaims owned_input(const MenuContext& /*ctx*/,
	                                                   const MenuState& /*state*/) const override {
		return {.select = true};
	}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ui_ctx, const MenuContext& ctx,
	                                 MenuState& state) const override;

	bool learn_note_on(const MenuContext& ctx, deluge::midi::DeviceId device, int32_t channel,
	                   int32_t note) const override;
	bool learn_cc(const MenuContext& ctx, deluge::midi::DeviceId device, int32_t channel, int32_t cc,
	              int32_t value) const override;
	bool learn_program_change(const MenuContext& ctx, deluge::midi::DeviceId device, int32_t channel,
	                          int32_t program) const override;
	bool unlearn(const MenuContext& ctx) const override;
	bool wants_learn_blink() const override {
		return true;
	} ///< `Command::shouldBlinkLearnLed` (`origin/main:src/deluge/gui/menu_item/midi/command.h`), true on all twelve.

	/// @brief Which command this item binds. For specs, and for the twelve-way mapping check.
	constexpr deluge::project::GlobalCommand command() const { return command_; }

private:
	deluge::project::GlobalCommand command_;
};

/// @name The twelve instances, one per global command.
///
/// `inline const` rather than `inline constexpr`, exactly as `EditNameItem`'s and `ResetItem`'s
/// instances are: the type has a vtable, and only the *addresses* need to be constant expressions,
/// which is all `custom()` asks for.
///
/// Named after the node each backs (`nodes/settings.h`), and paired with the legacy global it replaces
/// (`origin/main:src/deluge/gui/ui/menus.cpp`).
/// @{
inline const MidiCommandItem kMidiCommandPlayItem{deluge::project::GlobalCommand::PLAY}; ///< `playMidiCommand`
inline const MidiCommandItem kMidiCommandRestartItem{
    //
    deluge::project::GlobalCommand::PLAYBACK_RESTART}; ///< `playbackRestartMidiCommand`
inline const MidiCommandItem kMidiCommandRecordItem{deluge::project::GlobalCommand::RECORD}; ///< `recordMidiCommand`
inline const MidiCommandItem kMidiCommandTapItem{deluge::project::GlobalCommand::TAP};       ///< `tapMidiCommand`
inline const MidiCommandItem kMidiCommandUndoItem{deluge::project::GlobalCommand::UNDO};     ///< `undoMidiCommand`
inline const MidiCommandItem kMidiCommandRedoItem{deluge::project::GlobalCommand::REDO};     ///< `redoMidiCommand`
inline const MidiCommandItem kMidiCommandLoopItem{deluge::project::GlobalCommand::LOOP};     ///< `loopMidiCommand`
inline const MidiCommandItem kMidiCommandLayeringLoopItem{
    //
    deluge::project::GlobalCommand::LOOP_CONTINUOUS_LAYERING}; ///< `loopContinuousLayeringMidiCommand`
inline const MidiCommandItem kMidiCommandFillItem{deluge::project::GlobalCommand::FILL}; ///< `fillMidiCommand`
inline const MidiCommandItem kMidiCommandTransposeItem{
    deluge::project::GlobalCommand::TRANSPOSE}; ///< `transposeMidiCommand`
inline const MidiCommandItem kMidiCommandNextSongItem{
    deluge::project::GlobalCommand::NEXT_SONG};                                            ///< `nextSongMidiCommand`
inline const MidiCommandItem kMidiCommandShiftItem{deluge::project::GlobalCommand::SHIFT}; ///< `shiftMidiCommand`
/// @}

} // namespace deluge::scenes::menu::nodes
