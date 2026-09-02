#pragma once

/// @file
/// @brief The two guards any spec that drives a real file listing needs: where the card is
///        (`SdRoot`) and why the audio engine must be held still while the catalogue works
///        (`QuietAudio`).
///
/// Both were born inside `file_catalogue_spec.cpp` (task 1 of the 2026-09-02 plan) and are hoisted
/// here by task 2, because `QuietAudio` is not a convenience -- **it is the disarm for a landmine
/// that segfaults a spec process rather than failing it**, and the next three specs in this plan
/// all have to step over it. A guard that has to be re-derived from a task report is a guard that
/// will eventually be re-derived wrong; one `#include` is not.
///
/// @warning **Include this BEFORE `<cppspec.hpp>`.** CppSpec `#define`s `it` (its own DSL keyword),
///          and this header pulls in `processing/engines/audio_engine.h`, whose include closure
///          reaches `util/container/ordered_pos_vector.h` -- which has locals named `it`. Included
///          after CppSpec, those locals expand to `self.it` and the translation unit fails to
///          compile with a wall of errors that name neither file. Put it with the project includes
///          at the top, not with `spec/support/spec_main.h` at the bottom.

#include "processing/engines/audio_engine.h"

#include <cstdlib>
#include <string>

namespace deluge::scenes::spec {

/// @brief Point `$DELUGE_SD_ROOT` at a directory for the scope, and restore whatever was there.
///
/// The host BSP resolves every filesystem call against `$DELUGE_SD_ROOT` on each call
/// (`src/bsp/host/host_efatfs_passthrough.cpp:164 resolve_root_relative`,
/// `:547 deluge_efatfs_dir_open`), so switching it mid-process is enough to aim a listing.
///
/// Set it explicitly rather than relying on the aggregate `scenes_spec` ctest row's env: the
/// per-spec rows `create_specs_driver` registers do *not* set it.
struct SdRoot {
	std::string saved;
	bool had;
	explicit SdRoot(const std::string& root) {
		const char* prev = std::getenv("DELUGE_SD_ROOT");
		had = prev != nullptr;
		if (had) {
			saved = prev;
		}
		(void)::setenv("DELUGE_SD_ROOT", root.c_str(), 1);
	}
	~SdRoot() {
		if (had) {
			(void)::setenv("DELUGE_SD_ROOT", saved.c_str(), 1);
		}
		else {
			(void)::unsetenv("DELUGE_SD_ROOT");
		}
	}
	SdRoot(const SdRoot&) = delete;
	SdRoot& operator=(const SdRoot&) = delete;
};

/// @brief Hold `AudioEngine`'s own re-entrancy latch for the scope, so the catalogue's yield points
///        are no-ops.
///
/// @warning **Without this, a spec that drives a listing does not fail -- it segfaults.**
///
/// `emptyFileItems()` and `deleteSomeFileItems()` pump `AudioEngine::routineWithClusterLoading()`
/// between chunks (`storage/file_catalogue.cpp:58 emptyFileItems`, `:74 deleteSomeFileItems`) --
/// they can be erasing thousands of entries, and the audio must keep running. A spec process never
/// boots the audio engine: `AudioEngine::init()` (`processing/engines/audio_engine.cpp:226`) builds
/// a preview `SoundDrum` off `currentSong` and only then sets `inputRingPos` (`:246`), so an
/// un-booted process walks into `fillInputRing` (`:179`) with a cursor of 0 and dies on the first
/// non-empty erase.
///
/// `audioRoutineLocked` is the engine's *own* answer to "do not run the routine right now"
/// (`:1122` -- `routine()` returns immediately while it is set, and `runRoutine()` reaches
/// `routine()` directly here because no scheduler task is registered). Holding it makes every pump
/// call a documented no-op. It changes nothing about the catalogue: the pump is a yield point, not
/// part of the listing, sort, search or cull contract.
///
/// @note This is a **spec-side** guard, and it stays spec-side on purpose. The shipped seam
///       (`src/scenes/browser/listing.cpp`) must never hold this latch: on the device the pump is
///       exactly what keeps audio alive across a multi-second directory read, and a listing that
///       silenced it would be a regression, not a safety measure.
struct QuietAudio {
	bool saved = AudioEngine::audioRoutineLocked;
	QuietAudio() { AudioEngine::audioRoutineLocked = true; }
	~QuietAudio() { AudioEngine::audioRoutineLocked = saved; }
	QuietAudio(const QuietAudio&) = delete;
	QuietAudio& operator=(const QuietAudio&) = delete;
};

} // namespace deluge::scenes::spec
