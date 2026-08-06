#define DUCKDB_EXTENSION_MAIN

#include "huggingface_common.hpp"
#include "huggingface_extension.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/connection.hpp"

namespace duckdb {

namespace {

void DisablePersistentCache(ExtensionLoader &loader) {
	auto &instance = loader.GetDatabaseInstance();
	auto &config = DBConfig::GetConfig(instance);
	ExtensionOption cache_type;
	if (!config.TryGetExtensionOption("cache_httpfs_type", cache_type)) {
		throw InternalException("cache_httpfs_type setting is unavailable; cache_httpfs must load before huggingface");
	}

	Connection connection(instance);
	auto result = connection.Query("SET GLOBAL cache_httpfs_type = 'in_mem'");
	if (result->HasError()) {
		throw InternalException("Failed to disable persistent cache_httpfs caching: %s", result->GetError());
	}
}

void LoadInternal(ExtensionLoader &loader) {
	DisablePersistentCache(loader);
	RegisterHFFiles(loader);
	RegisterHFScan(loader);
	RegisterHFProfile(loader);
	RegisterHFBlobSize(loader);
}

} // namespace

void HuggingfaceExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string HuggingfaceExtension::Name() {
	return "huggingface";
}

std::string HuggingfaceExtension::Version() const {
#ifdef EXT_VERSION_HUGGINGFACE
	return EXT_VERSION_HUGGINGFACE;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(huggingface, loader) {
	duckdb::LoadInternal(loader);
}
}
