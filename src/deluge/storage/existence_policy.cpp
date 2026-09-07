#include "storage/existence_policy.h"

namespace deluge::storage {

Presence presence_of(const std::expected<bool, deluge::io::Status>& present) {
	if (!present) {
		return Presence::Undeterminable;
	}
	return *present ? Presence::Present : Presence::Absent;
}

Bootstrap bootstrap_action(const std::expected<bool, deluge::io::Status>& present) {
	switch (presence_of(present)) {
	case Presence::Present:
		return Bootstrap::Load;
	case Presence::Absent:
		return Bootstrap::WriteDefaults;
	case Presence::Undeterminable:
		return Bootstrap::UseInMemoryOnly;
	}
	// Unreachable for a valid Presence; UseInMemoryOnly is the safe fallback (writes nothing).
	return Bootstrap::UseInMemoryOnly;
}

SaveAction save_action(const std::expected<bool, deluge::io::Status>& present, bool mayOverwrite) {
	switch (presence_of(present)) {
	case Presence::Absent:
		return SaveAction::Write;
	case Presence::Present:
		return mayOverwrite ? SaveAction::Write : SaveAction::Occupied;
	case Presence::Undeterminable:
		return SaveAction::Refuse;
	}
	// No `default:` above, deliberately -- see this header's warning, so -Wswitch catches an
	// unhandled enumerator. Unreachable for a valid one; Refuse is the safe reading regardless.
	return SaveAction::Refuse;
}

} // namespace deluge::storage
