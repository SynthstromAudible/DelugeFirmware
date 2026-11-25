#pragma once

/// @file
/// @brief What a gold knob's push and a mod button's press do, as functions of the song's records: the settings they
///        write, the knob views they change, and the popup text they show.

#include "effects/state.h"
#include "gui/l10n/param_names.h"
#include "project/controller_mapping.h"
#include "project/fx_chain.h"
#include "project/instrument_slot.h"
#include "project/song.h"
#include "serialization/value.h"
#include "ui/controls/board.h"
#include "ui/controls/sink.h"
#include "ui/scenes/bind/knob_assignment.h"
#include "ui/scenes/bind/knob_focus.h"
#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/bind/ports/sound_facts.h"
#include "ui/scenes/bind/ports/track_facts.h"
#include "ui/scenes/bind/ports/write_sink.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace deluge::ui::scenes::services {

/// @brief The gesture's context: what a push reads that the song's records do not hold, and the ports it reads the rest
///        through.
struct PushInputs {
	bool button_held{false};   ///< the mode's mod button is held, so the push re-shows its button's popup
	bool clip_context{false};  ///< the knobs are on a clip, not the song (the stutter button's names)
	ui::controls::Board board; ///< the board the knobs are on
	/// the ports a push reads the owner's effects and the device facts through; none reads the defaults
	const bind::Ports* ports{nullptr};
};

using bind::EffectOwner;

/// @brief One effect setting a push writes, in its Edit form.
struct EffectWrite {
	std::string_view path;
	serialization::Value value;
};

/// @brief What a push does.
struct KnobPush {
	std::vector<EffectWrite> effects;                       ///< the focus's owner's effect settings, in order
	std::vector<std::pair<std::string_view, int32_t>> song; ///< song settings by menu value (a reverb preset)
	std::optional<std::pair<std::string_view, std::string_view>> chain_view; ///< the chain's view key and word
	std::optional<std::vector<project::ControllerMapping>> controls;         ///< the sound's knobs after a cycle
	std::optional<bool> stutter;                                             ///< begin (true) or end the stutter
	std::optional<bind::CompView> comp; ///< the song's compressor view after the push
	bool midi_cc_select{false};         ///< a MIDI track's knob: pick its CC with the select encoder
	std::string popup;                  ///< the held mod button's popup, shown again
	std::string notice;                 ///< else, a passing notice
	bool edited{false};                 ///< the push edited the instrument
};

/// @brief Whose effect settings @p focus's knobs push: a sound's slot, the song's chain, a kit's or audio track's.
[[nodiscard]] std::optional<EffectOwner> effect_owner(const bind::KnobFocus& focus);

/// @brief The effect settings of @p focus's owner, as the host's facts hold them.
/// @param focus What the knobs point at.
/// @param tracks The track facts: a chain's effects.
/// @param sound The sound facts: a sound's typed state.
/// @return The owner's effects; nullptr for an owner with none, or one the facts do not resolve.
[[nodiscard]] const effects::EffectStates* owner_effects(const bind::KnobFocus& focus, const bind::TrackFacts& tracks,
                                                         const bind::SoundFacts& sound);

/// @brief The modes of the sound's envelopes the knobs turn, which name some of their params.
/// @param focus What the knobs turn.
/// @param sound The sounds' states.
/// @return The modes; nullopt where the knobs turn no sound.
std::optional<l10n::EnvelopeModes> owner_envelope_modes(const bind::KnobFocus& focus, const bind::SoundFacts& sound);

/// @brief What pressing (or releasing, @p on false) gold knob @p knob of @p focus does.
/// @param song   The song's records.
/// @param focus  What the knobs point at.
/// @param knob   0 the lower, 1 the upper.
/// @param on     Press (true) or release.
/// @param gesture The gesture's context. The owner's effects come from the track and sound ports in @p gesture: with
///                no track or sound port the push reads a new chain's effects. The device facts come from its
///                controls port: with none the push reads the turn's sources (the alternative delay knobs off).
[[nodiscard]] KnobPush knob_push(const project::Song& song, const bind::KnobFocus& focus, int32_t knob, bool on,
                                 const PushInputs& gesture);

/// @brief The popup a press of mod button @p button shows over @p focus's knobs, with the mode already @p button.
/// @param song    The song's records.
/// @param focus   What the knobs point at.
/// @param button  The mod button.
/// @param gesture The gesture's context. The owner's effects come from the track and sound ports in @p gesture: with
///                no track or sound port the popup reads a new chain's effects. The device facts come from its
///                controls port: with none the popup reads the turn's sources (the alternative delay knobs off).
/// @return The popup's text; empty for none.
[[nodiscard]] std::string mod_button_popup(const project::Song& song, const bind::KnobFocus& focus, int32_t button,
                                           const PushInputs& gesture);

/// @brief Whether a stutter-rate knob on an owner with effects @p owner is quantized: the song's stutter's setting
///        (@p song) where the owner plays the song's stutter, else its own.
[[nodiscard]] constexpr bool stutter_quantized(const effects::EffectStates& owner, const effects::EffectStates& song) {
	return owner.stutter.use_song_stutter ? song.stutter.quantized : owner.stutter.quantized;
}

/// @brief What turning a gold knob on the song's compressor setting @p arm by @p offset does.
struct CompTurn {
	std::string_view path;      ///< the compressor setting
	serialization::Value value; ///< its new value
	std::string title;          ///< the notification's title: the setting's name
	std::string value_text;     ///< and its value, in its units
	int32_t knob_pos = 0;       ///< the knob's new position, -64..64, which its indicator shows
};

/// @brief The knob position (-64..64) the song's compressor setting @p arm is at.
[[nodiscard]] int32_t comp_knob_pos(const bind::CompArm& arm, const effects::EffectStates& effects);

/// @brief Turn the song's compressor setting @p arm by @p offset knob steps.
[[nodiscard]] CompTurn comp_turn(const bind::CompArm& arm, const effects::EffectStates& effects, int32_t offset);

/// @brief Send @p push's requests: its song settings to @p sink, and its effect settings, chain view, knobs and stutter
/// to @p controls.
/// @return Whether the sinks took every one.
bool apply_push(bind::WriteSink& sink, ui::controls::Sink& controls, const bind::KnobFocus& focus,
                const KnobPush& push);

/// @brief A note length's name, as the song's tick magnitude names it (`Song::getNoteLengthName`).
/// @param note_length    The length, in ticks of the song's input.
/// @param tick_magnitude The song's input tick magnitude.
[[nodiscard]] std::string note_length_name(uint32_t note_length, int32_t tick_magnitude);

} // namespace deluge::ui::scenes::services
