#include "serialization/schema/value_reader.h"

#include <charconv>
#include <string_view>
#include <utility>

namespace deluge::serialization::schema {
namespace {

[[nodiscard]] bool is_whole(std::string_view number) {
	return number.find_first_of(".eE") == std::string_view::npos;
}

template <typename Number>
[[nodiscard]] std::optional<Number> parse(std::string_view text) {
	Number value{};
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc{} || end != text.data() + text.size()) {
		return std::nullopt;
	}
	return value;
}

} // namespace

Token ValueReader::peek() const {
	if (current_ == nullptr) {
		return Token::END;
	}
	if (current_->isMap()) {
		return Token::RECORD;
	}
	if (current_->isArray()) {
		return Token::LIST;
	}
	const std::string& text = current_->str();
	switch (current_->scalarKind()) {
	case ScalarKind::TEXT:
		return Token::TEXT;
	case ScalarKind::NUMBER:
		return is_whole(text) ? Token::INTEGER : Token::REAL;
	case ScalarKind::LITERAL:
		return text == "null" ? Token::NONE : Token::BOOLEAN;
	}
	return Token::END;
}

void ValueReader::enter_record() {
	frames_.push_back({.node = take()});
}

std::optional<std::string> ValueReader::next_key() {
	Frame& frame = frames_.back();
	const auto& entries = frame.node->entries();
	if (frame.next == entries.size()) {
		frames_.pop_back();
		return std::nullopt;
	}
	const auto& [key, value] = entries[frame.next++];
	current_ = &value;
	return key;
}

void ValueReader::enter_list() {
	frames_.push_back({.node = take()});
}

bool ValueReader::next_item() {
	Frame& frame = frames_.back();
	const auto& items = frame.node->items();
	if (frame.next == items.size()) {
		frames_.pop_back();
		return false;
	}
	current_ = &items[frame.next++];
	return true;
}

std::optional<int64_t> ValueReader::integer() {
	const Value* node = take();
	if (node == nullptr || !node->isScalar() || node->scalarKind() != ScalarKind::NUMBER || !is_whole(node->str())) {
		return std::nullopt;
	}
	return parse<int64_t>(node->str());
}

std::optional<double> ValueReader::real() {
	const Value* node = take();
	if (node == nullptr || !node->isScalar() || node->scalarKind() != ScalarKind::NUMBER) {
		return std::nullopt;
	}
	return parse<double>(node->str());
}

std::optional<bool> ValueReader::boolean() {
	const Value* node = take();
	if (node == nullptr || !node->isScalar() || node->scalarKind() != ScalarKind::LITERAL || node->str() == "null") {
		return std::nullopt;
	}
	return node->str() == "true";
}

std::optional<std::string> ValueReader::text() {
	const Value* node = take();
	if (node == nullptr || !node->isScalar() || node->scalarKind() != ScalarKind::TEXT) {
		return std::nullopt;
	}
	return node->str();
}

Value ValueReader::value() {
	const Value* node = take();
	return node != nullptr ? *node : Value::makeScalar("null", ScalarKind::LITERAL);
}

const Value* ValueReader::take() {
	return std::exchange(current_, nullptr);
}

} // namespace deluge::serialization::schema
