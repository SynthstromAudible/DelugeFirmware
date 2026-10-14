#pragma once

#include "definitions_cxx.hpp" // Error

class Song;
namespace deluge::project {
class Song;
}

namespace deluge::edits {

/// @brief What an Edit executes against.
///
/// `song` is the engine-side controller, needed by Edits that delegate their mutation to engine objects.
struct Context {
	::Song& song;                 ///< Engine-side song controller.
	deluge::project::Song& model; ///< The authoritative project model.
};

/// @brief A reified forward mutation of persistent state.
///
/// The "Command" of docs/dev/project_model_north_star.md, named Edit to avoid colliding with MIDI/HID command
/// handling. An Edit addresses its target by stable id, resolves at apply time, mutates, and
/// produces the Consequence(s) that invert it into the open Action — the do-site and the
/// undo-record-site are the same object. Edits are UI/off-RT constructs: audio-thread-originated
/// writes (recorded automation) stay on the reconcile path and are NOT Edits.
class Edit {
public:
	virtual ~Edit() = default;

	/// @brief Perform the mutation (and record its undo Consequence(s)).
	///
	/// Any resolved pointer lives only for the duration of this call: an ephemeral borrow, never a stored member.
	///
	/// @param ctx The song and project model to mutate.
	/// @return Error::NONE on success.
	virtual Error apply(Context& ctx) = 0;

	/// @brief Snapshot-boundary event feed, emitted through the single dispatch point after a successful apply().
	///
	/// Default no-op; an Edit overrides it to publish its model-changed event (its durable {address, value}) for
	/// the engine to turn into a snapshot patch. The in-line engine poke in apply() still runs; the event is a
	/// parallel consumer. Off-RT (Edits are UI/off-audio-thread constructs).
	///
	/// @param ctx The song and project model, as passed to apply().
	virtual void emitEvent(Context& /*ctx*/) {}
};

/// @brief The single dispatch point every Edit goes through.
///
/// Applies the edit and, on success, emits its model-changed event (see Edit::emitEvent) for the engine to turn
/// into a snapshot update.
///
/// @param ctx  The song and project model to mutate.
/// @param edit The Edit to apply.
/// @return The Error from Edit::apply.
Error apply(Context& ctx, Edit& edit);

} // namespace deluge::edits
