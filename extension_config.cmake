# Extension dependencies and metadata for the local DuckDB build.
duckdb_extension_load(json)
duckdb_extension_load(parquet)
duckdb_extension_load(
  cache_httpfs SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/cache-httpfs
  EXTENSION_VERSION 0.15.0)
duckdb_extension_statically_link(cache_httpfs)

duckdb_extension_load(huggingface SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
                      EXTENSION_VERSION 0.2.0 LOAD_TESTS)
duckdb_extension_statically_link(huggingface)
