#include "huggingface_common.hpp"

#include "duckdb/common/file_system.hpp"

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
	idx_t size_bytes;
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

vector<HuggingFaceFile> GlobFiles(ClientContext &context, const TableFunctionBindInput &input) {
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
		auto handle = file_system.OpenFile(match, FileFlags::FILE_FLAGS_READ);
		auto file_size = file_system.GetFileSize(*handle);
		if (file_size < 0) {
			throw IOException("Could not determine size of Hugging Face file '%s'", match.path);
		}
		result.push_back({options.repository, options.revision, config, split, result.size(), match.path,
		                  NumericCast<idx_t>(file_size)});
	}
	std::sort(result.begin(), result.end(),
	          [](const HuggingFaceFile &left, const HuggingFaceFile &right) { return left.path < right.path; });
	for (idx_t index = 0; index < result.size(); index++) {
		result[index].file_index = index;
	}
	return result;
}

struct HFFilesBindData : public TableFunctionData {
	explicit HFFilesBindData(vector<HuggingFaceFile> files_p) : files(std::move(files_p)) {
	}

	vector<HuggingFaceFile> files;
};

struct HFFilesGlobalState : public GlobalTableFunctionState {
	idx_t offset = 0;

	idx_t MaxThreads() const override {
		return 1;
	}
};

unique_ptr<FunctionData> HFFilesBind(ClientContext &context, TableFunctionBindInput &input,
                                     vector<LogicalType> &return_types, vector<string> &names) {
	return_types = {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR,
	                LogicalType::UBIGINT, LogicalType::VARCHAR, LogicalType::UBIGINT};
	names = {"repository", "revision", "config", "split", "file_index", "path", "size_bytes"};
	return make_uniq<HFFilesBindData>(GlobFiles(context, input));
}

unique_ptr<GlobalTableFunctionState> HFFilesInit(ClientContext &context, TableFunctionInitInput &input) {
	return make_uniq<HFFilesGlobalState>();
}

void HFFilesFunction(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &bind_data = input.bind_data->Cast<HFFilesBindData>();
	auto &state = input.global_state->Cast<HFFilesGlobalState>();
	auto remaining = bind_data.files.size() - state.offset;
	auto count = MinValue<idx_t>(STANDARD_VECTOR_SIZE, remaining);
	for (idx_t row_index = 0; row_index < count; row_index++) {
		auto &file = bind_data.files[state.offset + row_index];
		output.data[0].SetValue(row_index, Value(file.repository));
		output.data[1].SetValue(row_index, Value(file.revision));
		output.data[2].SetValue(row_index, Value(file.config));
		output.data[3].SetValue(row_index, Value(file.split));
		output.data[4].SetValue(row_index, Value::UBIGINT(file.file_index));
		output.data[5].SetValue(row_index, Value(file.path));
		output.data[6].SetValue(row_index, Value::UBIGINT(file.size_bytes));
	}
	state.offset += count;
	output.SetCardinality(count);
}

} // namespace

void RegisterHFFiles(ExtensionLoader &loader) {
	TableFunction function("hf_files", {LogicalType::VARCHAR}, HFFilesFunction, HFFilesBind, HFFilesInit);
	function.named_parameters["revision"] = LogicalType::VARCHAR;
	function.named_parameters["config"] = LogicalType::VARCHAR;
	function.named_parameters["split"] = LogicalType::VARCHAR;
	function.named_parameters["path"] = LogicalType::VARCHAR;
	loader.RegisterFunction(function);
}

} // namespace duckdb
