#include "huggingface_common.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/vector_operations/unary_executor.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"

namespace duckdb {

namespace {

string GetBlobColumn(const TableFunctionBindInput &input) {
	auto entry = input.named_parameters.find("blob_column");
	if (entry == input.named_parameters.end() || entry->second.IsNull()) {
		return "images";
	}
	auto column = entry->second.GetValue<string>();
	if (column.empty()) {
		throw InvalidInputException("blob_column cannot be empty");
	}
	return column;
}

unique_ptr<TableRef> ParseDatasetSizeQuery(ClientContext &context, const string &query) {
	Parser parser(context.GetParserOptions());
	parser.ParseQuery(query);
	auto statement = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	return make_uniq<SubqueryRef>(std::move(statement));
}

string BuildDatasetSizeQuery(const string &files, const string &scan, const string &blob_column) {
	auto column = KeywordHelper::WriteQuoted(blob_column, '"');
	return StringUtil::Format(
	    "WITH parquet_stats AS ("
	    "SELECT count(*)::UBIGINT AS parquet_file_count, "
	    "coalesce(sum(file_size_bytes), 0)::HUGEINT AS parquet_size_bytes "
	    "FROM parquet_file_metadata(%s)"
	    "), blob_urls AS ("
	    "SELECT DISTINCT url FROM (SELECT unnest(%s)::VARCHAR AS url FROM %s) "
	    "WHERE url IS NOT NULL AND url <> ''"
	    "), blob_sizes AS ("
	    "SELECT url, hf_blob_size(url) AS size_bytes FROM blob_urls"
	    "), blob_stats AS ("
	    "SELECT count(*)::UBIGINT AS external_blob_count, "
	    "coalesce(sum(size_bytes), 0)::HUGEINT AS external_blob_size_bytes, "
	    "count(*) FILTER (WHERE size_bytes IS NULL)::UBIGINT AS unresolved_blob_count "
	    "FROM blob_sizes"
	    ") SELECT parquet_file_count, parquet_size_bytes, external_blob_count, "
	    "external_blob_size_bytes, unresolved_blob_count, "
	    "parquet_size_bytes + external_blob_size_bytes AS known_overall_size_bytes, "
	    "CASE WHEN unresolved_blob_count = 0 "
	    "THEN parquet_size_bytes + external_blob_size_bytes END AS overall_size_bytes "
	    "FROM parquet_stats CROSS JOIN blob_stats",
	    files, column, scan);
}

string HFCall(const string &function_name, const HuggingFaceOptions &options) {
	return StringUtil::Format("%s(%s, revision := %s, config := %s, split := %s, path := %s)", function_name,
	                          Value(options.repository).ToSQLString(), Value(options.revision).ToSQLString(),
	                          Value(options.config).ToSQLString(), Value(options.split).ToSQLString(),
	                          Value(options.path).ToSQLString());
}

unique_ptr<TableRef> HFDatasetSizeBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	auto options = HuggingFaceOptions::Parse(input);
	auto query =
	    BuildDatasetSizeQuery(Value(options.Pattern()).ToSQLString(), HFCall("hf_scan", options), GetBlobColumn(input));
	return ParseDatasetSizeQuery(context, query);
}

unique_ptr<TableRef> ParquetDatasetSizeBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs[0].IsNull()) {
		throw InvalidInputException("Parquet files cannot be NULL");
	}
	auto files = input.inputs[0].ToSQLString();
	auto query =
	    BuildDatasetSizeQuery(files, StringUtil::Format("parquet_scan(%s)", files), GetBlobColumn(input));
	return ParseDatasetSizeQuery(context, query);
}

void HFBlobSizeFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &file_system = FileSystem::GetFileSystem(state.GetContext());
	UnaryExecutor::ExecuteWithNulls<string_t, int64_t>(
	    args.data[0], result, args.size(), [&](string_t input, ValidityMask &mask, idx_t row_index) {
		    try {
			    auto handle = file_system.OpenFile(input.GetString(), FileFlags::FILE_FLAGS_READ);
			    auto size = file_system.GetFileSize(*handle);
			    if (size < 0) {
				    mask.SetInvalid(row_index);
				    return int64_t(0);
			    }
			    return size;
		    } catch (...) {
			    mask.SetInvalid(row_index);
			    return int64_t(0);
		    }
	    });
}

void AddBlobColumnParameter(TableFunction &function) {
	function.named_parameters["blob_column"] = LogicalType::VARCHAR;
}

} // namespace

void RegisterHFDatasetSize(ExtensionLoader &loader) {
	loader.RegisterFunction(
	    ScalarFunction("hf_blob_size", {LogicalType::VARCHAR}, LogicalType::BIGINT, HFBlobSizeFunction));

	TableFunction hf_dataset_size("hf_dataset_size", {LogicalType::VARCHAR}, nullptr, nullptr);
	hf_dataset_size.named_parameters["revision"] = LogicalType::VARCHAR;
	hf_dataset_size.named_parameters["config"] = LogicalType::VARCHAR;
	hf_dataset_size.named_parameters["split"] = LogicalType::VARCHAR;
	hf_dataset_size.named_parameters["path"] = LogicalType::VARCHAR;
	AddBlobColumnParameter(hf_dataset_size);
	hf_dataset_size.bind_replace = HFDatasetSizeBindReplace;
	loader.RegisterFunction(hf_dataset_size);

	TableFunction parquet_dataset_size("parquet_dataset_size", {LogicalType::LIST(LogicalType::VARCHAR)}, nullptr,
	                                   nullptr);
	AddBlobColumnParameter(parquet_dataset_size);
	parquet_dataset_size.bind_replace = ParquetDatasetSizeBindReplace;
	loader.RegisterFunction(parquet_dataset_size);
}

} // namespace duckdb
