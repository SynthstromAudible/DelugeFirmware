#include "model/edits/learn_mapping.h"

#include "io/debug/log.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/song/song.h"

namespace deluge::edits {

namespace {

/// Project @p target from the table @p ctx's model now holds; on refusal put back @p before, the table as it was
/// before the write.
Error project(Context& ctx, const deluge::project::MappingTarget& target, deluge::project::MappingTable before) {
	const Error projected = deluge::bridge::projectMapping(ctx.song, target);
	if (projected != Error::NONE) {
		ctx.model.mappings = std::move(before);
		return projected;
	}
	deluge::bridge::rebuildMappingIndex(ctx.song);
	return Error::NONE;
}

/// Apply @p edit to the current song, if there is one.
void applyToCurrentSong(Edit& edit) {
	if (currentSong == nullptr) {
		return;
	}
	Context ctx{*currentSong, currentSong->project};
	if (deluge::edits::apply(ctx, edit) != Error::NONE) {
		// A gesture names only what the song holds; a refusal here is a gesture and the song out of step.
		D_PRINTLN("mapping refused: its owner is not in the song");
	}
}

} // namespace

Error LearnMapping::apply(Context& ctx) {
	deluge::project::MappingTable before = ctx.model.mappings;
	ctx.model.mappings.learn(mapping_);
	return project(ctx, mapping_.target, std::move(before));
}

Error UnlearnMapping::apply(Context& ctx) {
	deluge::project::MappingTable before = ctx.model.mappings;
	ctx.model.mappings.unlearn(target_);
	return project(ctx, target_, std::move(before));
}

void learnMappingFor(deluge::project::MidiCommand source, deluge::project::MappingTarget target) {
	LearnMapping edit{deluge::project::Mapping{.source = std::move(source), .target = target}};
	applyToCurrentSong(edit);
}

void unlearnMappingFor(deluge::project::MappingTarget target) {
	UnlearnMapping edit{target};
	applyToCurrentSong(edit);
}

} // namespace deluge::edits
