#include "serialization/schema/value_writer.h"

#include <array>
#include <charconv>
#include <utility>

namespace deluge::serialization::schema {

void ValueWriter::begin_record() {
	open(Value::makeMap());
}

void ValueWriter::key(std::string_view name) {
	pending_key_ = std::string{name};
}

void ValueWriter::end_record() {
	close();
}

void ValueWriter::begin_list() {
	open(Value::makeArray());
}

void ValueWriter::end_list() {
	close();
}

void ValueWriter::integer(int64_t value) {
	put(Value::makeNumber(value));
}

void ValueWriter::boolean(bool value) {
	put(Value::makeScalar(value ? "true" : "false", ScalarKind::LITERAL));
}

void ValueWriter::text(std::string_view value) {
	put(Value::makeScalar(std::string{value}));
}

void ValueWriter::real(double value) {
	// The shortest text that parses back to the same double.
	std::array<char, 32> digits{};
	const auto [end, error] = std::to_chars(digits.data(), digits.data() + digits.size(), value);
	put(Value::makeScalar(std::string{digits.data(), end}, ScalarKind::NUMBER));
}

void ValueWriter::null() {
	put(Value::makeScalar("null", ScalarKind::LITERAL));
}

void ValueWriter::value(const Value& subtree) {
	put(subtree);
}

Value ValueWriter::take() && {
	return root_ ? std::move(*root_) : Value::makeMap();
}

void ValueWriter::open(Value node) {
	frames_.push_back({.node = std::move(node), .key = std::exchange(pending_key_, {})});
}

void ValueWriter::close() {
	Frame frame = std::move(frames_.back());
	frames_.pop_back();
	pending_key_ = std::move(frame.key);
	put(std::move(frame.node));
}

void ValueWriter::put(Value node) {
	if (frames_.empty()) {
		root_ = std::move(node);
		return;
	}
	Value& parent = frames_.back().node;
	if (parent.isMap()) {
		parent.set(std::exchange(pending_key_, {}), std::move(node));
	}
	else {
		parent.append(std::move(node));
	}
}

} // namespace deluge::serialization::schema
