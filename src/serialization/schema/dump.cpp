#include "serialization/schema/dump.h"

#include <algorithm>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace deluge::serialization::schema {
namespace {

template <typename... Fs>
struct Overloaded : Fs... {
	using Fs::operator()...;
};

Value text(std::string_view value) {
	return Value::makeScalar(std::string{value});
}

Value literal(bool value) {
	return Value::makeScalar(value ? "true" : "false", ScalarKind::LITERAL);
}

class Describer {
public:
	void record(const RecordDesc& desc) {
		if (std::ranges::contains(seen_, &desc)) {
			return;
		}
		seen_.push_back(&desc);
		Value fields = Value::makeArray();
		for (const FieldDesc& field : desc.fields) {
			fields.append(describe_field(field));
		}
		Value entry = Value::makeMap();
		entry.set("name", text(desc.name));
		entry.set("open", literal(desc.extras != nullptr));
		entry.set("fields", std::move(fields));
		records_.append(std::move(entry));
	}

	[[nodiscard]] Value take() && { return std::move(records_); }

private:
	Value describe_field(const FieldDesc& field) {
		Value entry = Value::makeMap();
		if (!field.key.empty()) {
			entry.set("key", text(field.key));
		}
		std::visit(Overloaded{
		               [&](const ScalarShape& shape) {
			               entry.set("kind", text("scalar"));
			               if (!shape.rule.words.empty()) {
				               entry.set("form", text("word"));
				               Value words = Value::makeArray();
				               for (std::string_view word : shape.rule.words) {
					               words.append(text(word));
				               }
				               entry.set("words", std::move(words));
				               return;
			               }
			               entry.set("form", text(shape.codec->form));
			               if (shape.codec->form == "integer") {
				               entry.set("min", Value::makeNumber(shape.rule.min));
				               entry.set("max", Value::makeNumber(shape.rule.max));
			               }
		               },
		               [&](const RecordShape& shape) {
			               record(*shape.record);
			               entry.set("kind", text("record"));
			               entry.set("record", text(shape.record->name));
		               },
		               [&](const OptionalShape& shape) {
			               entry.set("kind", text("optional"));
			               entry.set("value", describe_field(*shape.value));
		               },
		               [&](const ListShape& shape) {
			               entry.set("kind", text("list"));
			               if (shape.ops->capacity != std::numeric_limits<std::size_t>::max()) {
				               entry.set("capacity", Value::makeNumber(static_cast<int64_t>(shape.ops->capacity)));
			               }
			               if (!shape.id_key.empty()) {
				               entry.set("idKey", text(shape.id_key));
			               }
			               entry.set("element", describe_field(*shape.element));
		               },
		               [&](const VariantShape& shape) {
			               entry.set("kind", text("variant"));
			               Value alternatives = Value::makeArray();
			               for (const Alternative& alternative : shape.variant->alternatives) {
				               record(*alternative.record);
				               Value described = Value::makeMap();
				               described.set("word", text(alternative.word));
				               described.set("record", text(alternative.record->name));
				               alternatives.append(std::move(described));
			               }
			               entry.set("alternatives", std::move(alternatives));
		               },
		               [&](const PluginShape& shape) {
			               entry.set("kind", text("plugin"));
			               entry.set("plugin", text(shape.ops->name));
			               entry.set("version", Value::makeNumber(shape.ops->version));
		               },
		           },
		           field.shape);
		return entry;
	}

	std::vector<const RecordDesc*> seen_;
	Value records_ = Value::makeArray();
};

} // namespace

Value dump_schema(std::span<const RecordDesc* const> roots, uint32_t revision) {
	Describer describer;
	for (const RecordDesc* root : roots) {
		describer.record(*root);
	}
	Value dump = Value::makeMap();
	dump.set("revision", Value::makeNumber(revision));
	dump.set("records", std::move(describer).take());
	return dump;
}

} // namespace deluge::serialization::schema
