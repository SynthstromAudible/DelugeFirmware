#pragma once

/// @file
/// @brief Finish the input take a torn-down recorder screen left running, as the card routine and the foreground
///        recording's slow routine would.
///
/// `RecorderScene::on_exit` ends a take whose screen goes under it and leaves it published (`TakeActions::orphan`,
/// `deluge::recording::orphanInputTake` on the host) until its recorder has settled; a spec process runs no card
/// routine, so nothing would ever settle it, and the next example's recorder would be refused as "already recording".

#include "model/sample/sample_recorder.h"
#include "processing/engines/audio_engine.h"
#include "processing/recording/foreground_recording.h"

namespace deluge::ui::scenes::spec {

/// @brief Settle and free the published input take, if there is one. A take some other path still owns stays
///        published: `slowRoutine` frees only one `orphanInputTake` handed over.
inline void settle_torn_down_take() {
	SampleRecorder* rec = AudioEngine::foregroundRecorder();
	if (rec == nullptr || !deluge::recording::inputTakeInProgress()) {
		return;
	}
	rec->status = RecorderStatus::COMPLETE; // what the card routine leaves once the file is closed
	deluge::recording::slowRoutine();
}

} // namespace deluge::ui::scenes::spec
