#include "serialization/parse_json.h"
#include "serialization/schema/value_reader.h"
#include "serialization/schema/value_writer.h"
#include "serialization/write_json.h"
#include "support/printers/optional_printer.h"
#include "support/printers/value_printer.h"

#include <cppspec.hpp>

#include <cstdlib>

#include <cstdint>
#include <limits>
#include <string>

using deluge::serialization::Value;
using deluge::serialization::schema::Token;
using deluge::serialization::schema::ValueReader;
using deluge::serialization::schema::ValueWriter;

namespace {
/// The tree @p writes builds, written as JSON and parsed back: what a file holding it reads as.
template <typename Writes>
Value through_json(Writes writes) {
	ValueWriter out;
	writes(out);
	return *deluge::serialization::parseJson(deluge::serialization::write_json(std::move(out).take()));
}
} // namespace

// clang-format off
describe value_backend("the Value writer and reader", ${
	it("reads back every scalar the writer wrote, through JSON", _ {
		const Value tree = through_json([](ValueWriter& out) {
			out.begin_record();
			out.key("n"); out.integer(-12);
			out.key("big"); out.integer(std::numeric_limits<int64_t>::max());
			out.key("on"); out.boolean(true);
			out.key("name"); out.text("Kick \"1\"");
			out.key("gain"); out.real(0.1);
			out.key("narrow"); out.real(static_cast<double>(0.3F));
			out.key("none"); out.null();
			out.end_record();
		});
		ValueReader in{tree};
		expect(in.peek()).to_equal(Token::RECORD);
		in.enter_record();
		expect(in.next_key()).to_equal(std::optional<std::string>{"n"});
		expect(in.peek()).to_equal(Token::INTEGER);
		expect(in.integer()).to_equal(std::optional<int64_t>{-12});
		expect(in.next_key()).to_equal(std::optional<std::string>{"big"});
		expect(in.integer()).to_equal(std::optional<int64_t>{std::numeric_limits<int64_t>::max()});
		expect(in.next_key()).to_equal(std::optional<std::string>{"on"});
		expect(in.peek()).to_equal(Token::BOOLEAN);
		expect(in.boolean()).to_equal(std::optional<bool>{true});
		expect(in.next_key()).to_equal(std::optional<std::string>{"name"});
		expect(in.text()).to_equal(std::optional<std::string>{"Kick \"1\""});
		expect(in.next_key()).to_equal(std::optional<std::string>{"gain"});
		expect(in.peek()).to_equal(Token::REAL);
		expect(in.real()).to_equal(std::optional<double>{0.1});
		expect(in.next_key()).to_equal(std::optional<std::string>{"narrow"});
		expect(static_cast<float>(*in.real())).to_equal(0.3F);
		expect(in.next_key()).to_equal(std::optional<std::string>{"none"});
		expect(in.peek()).to_equal(Token::NONE);
		expect(in.value()).to_equal(Value::makeScalar("null", deluge::serialization::ScalarKind::LITERAL));
		expect(in.next_key()).to_equal(std::optional<std::string>{});
		expect(in.peek()).to_equal(Token::END);
	});

	it("writes a float in its fewest exact digits, and a real that is not finite as null", _ {
		ValueWriter out;
		out.begin_list();
		out.real(0.1F);
		out.real(std::numeric_limits<double>::infinity());
		out.end_list();
		const Value tree = std::move(out).take();
		expect(tree.items()[0].str()).to_equal(std::string{"0.1"});
		expect(tree.items()[1].str()).to_equal(std::string{"null"});
	});

	it("walks nested records and lists in order", _ {
		const Value tree = through_json([](ValueWriter& out) {
			out.begin_record();
			out.key("list");
			out.begin_list(); out.integer(1); out.begin_record(); out.key("a"); out.integer(2); out.end_record(); out.end_list();
			out.key("after"); out.integer(3);
			out.end_record();
		});
		ValueReader in{tree};
		in.enter_record();
		expect(in.next_key()).to_equal(std::optional<std::string>{"list"});
		expect(in.peek()).to_equal(Token::LIST);
		in.enter_list();
		expect(in.next_item()).to_equal(true);
		expect(in.integer()).to_equal(std::optional<int64_t>{1});
		expect(in.next_item()).to_equal(true);
		in.enter_record();
		expect(in.next_key()).to_equal(std::optional<std::string>{"a"});
		expect(in.integer()).to_equal(std::optional<int64_t>{2});
		expect(in.next_key()).to_equal(std::optional<std::string>{});
		expect(in.next_item()).to_equal(false);
		expect(in.next_key()).to_equal(std::optional<std::string>{"after"});
		expect(in.integer()).to_equal(std::optional<int64_t>{3});
	});

	it("consumes a value of the wrong type and reads it as nothing", _ {
		const Value tree = *deluge::serialization::parseJson(R"({"a": 1.5, "b": "x", "c": 2})");
		ValueReader in{tree};
		in.enter_record();
		(void)in.next_key();
		expect(in.integer()).to_equal(std::optional<int64_t>{});
		(void)in.next_key();
		expect(in.boolean()).to_equal(std::optional<bool>{});
		expect(in.next_key()).to_equal(std::optional<std::string>{"c"});
		expect(in.real()).to_equal(std::optional<double>{2.0});
	});

	it("hands a subtree over whole", _ {
		const Value tree = *deluge::serialization::parseJson(R"({"keep": {"x": [1, {"y": null}]}})");
		ValueReader in{tree};
		in.enter_record();
		(void)in.next_key();
		expect(in.value()).to_equal(*tree.find("keep"));
		expect(in.next_key()).to_equal(std::optional<std::string>{});
	});
});
// clang-format on

int value_backend_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(value_backend).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
