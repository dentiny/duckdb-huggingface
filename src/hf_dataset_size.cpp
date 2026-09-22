#include "huggingface_common.hpp"
#include "huggingface_functions.hpp"

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
constexpr idx_t DEFAULT_ROW_SAMPLE_SIZE = 100;
constexpr idx_t MAX_ROW_SAMPLE_SIZE = 100000;

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

string GetBlobHashColumn(const TableFunctionBindInput &input) {
	auto entry = input.named_parameters.find("blob_hash_column");
	if (entry == input.named_parameters.end() || entry->second.IsNull()) {
		return "";
	}
	return entry->second.GetValue<string>();
}

idx_t GetRowSampleSize(const TableFunctionBindInput &input) {
	auto entry = input.named_parameters.find("row_sample_size");
	if (entry == input.named_parameters.end() || entry->second.IsNull()) {
		return DEFAULT_ROW_SAMPLE_SIZE;
	}
	auto sample_size = entry->second.GetValue<int64_t>();
	if (sample_size < 1 || NumericCast<idx_t>(sample_size) > MAX_ROW_SAMPLE_SIZE) {
		throw InvalidInputException("row_sample_size must be between 1 and %d", MAX_ROW_SAMPLE_SIZE);
	}
	return NumericCast<idx_t>(sample_size);
}

unique_ptr<TableRef> ParseDatasetSizeQuery(ClientContext &context, const string &query) {
	Parser parser(context.GetParserOptions());
	parser.ParseQuery(query);
	auto statement = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	return make_uniq<SubqueryRef>(std::move(statement));
}

string BuildSampleQuery(const string &files, const string &blob_column, const string &blob_hash_column,
                        idx_t row_sample_size) {
	auto quoted_blob_column = KeywordHelper::WriteQuoted(blob_column, '"');
	if (blob_hash_column.empty()) {
		return StringUtil::Format("sampled_rows AS MATERIALIZED ("
		                          "SELECT %s FROM parquet_scan(%s) LIMIT %s"
		                          "), sampled_row_stats AS MATERIALIZED ("
		                          "SELECT count(*)::UBIGINT AS sampled_row_count FROM sampled_rows"
		                          "), blob_rows AS MATERIALIZED ("
		                          "SELECT unnest(%s)::VARCHAR AS url FROM sampled_rows"
		                          "), blob_stats AS MATERIALIZED ("
		                          "SELECT (count(*) FILTER (WHERE url IS NOT NULL AND url <> ''))::UBIGINT "
		                          "AS sampled_logical_blob_count FROM blob_rows"
		                          "), blob_sample AS MATERIALIZED ("
		                          "SELECT url, url AS blob_id FROM ("
		                          "SELECT url FROM blob_rows WHERE url IS NOT NULL AND url <> ''"
		                          ") AS valid_blob_rows USING SAMPLE reservoir(%s ROWS)"
		                          "), sampled_identity_stats AS MATERIALIZED ("
		                          "SELECT count(*)::UBIGINT AS sampled_blob_reference_count, "
		                          "count(DISTINCT blob_id)::UBIGINT AS sampled_distinct_blob_count FROM blob_sample"
		                          "), sample_urls AS MATERIALIZED ("
		                          "SELECT any_value(url) AS url FROM blob_sample GROUP BY blob_id"
		                          ")",
		                          quoted_blob_column, files, Value::UBIGINT(row_sample_size).ToSQLString(),
		                          quoted_blob_column, Value::UBIGINT(row_sample_size).ToSQLString());
	}

	return StringUtil::Format("sampled_rows AS MATERIALIZED ("
	                          "SELECT %s, %s FROM parquet_scan(%s) LIMIT %s"
	                          "), sampled_row_stats AS MATERIALIZED ("
	                          "SELECT count(*)::UBIGINT AS sampled_row_count FROM sampled_rows"
	                          "), blob_rows AS MATERIALIZED ("
	                          "SELECT unnest(%s)::VARCHAR AS url, unnest(%s)::VARCHAR AS blob_hash FROM sampled_rows"
	                          "), blob_stats AS MATERIALIZED ("
	                          "SELECT (count(*) FILTER (WHERE url IS NOT NULL AND url <> ''))::UBIGINT "
	                          "AS sampled_logical_blob_count FROM blob_rows"
	                          "), blob_sample AS MATERIALIZED ("
	                          "SELECT url, blob_hash AS blob_id FROM ("
	                          "SELECT url, blob_hash FROM blob_rows "
	                          "WHERE url IS NOT NULL AND url <> '' AND blob_hash IS NOT NULL AND blob_hash <> ''"
	                          ") AS valid_blob_rows USING SAMPLE reservoir(%s ROWS)"
	                          "), sampled_identity_stats AS MATERIALIZED ("
	                          "SELECT count(*)::UBIGINT AS sampled_blob_reference_count, "
	                          "count(DISTINCT blob_id)::UBIGINT AS sampled_distinct_blob_count FROM blob_sample"
	                          "), sample_urls AS MATERIALIZED ("
	                          "SELECT any_value(url) AS url FROM blob_sample GROUP BY blob_id"
	                          ")",
	                          quoted_blob_column, KeywordHelper::WriteQuoted(blob_hash_column, '"'), files,
	                          Value::UBIGINT(row_sample_size).ToSQLString(), quoted_blob_column,
	                          KeywordHelper::WriteQuoted(blob_hash_column, '"'),
	                          Value::UBIGINT(row_sample_size).ToSQLString());
}

