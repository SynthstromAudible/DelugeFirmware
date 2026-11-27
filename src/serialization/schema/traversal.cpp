#include "serialization/schema/traversal.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace deluge::serialization::schema {
namespace {

template <typename... Fs>
struct Overloaded : Fs... {
	using Fs::operator()...;
};

/// What decoding a value did: read it, or kept a plugin node in the owning record's extras.
enum class Decoded : uint8_t { VALUE, KEPT };
using Result = std::expected<Decoded, LoadError>;

/// Where a plugin node no plugin reads is kept: the record holding the field, and the field's key.
struct Owner {
	Extras* extras{nullptr};
	std::string_view key;
};

void encode_value(const FieldDesc& field, const void* member, Writer& out, const WalkContext& context);

void encode_fields(const RecordDesc& desc, const void* record, Writer& out, const WalkContext& context) {
	const Extras* extras = desc.extras != nullptr ? desc.extras(record) : nullptr;
	for (const FieldDesc& field : desc.fields) {
		if (extras != nullptr && extras->holds(field.key)) {
			continue; // a plugin node no plugin read: written below, as read
		}
		out.key(field.key);
		encode_value(field, field.access.get(record), out, context);
	}
	if (extras != nullptr) {
		for (const auto& [key, value] : extras->entries()) {
			out.key(key);
			out.value(value);
		}
	}
}

void encode_variant(const VariantDesc& variant, const void* member, Writer& out, const WalkContext& context) {
	const Alternative& alternative = variant.alternatives[variant.index(member)];
	out.begin_record();
	out.key(kKindKey);
	out.text(alternative.word);
	encode_fields(*alternative.record, variant.get(member), out, context);
	out.end_record();
}

void encode_value(const FieldDesc& field, const void* member, Writer& out, const WalkContext& context) {
	std::visit(Overloaded{
	               [&](const ScalarShape& shape) { shape.codec->write(member, shape.rule, out); },
	               [&](const RecordShape& shape) {
		               out.begin_record();
		               encode_fields(*shape.record, member, out, context);
		               out.end_record();
	               },
	               [&](const OptionalShape& shape) {
		               if (shape.ops->has_value(member)) {
			               encode_value(*shape.value, shape.ops->value(member), out, context);
		               }
		               else {
			               out.null();
		               }
	               },
	               [&](const ListShape& shape) {
		               out.begin_list();
		               shape.ops->for_each(member,
		                                   [&](const void* item) { encode_value(*shape.element, item, out, context); });
		               out.end_list();
	               },
	               [&](const VariantShape& shape) { encode_variant(*shape.variant, member, out, context); },
	               [&](const PluginShape& shape) { shape.ops->write(member, out, context); },
	           },
	           field.shape);
}

Result decode_value(const FieldDesc& field, void* member, Reader& in, WalkContext& context, Owner owner);

std::expected<void, LoadError> decode_fields(const RecordDesc& desc, void* record, Reader& in, WalkContext& context) {
	Extras* extras = desc.extras_mut != nullptr ? desc.extras_mut(record) : nullptr;
	while (std::optional<std::string> key = in.next_key()) {
		context.enter(*key);
		const auto field = std::ranges::find(desc.fields, std::string_view{*key}, &FieldDesc::key);
		if (field == desc.fields.end()) {
			if (extras != nullptr) {
				extras->keep(*key, in.value());
				context.note(LoadNote::Kind::UNKNOWN_KEY_KEPT);
			}
			else {
				(void)in.value();
				context.note(LoadNote::Kind::UNKNOWN_KEY_DROPPED);
			}
		}
		else if (Result done = decode_value(*field, field->access.get_mut(record), in, context,
		                                    {.extras = extras, .key = field->key});
		         !done) {
			return std::unexpected(std::move(done.error()));
		}
		context.leave();
	}
	return {};
}

std::expected<void, LoadError> enter_record(Reader& in, WalkContext& context) {
	if (in.peek() != Token::RECORD) {
		(void)in.value();
		return std::unexpected(context.error(LoadError::Kind::WRONG_TYPE));
	}
	in.enter_record();
	return {};
}

/// Enter the record of several kinds @p in is on and read its kind.
std::expected<std::size_t, LoadError> enter_variant(const VariantDesc& variant, Reader& in, WalkContext& context) {
	if (auto entered = enter_record(in, context); !entered) {
		return std::unexpected(std::move(entered.error()));
	}
	const std::optional<std::string> key = in.next_key();
	if (!key || *key != kKindKey) {
		return std::unexpected(context.error(LoadError::Kind::NO_KIND));
	}
	context.enter(kKindKey);
	const std::optional<std::string> word = in.text();
	const auto chosen = word ? std::ranges::find(variant.alternatives, std::string_view{*word}, &Alternative::word)
	                         : variant.alternatives.end();
	if (chosen == variant.alternatives.end()) {
		return std::unexpected(context.error(word ? LoadError::Kind::UNKNOWN_KIND : LoadError::Kind::WRONG_TYPE));
	}
	context.leave();
	return static_cast<std::size_t>(chosen - variant.alternatives.begin());
}

/// Skip the rest of a record already entered.
void drain(Reader& in) {
	while (in.next_key()) {
		(void)in.value();
	}
}

std::expected<void, LoadError> decode_item(const ListShape& list, void* member, std::size_t index, Reader& in,
                                           WalkContext& context) {
	const auto* variant = std::get_if<VariantShape>(&list.element->shape);
	std::size_t alternative = 0;
	if (variant != nullptr) {
		auto chosen = enter_variant(*variant->variant, in, context);
		if (!chosen) {
			return std::unexpected(std::move(chosen.error()));
		}
		alternative = *chosen;
	}
	std::expected<void, LoadError> filled{};
	const Appended appended = list.ops->append(member, index, alternative, [&](void* item) {
		if (variant != nullptr) {
			const Alternative& kind = variant->variant->alternatives[alternative];
			filled = decode_fields(*kind.record, variant->variant->emplace(item, alternative), in, context);
		}
		else if (Result done = decode_value(*list.element, item, in, context, {}); !done) {
			filled = std::unexpected(std::move(done.error()));
		}
	});
	if (!filled) {
		return filled;
	}
	switch (appended) {
	case Appended::YES:
		return {};
	case Appended::FULL:
		if (variant != nullptr) {
			drain(in);
		}
		else {
			(void)in.value();
		}
		context.note(LoadNote::Kind::TOO_MANY_ITEMS);
		return {};
	case Appended::DUPLICATE_ID:
		return std::unexpected(context.error(LoadError::Kind::DUPLICATE_ID));
	}
	return {};
}

Result decode_list(const ListShape& shape, void* member, Reader& in, WalkContext& context) {
	if (in.peek() != Token::LIST) {
		(void)in.value();
		return std::unexpected(context.error(LoadError::Kind::WRONG_TYPE));
	}
	in.enter_list();
	shape.ops->clear(member);
	for (std::size_t index = 0; in.next_item(); ++index) {
		context.enter(index);
		if (auto done = decode_item(shape, member, index, in, context); !done) {
			return std::unexpected(std::move(done.error()));
		}
		context.leave();
	}
	return Decoded::VALUE;
}

Result decode_variant(const VariantShape& shape, void* member, Reader& in, WalkContext& context) {
	auto chosen = enter_variant(*shape.variant, in, context);
	if (!chosen) {
		return std::unexpected(std::move(chosen.error()));
	}
	const Alternative& kind = shape.variant->alternatives[*chosen];
	if (auto done = decode_fields(*kind.record, shape.variant->emplace(member, *chosen), in, context); !done) {
		return std::unexpected(std::move(done.error()));
	}
	return Decoded::VALUE;
}

Result decode_plugin(const PluginShape& shape, void* member, Reader& in, WalkContext& context, Owner owner) {
	Value node = in.value();
	switch (shape.ops->read(member, node, context)) {
	case PluginRead::DECODED:
		return Decoded::VALUE;
	case PluginRead::KEEP:
		if (owner.extras != nullptr) {
			owner.extras->keep(std::string{owner.key}, std::move(node));
			context.note(LoadNote::Kind::PLUGIN_STATE_KEPT);
		}
		else {
			context.note(LoadNote::Kind::UNKNOWN_KEY_DROPPED);
		}
		return Decoded::KEPT;
	case PluginRead::BAD:
		break;
	}
	return std::unexpected(context.error(LoadError::Kind::BAD_PLUGIN_STATE));
}

Result decode_value(const FieldDesc& field, void* member, Reader& in, WalkContext& context, Owner owner) {
	return std::visit(Overloaded{
	                      [&](const ScalarShape& shape) -> Result {
		                      if (auto done = shape.codec->read(member, shape.rule, in, context); !done) {
			                      return std::unexpected(context.error(done.error()));
		                      }
		                      return Decoded::VALUE;
	                      },
	                      [&](const RecordShape& shape) -> Result {
		                      if (auto entered = enter_record(in, context); !entered) {
			                      return std::unexpected(std::move(entered.error()));
		                      }
		                      if (auto done = decode_fields(*shape.record, member, in, context); !done) {
			                      return std::unexpected(std::move(done.error()));
		                      }
		                      return Decoded::VALUE;
	                      },
	                      [&](const OptionalShape& shape) -> Result {
		                      if (in.peek() == Token::NONE) {
			                      (void)in.value();
			                      shape.ops->reset(member);
			                      return Decoded::VALUE;
		                      }
		                      Result done = decode_value(*shape.value, shape.ops->emplace(member), in, context, owner);
		                      if (done && *done == Decoded::KEPT) {
			                      shape.ops->reset(member); // the node is in the owner's extras
		                      }
		                      return done;
	                      },
	                      [&](const ListShape& shape) { return decode_list(shape, member, in, context); },
	                      [&](const VariantShape& shape) { return decode_variant(shape, member, in, context); },
	                      [&](const PluginShape& shape) { return decode_plugin(shape, member, in, context, owner); },
	                  },
	                  field.shape);
}

} // namespace

void encode_record(const RecordDesc& desc, const void* record, Writer& out, const WalkContext& context) {
	out.begin_record();
	encode_fields(desc, record, out, context);
	out.end_record();
}

std::expected<void, LoadError> decode_record(const RecordDesc& desc, void* record, Reader& in, WalkContext& context) {
	if (auto entered = enter_record(in, context); !entered) {
		return entered;
	}
	return decode_fields(desc, record, in, context);
}

} // namespace deluge::serialization::schema
