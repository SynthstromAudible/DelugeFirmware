#include "project/clean_codec.h"

#include "project/schema/song_schema.h"
#include "serialization/schema/codec.h"
#include "serialization/tree.h"

#include <set>
#include <string>
#include <utility>
#include <variant>

namespace deluge::project {
namespace {

namespace sch = serialization::schema;
using serialization::Value;

constexpr std::string_view kHeaderKey = "deluge";
constexpr std::string_view kSongKind = "song";
constexpr std::string_view kPresetKind = "preset";

template <typename... Fs>
struct Overloaded : Fs... {
	using Fs::operator()...;
};

Value header(std::string_view kind, std::string_view firmware) {
	Value made = Value::makeMap();
	made.set("kind", Value::makeScalar(std::string{kind}));
	made.set("schema", Value::makeNumber(kSchemaRevision));
	made.set("firmware", Value::makeScalar(std::string{firmware}));
	return made;
}

template <typename R>
Value document(std::string_view kind, std::string_view body_key, sch::Schema<R> schema, const R& record,
               std::string_view firmware, const schema::CleanContext& context) {
	Value root = Value::makeMap();
	root.set(std::string{kHeaderKey}, header(kind, firmware));
	root.set(std::string{body_key}, sch::encode(schema, record, &context));
	return root;
}

/// The body of a clean document of @p kind, its header checked; a document of another revision is noted.
std::expected<const Value*, sch::LoadError> body_of(const Value& root, std::string_view kind, std::string_view body_key,
                                                    sch::WalkContext& walk) {
	const Value* head = root.isMap() ? root.find(kHeaderKey) : nullptr;
	if (head == nullptr || !head->isMap()) {
		return std::unexpected(sch::LoadError{sch::LoadError::Kind::NOT_A_DOCUMENT, {}});
	}
	const std::string* named = serialization::text_of(head->find("kind"));
	if (named == nullptr || *named != kind) {
		return std::unexpected(sch::LoadError{sch::LoadError::Kind::WRONG_KIND, "deluge/kind"});
	}
	if (serialization::int_of(head->find("schema")) != std::optional<int32_t>{static_cast<int32_t>(kSchemaRevision)}) {
		walk.enter(kHeaderKey);
		walk.enter("schema");
		walk.note(sch::LoadNote::Kind::SCHEMA_REVISION_DIFFERS);
		walk.leave();
		walk.leave();
	}
	const Value* body = root.find(body_key);
	if (body == nullptr) {
		return std::unexpected(sch::LoadError{sch::LoadError::Kind::NOT_A_DOCUMENT, std::string{body_key}});
	}
	return body;
}

template <typename R>
std::expected<sch::Loaded<R>, sch::LoadError> read_document(const Value& root, std::string_view kind,
                                                            std::string_view body_key, sch::Schema<R> schema,
                                                            const CodecPlugins& plugins) {
	// A decode takes each plugin node's own tick magnitude; the context's is for writing.
	const schema::CleanContext context{&plugins, 0};
	sch::WalkContext walk{&context};
	auto body = body_of(root, kind, body_key, walk);
	if (!body) {
		return std::unexpected(std::move(body.error()));
	}
	R record;
	sch::ValueReader in{**body};
	walk.enter(body_key);
	if (auto done = sch::decode_record(*schema.desc, &record, in, walk); !done) {
		return std::unexpected(std::move(done.error()));
	}
	walk.leave();
	return sch::Loaded<R>{std::move(record), std::move(walk).take_report()};
}

} // namespace

bool is_clean_document(const Value& root) {
	return root.isMap() && root.find(kHeaderKey) != nullptr;
}

Value encode_clean(const SongDocument& song, std::string_view firmware, const CodecPlugins& plugins) {
	const schema::CleanContext context{&plugins, song.transport.input_tick_magnitude()};
	return document(kSongKind, kSongKind, schema::kSongSchema, song, firmware, context);
}

std::expected<sch::Loaded<SongDocument>, sch::LoadError> decode_clean(const Value& root, const CodecPlugins& plugins) {
	return read_document(root, kSongKind, kSongKind, schema::kSongSchema, plugins);
}

PresetDocument extract_preset(const SongDocument& song, uint32_t output_id) {
	PresetDocument preset;
	const auto entry = std::ranges::find(song.output_order, output_id, &SongDocument::OutputEntry::id);
	if (entry == song.output_order.end()) {
		return preset;
	}
	preset.output_order.push_back(*entry);
	preset.outputs.emplace(output_id, song.outputs.at(output_id));
	std::set<uint32_t> drums;
	for (const SongDocument::DrumEntry& drum : entry->drums) {
		preset.drums.emplace(drum.id, song.drums.at(drum.id));
		drums.insert(drum.id);
	}
	const auto own = [&](const Mapping& mapping) {
		return std::visit(
		    Overloaded{
		        [&](const OwnerParamTarget& target) {
			        return (target.owner.kind == ControlsAddress::Kind::OUTPUT && target.owner.id == output_id)
			               || (target.owner.kind == ControlsAddress::Kind::DRUM && drums.contains(target.owner.id));
		        },
		        [](const ClipMuteTarget& /*target*/) { return false; },
		        [&](const DrumMuteTarget& target) { return drums.contains(target.drum_id); },
		        [](const SectionLaunchTarget& /*target*/) { return false; },
		        [&](const TrackInputTarget& target) { return target.output_id == output_id; },
		        [&](const LaneInputTarget& target) { return drums.contains(target.drum_id); },
		    },
		    mapping.target);
	};
	for (const Mapping& mapping : song.mappings.entries()) {
		if (own(mapping)) {
			preset.mappings.restore(mapping);
		}
	}
	return preset;
}

Value encode_preset_clean(const PresetDocument& preset, int32_t tick_magnitude, std::string_view firmware,
                          const CodecPlugins& plugins) {
	const schema::CleanContext context{&plugins, tick_magnitude};
	return document(kPresetKind, kPresetKind, schema::kPresetSchema, preset, firmware, context);
}

std::expected<sch::Loaded<PresetDocument>, sch::LoadError> decode_preset_clean(const Value& root,
                                                                               const CodecPlugins& plugins) {
	return read_document(root, kPresetKind, kPresetKind, schema::kPresetSchema, plugins);
}

} // namespace deluge::project