string BuildDatasetEstimateQuery(const HuggingFaceOptions &options, const string &sampled_files,
                                 idx_t parquet_file_count, const string &blob_column, const string &blob_hash_column,
                                 idx_t row_sample_size, idx_t blob_concurrency) {
	auto sample_query = BuildSampleQuery(sampled_files, blob_column, blob_hash_column, row_sample_size);
	return StringUtil::Format(
	    "WITH %s, sampled_file AS MATERIALIZED ("
	    "SELECT num_rows::HUGEINT AS sampled_file_row_count, "
	    "file_size_bytes::HUGEINT AS sampled_file_size_bytes FROM parquet_file_metadata(%s)"
	    "), sample_stats AS MATERIALIZED ("
	    "SELECT avg(huggingface_internal_blob_size(url, %s))::DOUBLE AS average_blob_size_bytes "
	    "FROM sample_urls"
	    "), scaled_counts AS MATERIALIZED ("
	    "SELECT %s::UBIGINT AS parquet_file_count, sampled_row_count, "
	    "round(sampled_file_size_bytes::DOUBLE * %s)::HUGEINT AS parquet_size_bytes, "
	    "CASE WHEN sampled_row_count = 0 THEN 0::HUGEINT "
	    "ELSE round(sampled_logical_blob_count::DOUBLE * sampled_file_row_count::DOUBLE * %s / "
	    "sampled_row_count::DOUBLE)::HUGEINT END AS logical_blob_count, "
	    "sampled_logical_blob_count, sampled_blob_reference_count, sampled_distinct_blob_count "
	    "FROM sampled_file CROSS JOIN sampled_row_stats CROSS JOIN blob_stats"
	    " CROSS JOIN sampled_identity_stats"
	    "), estimates AS ("
	    "SELECT *, CASE WHEN sampled_blob_reference_count = 0 THEN 0::UBIGINT "
	    "ELSE round(logical_blob_count::DOUBLE * sampled_distinct_blob_count::DOUBLE / "
	    "sampled_blob_reference_count::DOUBLE)::UBIGINT END AS estimated_physical_blob_count, "
	    "CASE WHEN sampled_blob_reference_count = 0 THEN NULL::DOUBLE "
	    "ELSE 1.0 - sampled_distinct_blob_count::DOUBLE / sampled_blob_reference_count::DOUBLE "
	    "END AS estimated_blob_dup_rate "
	    "FROM scaled_counts CROSS JOIN sample_stats"
	    "), final_estimates AS ("
	    "SELECT *, CASE WHEN average_blob_size_bytes IS NOT NULL "
	    "THEN round(estimated_physical_blob_count * average_blob_size_bytes)::HUGEINT "
	    "END AS estimated_blob_size_bytes "
	    "FROM estimates"
	    ") SELECT %s AS repository, %s AS revision, %s AS config, %s AS split, "
	    "parquet_file_count, sampled_row_count, parquet_size_bytes, logical_blob_count, "
	    "estimated_physical_blob_count, estimated_blob_dup_rate, average_blob_size_bytes, "
	    "estimated_blob_size_bytes, "
	    "CASE WHEN estimated_blob_size_bytes IS NOT NULL "
	    "THEN parquet_size_bytes + estimated_blob_size_bytes END AS estimated_dataset_size_bytes "
	    "FROM final_estimates",
	    sample_query, sampled_files, Value::UBIGINT(blob_concurrency).ToSQLString(),
	    Value::UBIGINT(parquet_file_count).ToSQLString(), Value::UBIGINT(parquet_file_count).ToSQLString(),
	    Value::UBIGINT(parquet_file_count).ToSQLString(), Value(options.repository).ToSQLString(),
	    Value(options.revision).ToSQLString(), Value(options.config).ToSQLString(), Value(options.split).ToSQLString());
}

unique_ptr<TableRef> HFDatasetEstimateBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	auto options = HuggingFaceOptions::Parse(input);
	auto &file_system = FileSystem::GetFileSystem(context);
	auto matches = file_system.GlobFiles(options.Pattern(), FileGlobOptions::DISALLOW_EMPTY);
	vector<Value> sampled_paths;
	sampled_paths.emplace_back(matches[matches.size() / 2].path);
	auto sampled_files = Value::LIST(LogicalType::VARCHAR, std::move(sampled_paths)).ToSQLString();
	auto query =
	    BuildDatasetEstimateQuery(options, sampled_files, matches.size(), GetBlobColumn(input),
	                              GetBlobHashColumn(input), GetRowSampleSize(input), GetBlobConcurrency(input));
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

} // namespace

ScalarFunction GetHFBlobSizeFunction() {
	return ScalarFunction("huggingface_internal_blob_size", {LogicalType::VARCHAR, LogicalType::UBIGINT},
	                      LogicalType::BIGINT, HFBlobSizeFunction);
}

TableFunction GetHFDatasetEstimateFunction() {
	TableFunction hf_dataset_estimate("hf_dataset_estimate", {LogicalType::VARCHAR}, nullptr, nullptr);
	hf_dataset_estimate.named_parameters["revision"] = LogicalType::VARCHAR;
	hf_dataset_estimate.named_parameters["config"] = LogicalType::VARCHAR;
	hf_dataset_estimate.named_parameters["split"] = LogicalType::VARCHAR;
	hf_dataset_estimate.named_parameters["path"] = LogicalType::VARCHAR;
	hf_dataset_estimate.named_parameters["blob_column"] = LogicalType::VARCHAR;
	hf_dataset_estimate.named_parameters["blob_hash_column"] = LogicalType::VARCHAR;
	hf_dataset_estimate.named_parameters["row_sample_size"] = LogicalType::BIGINT;
	hf_dataset_estimate.named_parameters["blob_concurrency"] = LogicalType::BIGINT;
	hf_dataset_estimate.bind_replace = HFDatasetEstimateBindReplace;
	return hf_dataset_estimate;
}

} // namespace duckdb
