#include "huggingface_common.hpp"
#include "huggingface_functions.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"

namespace duckdb {

namespace {

bool GetNamedBoolean(const TableFunctionBindInput &input, const Identifier &name, bool default_value) {
	auto entry = input.named_parameters.find(name);
	if (entry == input.named_parameters.end() || entry->second.IsNull()) {
		return default_value;
	}
	return entry->second.GetValue<bool>();
}

unique_ptr<TableRef> HFScanBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	auto pattern = HuggingFaceOptions::Parse(input).Pattern();
	vector<unique_ptr<ParsedExpression>> arguments;
	arguments.push_back(make_uniq<ConstantExpression>(Value(pattern)));

	auto union_by_name = make_uniq<ConstantExpression>(Value::BOOLEAN(GetNamedBoolean(input, "union_by_name", true)));
	union_by_name->SetAlias("union_by_name");
	arguments.push_back(std::move(union_by_name));

	auto filename = make_uniq<ConstantExpression>(Value::BOOLEAN(GetNamedBoolean(input, "filename", false)));
	filename->SetAlias("filename");
	arguments.push_back(std::move(filename));

	auto result = make_uniq<TableFunctionRef>();
	result->function = make_uniq<FunctionExpression>(Identifier("parquet_scan"), std::move(arguments));
	return result;
}

} // namespace

TableFunction GetHFScanFunction() {
	TableFunction function("hf_scan", {LogicalType::VARCHAR}, nullptr, nullptr);
	function.GetSignature().WithTypedKwargs("options", [](TypedKwargs &options) {
		options.Add("revision", LogicalType::VARCHAR);
		options.Add("config", LogicalType::VARCHAR);
		options.Add("split", LogicalType::VARCHAR);
		options.Add("path", LogicalType::VARCHAR);
		options.Add("union_by_name", LogicalType::BOOLEAN);
		options.Add("filename", LogicalType::BOOLEAN);
	});
	function.bind_replace = HFScanBindReplace;
	return function;
}

} // namespace duckdb
