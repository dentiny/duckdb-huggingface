#include "huggingface_common.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"

namespace duckdb {

namespace {

unique_ptr<TableRef> ParseProfileQuery(ClientContext &context, const string &query) {
	Parser parser(context.GetParserOptions());
	parser.ParseQuery(query);
	auto statement = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	return make_uniq<SubqueryRef>(std::move(statement));
}

unique_ptr<TableRef> HFProfileBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	auto options = HuggingFaceOptions::Parse(input);
	auto config = options.config.empty() ? "*" : options.config;
	auto split = options.split.empty() ? "*" : options.split;
	auto query = "SELECT " + Value(options.repository).ToSQLString() + " AS repository, " +
	             Value(options.revision).ToSQLString() + " AS revision, " + Value(config).ToSQLString() +
	             " AS config, " + Value(split).ToSQLString() +
	             " AS split, count(*)::UBIGINT AS file_count, sum(num_rows)::HUGEINT AS total_rows, "
	             "sum(num_row_groups)::HUGEINT AS total_row_groups, "
	             "sum(file_size_bytes)::HUGEINT AS total_file_size_bytes "
	             "FROM parquet_file_metadata(" +
	             Value(options.Pattern()).ToSQLString() + ")";
	return ParseProfileQuery(context, query);
}

} // namespace

void RegisterHFProfile(ExtensionLoader &loader) {
	TableFunction function("hf_profile", {LogicalType::VARCHAR}, nullptr, nullptr);
	function.named_parameters["revision"] = LogicalType::VARCHAR;
	function.named_parameters["config"] = LogicalType::VARCHAR;
	function.named_parameters["split"] = LogicalType::VARCHAR;
	function.named_parameters["path"] = LogicalType::VARCHAR;
	function.bind_replace = HFProfileBindReplace;
	loader.RegisterFunction(function);
}

} // namespace duckdb
