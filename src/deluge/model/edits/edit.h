#pragma once

#include "definitions_cxx.hpp" // Error

class Song;
namespace deluge::project {
class Song;
}

namespace deluge::edits {

/// What an Edit executes against. `model` is the authoritative project model; `song` is the
/// legacy engine-side controller, present for the funnel phase only (Edits that still delegate
/// their mutation to engine objects) — it goes away as owners flip to model authority (M3,
/// docs/dev/project_model_migration_plan.md).
struct Context {
	::Song& song;
	deluge::project::Song& model;
};

/// A reified forward mutation of persistent state (M1 of the migration plan; the "Command" of
/// docs/dev/project_model_north_star.md, named Edit to avoid colliding with MIDI/HID command
/// handling). An Edit addresses its target by stable id, resolves at apply time, mutates, and
/// produces the Consequence(s) that invert it into the open Action — the do-site and the
/// undo-record-site are the same object. Edits are UI/off-RT constructs: audio-thread-originated
/// writes (recorded automation) stay on the reconcile path and are NOT Edits.
class Edit {
public:
	virtual ~Edit() = default;

	/// Perform the mutation (and record its undo Consequence(s)). Any resolved pointer lives only
	/// for the duration of this call — an ephemeral borrow, never a stored member.
	virtual Error apply(Context& ctx) = 0;
};

/// The single dispatch point every Edit goes through. Today: applies the edit. From M4: also the
/// emission point for the model-changed event the engine turns into a snapshot update.
Error apply(Context& ctx, Edit& edit);

} // namespace deluge::edits
