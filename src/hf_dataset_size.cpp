#include "huggingface_common.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"

#include <atomic>
#include <thread>

namespace duckdb {

namespace {

constexpr idx_t DEFAULT_BLOB_CONCURRENCY = 32;
constexpr idx_t MAX_BLOB_CONCURRENCY = 256;

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

idx_t GetBlobConcurrency(const TableFunctionBindInput &input) {
	auto entry = input.named_parameters.find("blob_concurrency");
	if (entry == input.named_parameters.end() || entry->second.IsNull()) {
		return DEFAULT_BLOB_CONCURRENCY;
	}
	auto concurrency = entry->second.GetValue<int64_t>();
	if (concurrency < 1 || NumericCast<idx_t>(concurrency) > MAX_BLOB_CONCURRENCY) {
		throw InvalidInputException("blob_concurrency must be between 1 and %d", MAX_BLOB_CONCURRENCY);
	}
	return NumericCast<idx_t>(concurrency);
}

unique_ptr<TableRef> ParseDatasetSizeQuery(ClientContext &context, const string &query) {
	Parser parser(context.GetParserOptions());
	parser.ParseQuery(query);
	auto statement = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	return make_uniq<SubqueryRef>(std::move(statement));
}

string BuildDatasetSizeQuery(const string &files, const string &scan, const string &blob_column,
                             idx_t blob_concurrency) {
	auto column = KeywordHelper::WriteQuoted(blob_column, '"');
	auto concurrency = Value::UBIGINT(blob_concurrency).ToSQLString();
	return StringUtil::Format("WITH parquet_stats AS ("
	                          "SELECT count(*)::UBIGINT AS parquet_file_count, "
	                          "coalesce(sum(file_size_bytes), 0)::HUGEINT AS parquet_size_bytes "
	                          "FROM parquet_file_metadata(%s)"
	                          "), blob_urls AS ("
	                          "SELECT DISTINCT url FROM (SELECT unnest(%s)::VARCHAR AS url FROM %s) "
	                          "WHERE url IS NOT NULL AND url <> ''"
	                          "), blob_sizes AS ("
	                          "SELECT url, hf_blob_size(url, %s) AS size_bytes FROM blob_urls"
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
	                          files, column, scan, concurrency);
}

string HFCall(const string &function_name, const HuggingFaceOptions &options) {
	return StringUtil::Format("%s(%s, revision := %s, config := %s, split := %s, path := %s)", function_name,
	                          Value(options.repository).ToSQLString(), Value(options.revision).ToSQLString(),
	                          Value(options.config).ToSQLString(), Value(options.split).ToSQLString(),
	                          Value(options.path).ToSQLString());
}

unique_ptr<TableRef> HFDatasetSizeBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	auto options = HuggingFaceOptions::Parse(input);
	auto query = BuildDatasetSizeQuery(Value(options.Pattern()).ToSQLString(), HFCall("hf_scan", options),
	                                   GetBlobColumn(input), GetBlobConcurrency(input));
	return ParseDatasetSizeQuery(context, query);
}

unique_ptr<TableRef> ParquetDatasetSizeBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs[0].IsNull()) {
		throw InvalidInputException("Parquet files cannot be NULL");
	}
	auto files = input.inputs[0].ToSQLString();
	auto query = BuildDatasetSizeQuery(files, StringUtil::Format("parquet_scan(%s)", files), GetBlobColumn(input),
	                                   GetBlobConcurrency(input));
	return ParseDatasetSizeQuery(context, query);
}

void HFBlobSizeFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto concurrency = args.data[1].GetValue(0).GetValue<uint64_t>();
	concurrency = MinValue<uint64_t>(concurrency, count);

	UnifiedVectorFormat input_data;
	args.data[0].ToUnifiedFormat(count, input_data);
	auto input_urls = UnifiedVectorFormat::GetData<string_t>(input_data);
	vector<int64_t> sizes(count);
	vector<uint8_t> resolved(count, 0);
	auto &file_system = FileSystem::GetFileSystem(state.GetContext());
	std::atomic<idx_t> next_row(0);

	auto worker = [&]() {
		while (true) {
			auto row_index = next_row.fetch_add(1);
			if (row_index >= count) {
				return;
			}
			auto source_index = input_data.sel->get_index(row_index);
			if (!input_data.validity.RowIsValid(source_index)) {
				continue;
			}
			try {
				auto handle = file_system.OpenFile(input_urls[source_index].GetString(), FileFlags::FILE_FLAGS_READ);
				auto size = file_system.GetFileSize(*handle);
				if (size >= 0) {
					sizes[row_index] = size;
					resolved[row_index] = 1;
				}
			} catch (...) {
			}
		}
	};

	vector<std::thread> workers;
	workers.reserve(concurrency);
	for (idx_t worker_index = 0; worker_index < concurrency; worker_index++) {
		workers.emplace_back(worker);
	}
	for (auto &thread : workers) {
		thread.join();
	}

	result.SetVectorType(VectorType::FLAT_VECTOR);
	auto output = FlatVector::GetData<int64_t>(result);
	auto &validity = FlatVector::Validity(result);
	for (idx_t row_index = 0; row_index < count; row_index++) {
		if (resolved[row_index]) {
			output[row_index] = sizes[row_index];
		} else {
			validity.SetInvalid(row_index);
		}
	}
}

void AddBlobParameters(TableFunction &function) {
	function.named_parameters["blob_column"] = LogicalType::VARCHAR;
	function.named_parameters["blob_concurrency"] = LogicalType::BIGINT;
}

} // namespace

void RegisterHFDatasetSize(ExtensionLoader &loader) {
	loader.RegisterFunction(ScalarFunction("hf_blob_size", {LogicalType::VARCHAR, LogicalType::UBIGINT},
	                                       LogicalType::BIGINT, HFBlobSizeFunction));

	TableFunction hf_dataset_size("hf_dataset_size", {LogicalType::VARCHAR}, nullptr, nullptr);
	hf_dataset_size.named_parameters["revision"] = LogicalType::VARCHAR;
	hf_dataset_size.named_parameters["config"] = LogicalType::VARCHAR;
	hf_dataset_size.named_parameters["split"] = LogicalType::VARCHAR;
	hf_dataset_size.named_parameters["path"] = LogicalType::VARCHAR;
	AddBlobParameters(hf_dataset_size);
	hf_dataset_size.bind_replace = HFDatasetSizeBindReplace;
	loader.RegisterFunction(hf_dataset_size);

	TableFunction parquet_dataset_size("parquet_dataset_size", {LogicalType::LIST(LogicalType::VARCHAR)}, nullptr,
	                                   nullptr);
	AddBlobParameters(parquet_dataset_size);
	parquet_dataset_size.bind_replace = ParquetDatasetSizeBindReplace;
	loader.RegisterFunction(parquet_dataset_size);
}

} // namespace duckdb
