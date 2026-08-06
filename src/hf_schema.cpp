#include "huggingface_common.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"

namespace duckdb {

namespace {

unique_ptr<TableRef> HFSchemaBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	auto pattern = HuggingFaceOptions::Parse(input).Pattern();
	vector<unique_ptr<ParsedExpression>> arguments;
	arguments.push_back(make_uniq<ConstantExpression>(Value(pattern)));
	auto result = make_uniq<TableFunctionRef>();
	result->function = make_uniq<FunctionExpression>("parquet_schema", std::move(arguments));
	return std::move(result);
}

} // namespace

void RegisterHFSchema(ExtensionLoader &loader) {
	TableFunction function("hf_schema", {LogicalType::VARCHAR}, nullptr, nullptr);
	function.named_parameters["revision"] = LogicalType::VARCHAR;
	function.named_parameters["config"] = LogicalType::VARCHAR;
	function.named_parameters["split"] = LogicalType::VARCHAR;
	function.named_parameters["path"] = LogicalType::VARCHAR;
	function.bind_replace = HFSchemaBindReplace;
	loader.RegisterFunction(function);
}

} // namespace duckdb
