#pragma once

/// @file
/// @brief The schema's descriptors: how a record's fields are found, written and read.
///
/// A descriptor names its members through small accessor functions over `void*`, so that one copy of the walk serves
/// every record type; the typed builders in fields.h make them, and Schema<R> keeps a record's type beside its
/// descriptor.

#include "serialization/schema/extras.h"
#include "serialization/schema/function_ref.h"
#include "serialization/schema/reader.h"
#include "serialization/schema/report.h"
#include "serialization/schema/walk_context.h"
#include "serialization/schema/writer.h"
#include "serialization/value.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>
#include <variant>

namespace deluge::serialization::schema {

struct FieldDesc;
struct RecordDesc;

/// The key that names a record's alternative, written first.
inline constexpr std::string_view kKindKey = "kind";

/// @brief What a scalar field may hold: a range, or, for a choice, the words its values are written as (value i as
/// word i).
struct ScalarRule {
	int64_t min{std::numeric_limits<int64_t>::min()};
	int64_t max{std::numeric_limits<int64_t>::max()};
	std::span<const std::string_view> words;
};

/// @brief How one member type is written and read as a scalar.
struct ScalarCodec {
	std::string_view form; ///< what the schema lock records: `integer`, `bool`, `text` or `real`
	void (*write)(const void* member, const ScalarRule& rule, Writer& out) = nullptr;
	/// Reads the next value into the member; clamps and notes a value outside the rule.
	std::expected<void, LoadError::Kind> (*read)(void* member, const ScalarRule& rule, Reader& in,
	                                             WalkContext& context) = nullptr;
};

/// @brief How a field's member is found from the object its record describes.
struct Access {
	const void* (*get)(const void* owner) = nullptr;
	void* (*get_mut)(void* owner) = nullptr;
};

/// @brief How an optional member is read and changed.
struct OptionalOps {
	bool (*has_value)(const void* member) = nullptr;
	const void* (*value)(const void* member) = nullptr;
	/// Makes the member hold a default value, and returns it.
	void* (*emplace)(void* member) = nullptr;
	void (*reset)(void* member) = nullptr;
};

/// @brief Whether an append made its item.
enum class Appended : uint8_t {
	YES,          ///< the item was made and filled
	FULL,         ///< the list holds no more; fill was not called
	DUPLICATE_ID, ///< the item was filled, and its id is already in the list
};

/// @brief How a list member is walked and built.
struct ListOps {
	void (*for_each)(const void* list, FunctionRef<void(const void* element)> visit) = nullptr;
	void (*clear)(void* list) = nullptr;
	/// Makes item @p index, of alternative @p alternative (0 for a list of one kind), and calls fill with it once,
	/// unless the list is full.
	Appended (*append)(void* list, std::size_t index, std::size_t alternative,
	                   FunctionRef<void(void* element)> fill) = nullptr;
	std::size_t capacity{std::numeric_limits<std::size_t>::max()};
};

/// @brief One kind a record of several kinds can be: its word and its fields.
struct Alternative {
	std::string_view word;
	const RecordDesc* record;
};

/// @brief How a member of several kinds is walked: which kind it is, and the object that kind's fields describe.
struct VariantDesc {
	std::span<const Alternative> alternatives;
	std::size_t (*index)(const void* member) = nullptr;
	const void* (*get)(const void* member) = nullptr;
	/// Makes the member alternative @p index with default values, and returns the object its fields describe.
	void* (*emplace)(void* member, std::size_t index) = nullptr;
};

/// @brief What a plugin made of its node.
enum class PluginRead : uint8_t {
	DECODED, ///< the member holds the node's state
	KEEP,    ///< no registered plugin reads it: the walk keeps the node in the owning record's extras, and writes it
	         ///< back while the member holds no state of its own
	BAD,     ///< the node is not a plugin node
};

/// @brief How a plugin's state member is written and read, as a node (plugin_node.h).
struct PluginOps {
	std::string_view name; ///< what the schema lock records
	uint32_t version;      ///< the node version it writes
	/// Whether the member holds a state of its own; while it does not, a node kept from the file is written instead.
	bool (*has_state)(const void* member) = nullptr;
	void (*write)(const void* member, Writer& out, const WalkContext& context) = nullptr;
	PluginRead (*read)(void* member, const Value& node, WalkContext& context) = nullptr;
};

/// @brief A tag per member type, so that a lookup can tell a scalar field's member type whatever its codec.
template <typename T>
inline constexpr char kTypeTag = 0;

struct ScalarShape {
	const ScalarCodec* codec{nullptr};
	ScalarRule rule{};
	const void* type{nullptr}; ///< &kTypeTag<T> for a member of type T
};
struct RecordShape {
	const RecordDesc* record{nullptr};
};
struct OptionalShape {
	const OptionalOps* ops{nullptr};
	const FieldDesc* value{nullptr}; ///< the held value's field, keyless, whose access is the value itself
};
struct ListShape {
	const ListOps* ops{nullptr};
	const FieldDesc* element{nullptr}; ///< an item's field, keyless, whose access is the item itself
	std::string_view id_key;           ///< the key of the item field each item's is unique in; empty for none
};
struct VariantShape {
	const VariantDesc* variant{nullptr};
};
struct PluginShape {
	const PluginOps* ops{nullptr};
};
using Shape = std::variant<ScalarShape, RecordShape, OptionalShape, ListShape, VariantShape, PluginShape>;

/// @brief One key of a record: where its member is, and its shape.
struct FieldDesc {
	std::string_view key;
	Access access;
	Shape shape;
};

/// @brief A record's fields, in the order written, and where it keeps unknown keys.
struct RecordDesc {
	std::string_view name;
	std::span<const FieldDesc> fields;
	/// Its kept unknown keys; null for a closed record, which drops them.
	const Extras* (*extras)(const void* record) = nullptr;
	Extras* (*extras_mut)(void* record) = nullptr;
};

/// @brief A record type's descriptor, so that the typed entry points take only records it describes.
/// @tparam R The record type.
template <typename R>
struct Schema {
	const RecordDesc* desc;
};

} // namespace deluge::serialization::schema
