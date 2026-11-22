#pragma once

/// @file
/// @brief `SliceHeld`: a sample the browser's preview drum holds under a path, so the slicer's host opens on it as the
///        app's SLICE door does (`gui::ui::openShellSlice`), and the waveforms port's SLICE address resolves to it.

#include "gui/ui/shell_slice_ops.h"
#include "model/sample/sample.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "storage/multi_range/multisample_range.h"

#include <string>
#include <string_view>

namespace deluge::scenes::spec {

/// @brief Put @p sample in the preview drum's range under @p path, as a landed preview does, for an example.
///
/// `open()` opens the slicer's host on it. The host takes the drum's file without loading or stopping it, so a
/// sample with no lease is safe here. The destructor releases the host, as a guest close does, and empties the range.
class SliceHeld {
public:
	/// @param sample The sample the drum holds; its `filePath` becomes @p path, as a bound load's does.
	/// @param path   The path the drum's holder names it by.
	SliceHeld(Sample* sample, std::string_view path)
	    : range_(static_cast<MultisampleRange*>(AudioEngine::sampleForPreview->getOrCreateFirstRange(0))), path_(path) {
		sample->filePath.assign(path_);
		range_->sampleHolder.filePath.assign(path_);
		range_->sampleHolder.audioFile = sample;
	}
	~SliceHeld() {
		deluge::gui::ui::releaseShellSlicerBeforeClose();
		range_->sampleHolder.audioFile = nullptr;
		range_->sampleHolder.filePath.clear();
	}
	SliceHeld(const SliceHeld&) = delete;
	SliceHeld& operator=(const SliceHeld&) = delete;

	/// @brief Open the slicer's host on the held file, which opens the scene on it.
	void open() const { deluge::gui::ui::openShellSlice(path_); }

	/// @return The path the drum holds the file under.
	[[nodiscard]] const std::string& path() const { return path_; }

private:
	MultisampleRange* range_;
	std::string path_;
};

} // namespace deluge::scenes::spec
