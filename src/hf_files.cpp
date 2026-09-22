#include "huggingface_common.hpp"
#include "huggingface_functions.hpp"

#include "duckdb/common/file_system.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"

#include <algorithm>

namespace duckdb {

namespace {

struct HuggingFaceFile {
	string repository;
	string revision;
	string config;
	string split;
	idx_t file_index;
	string path;
	idx_t parquet_size_bytes;
};

vector<string> SplitPath(const string &path) {
	vector<string> result;
	idx_t start = 0;
	while (start < path.size()) {
		auto end = path.find('/', start);
		if (end == string::npos) {
			end = path.size();
		}
		if (end > start) {
			result.push_back(path.substr(start, end - start));
		}
		start = end + 1;
	}
	return result;
}

void InferConfigAndSplit(const string &file_path, const string &repository, string &config, string &split) {
	auto repository_position = file_path.find(repository);
	if (repository_position == string::npos) {
		return;
	}
	auto path_position = file_path.find('/', repository_position + repository.size());
	if (path_position == string::npos) {
		return;
	}
	auto components = SplitPath(file_path.substr(path_position + 1));
	if (components.size() >= 2) {
		config = components[0];
		split = components[1];
	}
}

vector<HuggingFaceFile> GlobFiles(ClientContext &context, const TableFunctionBindInput &input, bool load_file_sizes) {
	auto options = HuggingFaceOptions::Parse(input);

	auto &file_system = FileSystem::GetFileSystem(context);
	auto matches = file_system.GlobFiles(options.Pattern(), FileGlobOptions::DISALLOW_EMPTY);
	vector<HuggingFaceFile> result;
	result.reserve(matches.size());
	for (auto &match : matches) {
		string config = options.config;
		string split = options.split;
		if (config.empty() || split.empty()) {
			InferConfigAndSplit(match.path, options.repository, config, split);
		}
		idx_t file_size = 0;
		if (load_file_sizes) {
			auto handle = file_system.OpenFile(match, FileFlags::FILE_FLAGS_READ);
			auto resolved_size = file_system.GetFileSize(*handle);
			if (resolved_size < 0) {
				throw IOException("Could not determine size of Hugging Face file '%s'", match.path);
			}
			file_size = NumericCast<idx_t>(resolved_size);
		}
		result.push_back({options.repository, options.revision, config, split, result.size(), match.path, file_size});
	}
	std::sort(result.begin(), result.end(),
	          [](const HuggingFaceFile &left, const HuggingFaceFile &right) { return left.path < right.path; });
	for (idx_t index = 0; index < result.size(); index++) {
		result[index].file_index = index;
	}
	return result;
}

string GetNamedString(const TableFunctionBindInput &input, const string &name) {
	auto entry = input.named_parameters.find(name);
	if (entry == input.named_parameters.end() || entry->second.IsNull()) {
		return "";
	}
	return entry->second.GetValue<string>();
}

idx_t GetBlobConcurrency(const TableFunctionBindInput &input) {
	auto entry = input.named_parameters.find("blob_concurrency");
	if (entry == input.named_parameters.end() || entry->second.IsNull()) {
		return 32;
	}
	auto concurrency = entry->second.GetValue<int64_t>();
	if (concurrency < 1 || concurrency > 256) {
		throw InvalidInputException("blob_concurrency must be between 1 and 256");
	}
	return NumericCast<idx_t>(concurrency);
}

unique_ptr<TableRef> ParseFilesQuery(ClientContext &context, const string &query) {
	Parser parser(context.GetParserOptions());
	parser.ParseQuery(query);
	auto statement = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	return make_uniq<SubqueryRef>(std::move(statement));
}

string BuildFilesValues(const vector<HuggingFaceFile> &files) {
	vector<string> rows;
	rows.reserve(files.size());
	for (const auto &file : files) {
		rows.push_back(StringUtil::Format("(%s, %s, %s, %s, %s::UBIGINT, %s, %s::UBIGINT)",
		                                  Value(file.repository).ToSQLString(), Value(file.revision).ToSQLString(),
		                                  Value(file.config).ToSQLString(), Value(file.split).ToSQLString(),
		                                  Value::UBIGINT(file.file_index).ToSQLString(), Value(file.path).ToSQLString(),
		                                  Value::UBIGINT(file.parquet_size_bytes).ToSQLString()));
	}
	return StringUtil::Join(rows, ", ");
}

string BuildParquetOnlyQuery(const string &file_values) {
	return StringUtil::Format(
	    "WITH files(repository, revision, config, split, file_index, path, parquet_size_bytes) AS (VALUES %s) "
	    "SELECT repository, revision, config, split, file_index, path, parquet_size_bytes "
	    "FROM files ORDER BY file_index",
	    file_values);
}

string BuildBlobQuery(const string &file_values, const string &file_paths, const string &blob_column,
                      idx_t blob_concurrency) {
	return StringUtil::Format(
	    "WITH files(repository, revision, config, split, file_index, path, parquet_size_bytes) AS (VALUES %s), "
	    "blob_refs AS MATERIALIZED ("
	    "SELECT filename AS path, url FROM ("
	    "SELECT filename, unnest(%s)::VARCHAR AS url FROM parquet_scan(%s, filename := true)"
	    ") WHERE url IS NOT NULL AND url <> ''"
	    "), parquet_sizes AS MATERIALIZED ("
	    "SELECT files.path, huggingface_internal_blob_size(files.path, %s + scanned_rows * 0) AS parquet_size_bytes "
	    "FROM files CROSS JOIN (SELECT count(*)::UBIGINT AS scanned_rows FROM blob_refs)"
	    "), blob_sizes AS MATERIALIZED ("
	    "SELECT url, huggingface_internal_blob_size(url, %s) AS blob_size_bytes "
	    "FROM (SELECT DISTINCT url FROM blob_refs)"
	    "), logical_stats AS ("
	    "SELECT path, count(*)::UBIGINT AS logical_blob_count, "
	    "coalesce(sum(blob_size_bytes), 0)::HUGEINT AS logical_blob_size_bytes "
	    "FROM blob_refs LEFT JOIN blob_sizes USING (url) GROUP BY path"
	    "), physical_refs AS MATERIALIZED (SELECT DISTINCT path, url FROM blob_refs), "
	    "physical_stats AS ("
	    "SELECT path, count(*)::UBIGINT AS physical_blob_count, "
	    "coalesce(sum(blob_size_bytes), 0)::HUGEINT AS physical_blob_size_bytes, "
	    "count(*) FILTER (WHERE blob_size_bytes IS NULL)::UBIGINT AS unresolved_blob_count "
	    "FROM physical_refs LEFT JOIN blob_sizes USING (url) GROUP BY path"
	    ") SELECT files.repository, files.revision, files.config, files.split, files.file_index, files.path, "
	    "CASE WHEN parquet_sizes.parquet_size_bytes IS NOT NULL AND coalesce(unresolved_blob_count, 0) = 0 "
	    "THEN parquet_sizes.parquet_size_bytes + coalesce(physical_blob_size_bytes, 0) END::HUGEINT AS size_bytes, "
	    "parquet_sizes.parquet_size_bytes::UBIGINT AS parquet_size_bytes, "
	    "coalesce(logical_blob_size_bytes, 0)::HUGEINT AS logical_blob_size_bytes, "
	    "coalesce(physical_blob_size_bytes, 0)::HUGEINT AS physical_blob_size_bytes, "
	    "coalesce(logical_blob_count, 0)::UBIGINT AS logical_blob_count, "
	    "coalesce(physical_blob_count, 0)::UBIGINT AS physical_blob_count, "
	    "coalesce(unresolved_blob_count, 0)::UBIGINT AS unresolved_blob_count "
	    "FROM files LEFT JOIN parquet_sizes USING (path) LEFT JOIN logical_stats USING (path) "
	    "LEFT JOIN physical_stats USING (path) "
	    "ORDER BY files.file_index",
	    file_values, KeywordHelper::WriteQuoted(blob_column, '"'), file_paths,
	    Value::UBIGINT(blob_concurrency).ToSQLString(), Value::UBIGINT(blob_concurrency).ToSQLString());
}

unique_ptr<TableRef> HFFilesBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	auto blob_column = GetNamedString(input, "blob_column");
	auto files = GlobFiles(context, input, blob_column.empty());
	auto file_values = BuildFilesValues(files);
	if (blob_column.empty()) {
		return ParseFilesQuery(context, BuildParquetOnlyQuery(file_values));
	}

	vector<Value> paths;
	paths.reserve(files.size());
	for (const auto &file : files) {
		paths.emplace_back(file.path);
	}
	auto file_paths = Value::LIST(LogicalType::VARCHAR, std::move(paths)).ToSQLString();
	return ParseFilesQuery(context, BuildBlobQuery(file_values, file_paths, blob_column, GetBlobConcurrency(input)));
}

} // namespace

TableFunction GetHFFilesFunction() {
	TableFunction function("hf_files", {LogicalType::VARCHAR}, nullptr, nullptr);
	function.named_parameters["revision"] = LogicalType::VARCHAR;
	function.named_parameters["config"] = LogicalType::VARCHAR;
	function.named_parameters["split"] = LogicalType::VARCHAR;
	function.named_parameters["path"] = LogicalType::VARCHAR;
	function.named_parameters["blob_column"] = LogicalType::VARCHAR;
	function.named_parameters["blob_concurrency"] = LogicalType::BIGINT;
	function.bind_replace = HFFilesBindReplace;
	return function;
}

} // namespace duckdb
