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
constexpr idx_t DEFAULT_BLOB_SAMPLE_SIZE = 1000;
constexpr idx_t MAX_BLOB_SAMPLE_SIZE = 100000;
constexpr idx_t MAX_BLOB_SAMPLE_POOL_SIZE = 400000;

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

idx_t GetBlobSampleSize(const TableFunctionBindInput &input) {
	auto entry = input.named_parameters.find("blob_sample_size");
	if (entry == input.named_parameters.end() || entry->second.IsNull()) {
		return DEFAULT_BLOB_SAMPLE_SIZE;
	}
	auto sample_size = entry->second.GetValue<int64_t>();
	if (sample_size < 1 || NumericCast<idx_t>(sample_size) > MAX_BLOB_SAMPLE_SIZE) {
		throw InvalidInputException("blob_sample_size must be between 1 and %d", MAX_BLOB_SAMPLE_SIZE);
	}
	return NumericCast<idx_t>(sample_size);
}

idx_t GetBlobSamplePoolSize(const TableFunctionBindInput &input, idx_t sample_size) {
	auto entry = input.named_parameters.find("blob_sample_pool_size");
	if (entry == input.named_parameters.end() || entry->second.IsNull()) {
		return sample_size * 4;
	}
	auto pool_size = entry->second.GetValue<int64_t>();
	if (pool_size < NumericCast<int64_t>(sample_size) || NumericCast<idx_t>(pool_size) > MAX_BLOB_SAMPLE_POOL_SIZE) {
		throw InvalidInputException("blob_sample_pool_size must be between blob_sample_size and %d",
		                            MAX_BLOB_SAMPLE_POOL_SIZE);
	}
	return NumericCast<idx_t>(pool_size);
}

unique_ptr<TableRef> ParseDatasetSizeQuery(ClientContext &context, const string &query) {
	Parser parser(context.GetParserOptions());
	parser.ParseQuery(query);
	auto statement = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	return make_uniq<SubqueryRef>(std::move(statement));
}

string BuildBlobStatsQuery(const string &files, const string &blob_column, const string &blob_hash_column) {
	auto quoted_blob_column = KeywordHelper::WriteQuoted(blob_column, '"');
	if (blob_hash_column.empty()) {
		return StringUtil::Format("blob_stats AS MATERIALIZED ("
		                          "SELECT count(*)::HUGEINT AS logical_blob_count, "
		                          "approx_count_distinct(url)::UBIGINT AS estimated_physical_blob_count "
		                          "FROM (SELECT unnest(%s)::VARCHAR AS url FROM parquet_scan(%s)) "
		                          "WHERE url IS NOT NULL AND url <> ''"
		                          ")",
		                          quoted_blob_column, files);
	}

	return StringUtil::Format(
	    "logical_stats AS MATERIALIZED ("
	    "SELECT count(*)::HUGEINT AS logical_blob_count "
	    "FROM (SELECT unnest(%s)::VARCHAR AS url FROM parquet_scan(%s)) "
	    "WHERE url IS NOT NULL AND url <> ''"
	    "), physical_stats AS MATERIALIZED ("
	    "SELECT approx_count_distinct(blob_hash)::UBIGINT AS estimated_physical_blob_count "
	    "FROM (SELECT unnest(%s)::VARCHAR AS blob_hash FROM parquet_scan(%s)) "
	    "WHERE blob_hash IS NOT NULL AND blob_hash <> ''"
	    "), blob_stats AS MATERIALIZED ("
	    "SELECT logical_blob_count, estimated_physical_blob_count FROM logical_stats CROSS JOIN physical_stats"
	    ")",
	    quoted_blob_column, files, KeywordHelper::WriteQuoted(blob_hash_column, '"'), files);
}

