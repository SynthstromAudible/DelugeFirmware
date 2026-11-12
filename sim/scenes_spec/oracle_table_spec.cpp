/// @file
/// @brief The reference tables' reader: what a line holds, what is a problem, and what a spec has not asked.

#include "support/oracle_table.h"

#include "cppspec.hpp"

#include <string>

#include "spec/support/spec_main.h"

namespace spec = deluge::scenes::spec;

// clang-format off
describe oracle_table("scenes spec reference tables: the reader", $ {
	it("reads a path, a column and every value of a line, and skips comments and blank lines", _ {
		spec::OracleTable t = spec::OracleTable::parse("# a comment\n\nsound/2/0\tread\t40\nsound\tkids\t27\n"
		                                               "sound/5/1\toptions\tOff\tLow\n");
		expect(t.size()).to_equal(size_t{3});
		expect(t.value("sound/2/0", "read")).to_equal(std::string{"40"});
		const auto* options = t.find("sound/5/1", "options");
		expect(options != nullptr && options->size() == 2 && (*options)[1] == "Low").to_be_true();
	});
	it("holds a row with no value, and tells it from a missing row", _ {
		spec::OracleTable t = spec::OracleTable::parse("cell/sounds/v3/0,0\tempty\n");
		expect(t.find("cell/sounds/v3/0,0", "empty") != nullptr).to_be_true();
		expect(t.value("cell/sounds/v3/0,0", "empty")).to_equal(std::string{"<no value>"});
		expect(t.value("cell/sounds/v3/0,1", "empty")).to_equal(std::string{"<no row>"});
	});
	it("unescapes a tab, a newline and a backslash, and escapes them back", _ {
		const std::string field = "a\tb\nc\\d";
		expect(spec::oracle_escape(field)).to_equal(std::string{"a\\tb\\nc\\\\d"});
		spec::OracleTable t = spec::OracleTable::parse("p\tc\t" + spec::oracle_escape(field) + "\n");
		expect(t.value("p", "c")).to_equal(field);
	});
	it("reports a line with no column and a second row for one key, and keeps neither", _ {
		spec::OracleTable t = spec::OracleTable::parse("lonely\np\tc\t1\np\tc\t2\n");
		expect(t.problems().size()).to_equal(size_t{2});
		expect(t.size()).to_equal(size_t{1});
		expect(t.value("p", "c")).to_equal(std::string{"1"});
	});
	it("names every row nothing asked, and none once each is asked", _ {
		spec::OracleTable t = spec::OracleTable::parse("a\tx\t1\na\ty/1\t2\na\ty/2\t3\nb\tx\t4\n");
		expect(t.unasked()).to_equal(std::string{"a x\na y/1\na y/2\nb x\n"});
		(void)t.find("a", "x");
		expect(t.columns("a", "y/").size()).to_equal(size_t{2});
		expect(t.unasked()).to_equal(std::string{"b x\n"});
		expect(t.under("b").size()).to_equal(size_t{1});
		expect(t.unasked()).to_equal(std::string{});
	});
	it("names the unasked rows of the columns it is given, and no others", _ {
		spec::OracleTable t = spec::OracleTable::parse("a\tx\t1\na\ty/1\t2\na\tz\t3\nb\ty/2\t4\n");
		expect(t.unasked({"y/"})).to_equal(std::string{"a y/1\nb y/2\n"});
		expect(t.unasked({"x", "z"})).to_equal(std::string{"a x\na z\n"});
		(void)t.find("a", "y/1");
		(void)t.find("a", "x");
		expect(t.unasked({"y/", "x"})).to_equal(std::string{"b y/2\n"});
		expect(t.unasked({})).to_equal(std::string{});
	});
	it("reports a table that is not there, and finds nothing in it", _ {
		spec::OracleTable& t = spec::OracleTable::named("no_such_table");
		expect(t.problems().size()).to_equal(size_t{1});
		expect(t.find("sound", "kids") == nullptr).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(oracle_table_spec, oracle_table)