string BuildDatasetEstimateQuery(const HuggingFaceOptions &options, const string &files, idx_t parquet_file_count,
                                 const string &blob_column, const string &blob_hash_column, idx_t blob_sample_size,
                                 idx_t blob_sample_pool_size, idx_t blob_concurrency) {
	auto blob_stats = BuildBlobStatsQuery(files, blob_column, blob_hash_column);
	auto quoted_blob_column = KeywordHelper::WriteQuoted(blob_column, '"');
	return StringUtil::Format(
	    "WITH %s, parquet_sizes AS MATERIALIZED ("
	    "SELECT path, huggingface_internal_blob_size(path, (%s + logical_blob_count * 0)::UBIGINT) AS size_bytes "
	    "FROM unnest(%s) AS paths(path) CROSS JOIN blob_stats"
	    "), parquet_stats AS MATERIALIZED ("
	    "SELECT %s::UBIGINT AS parquet_file_count, coalesce(sum(size_bytes), 0)::HUGEINT AS parquet_size_bytes, "
	    "count(*) FILTER (WHERE size_bytes IS NULL)::UBIGINT AS unresolved_parquet_file_count "
	    "FROM parquet_sizes"
	    "), sample_urls AS MATERIALIZED ("
	    "SELECT DISTINCT url FROM ("
	    "SELECT url FROM ("
	    "SELECT unnest(%s)::VARCHAR AS url FROM parquet_scan(%s)"
	    ") AS all_urls WHERE url IS NOT NULL AND url <> ''"
	    ") AS filtered_urls USING SAMPLE reservoir(%s ROWS)"
	    " LIMIT %s"
	    "), sample_sizes AS MATERIALIZED ("
	    "SELECT url, huggingface_internal_blob_size(url, %s) AS size_bytes FROM sample_urls"
	    "), sample_stats AS MATERIALIZED ("
	    "SELECT count(*)::UBIGINT AS sampled_blob_count, count(size_bytes)::UBIGINT AS resolved_sample_count, "
	    "count(*) FILTER (WHERE size_bytes IS NULL)::UBIGINT AS unresolved_sample_count, "
	    "avg(size_bytes)::DOUBLE AS average_blob_size_bytes FROM sample_sizes"
	    "), estimates AS ("
	    "SELECT *, CASE WHEN average_blob_size_bytes IS NOT NULL "
	    "THEN round(estimated_physical_blob_count * average_blob_size_bytes)::HUGEINT "
	    "END AS estimated_blob_size_bytes "
	    "FROM parquet_stats CROSS JOIN blob_stats CROSS JOIN sample_stats"
	    ") SELECT %s AS repository, %s AS revision, %s AS config, %s AS split, "
	    "parquet_file_count, parquet_size_bytes, unresolved_parquet_file_count, logical_blob_count, "
	    "estimated_physical_blob_count, sampled_blob_count, resolved_sample_count, unresolved_sample_count, "
	    "average_blob_size_bytes, estimated_blob_size_bytes, "
	    "CASE WHEN unresolved_parquet_file_count = 0 AND estimated_blob_size_bytes IS NOT NULL "
	    "THEN parquet_size_bytes + estimated_blob_size_bytes END AS estimated_dataset_size_bytes "
	    "FROM estimates",
	    blob_stats, Value::UBIGINT(blob_concurrency).ToSQLString(), files,
	    Value::UBIGINT(parquet_file_count).ToSQLString(), quoted_blob_column, files,
	    Value::UBIGINT(blob_sample_pool_size).ToSQLString(), Value::UBIGINT(blob_sample_size).ToSQLString(),
	    Value::UBIGINT(blob_concurrency).ToSQLString(), Value(options.repository).ToSQLString(),
	    Value(options.revision).ToSQLString(), Value(options.config).ToSQLString(), Value(options.split).ToSQLString());
}

unique_ptr<TableRef> HFDatasetEstimateBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	auto options = HuggingFaceOptions::Parse(input);
	auto blob_sample_size = GetBlobSampleSize(input);
	auto &file_system = FileSystem::GetFileSystem(context);
	auto matches = file_system.GlobFiles(options.Pattern(), FileGlobOptions::DISALLOW_EMPTY);
	vector<Value> paths;
	paths.reserve(matches.size());
	for (const auto &match : matches) {
		paths.emplace_back(match.path);
	}
	auto files = Value::LIST(LogicalType::VARCHAR, std::move(paths)).ToSQLString();
	auto query = BuildDatasetEstimateQuery(options, files, matches.size(), GetBlobColumn(input),
	                                       GetBlobHashColumn(input), blob_sample_size,
	                                       GetBlobSamplePoolSize(input, blob_sample_size), GetBlobConcurrency(input));
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

void RegisterHFBlobSize(ExtensionLoader &loader) {
	loader.RegisterFunction(ScalarFunction("huggingface_internal_blob_size",
	                                       {LogicalType::VARCHAR, LogicalType::UBIGINT}, LogicalType::BIGINT,
	                                       HFBlobSizeFunction));

	TableFunction hf_dataset_estimate("hf_dataset_estimate", {LogicalType::VARCHAR}, nullptr, nullptr);
	hf_dataset_estimate.named_parameters["revision"] = LogicalType::VARCHAR;
	hf_dataset_estimate.named_parameters["config"] = LogicalType::VARCHAR;
	hf_dataset_estimate.named_parameters["split"] = LogicalType::VARCHAR;
	hf_dataset_estimate.named_parameters["path"] = LogicalType::VARCHAR;
	hf_dataset_estimate.named_parameters["blob_column"] = LogicalType::VARCHAR;
	hf_dataset_estimate.named_parameters["blob_hash_column"] = LogicalType::VARCHAR;
	hf_dataset_estimate.named_parameters["blob_sample_size"] = LogicalType::BIGINT;
	hf_dataset_estimate.named_parameters["blob_sample_pool_size"] = LogicalType::BIGINT;
	hf_dataset_estimate.named_parameters["blob_concurrency"] = LogicalType::BIGINT;
	hf_dataset_estimate.bind_replace = HFDatasetEstimateBindReplace;
	loader.RegisterFunction(hf_dataset_estimate);
}

} // namespace duckdb
