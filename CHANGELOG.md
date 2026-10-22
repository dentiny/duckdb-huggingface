# Changelog

## v0.2.0

### Fixed

- Use the public parser constructor and literal factories, and append blob-size results with correct row counts and NULL validity on DuckDB v2.
- Register optional function arguments through DuckDB v2 typed kwargs and read their descriptions from the function signature.
- Preserve positional argument names in `duckdb_functions()` using the native signature metadata.

### Changed

- Upgrade DuckDB to `v2.0.0`, synchronize extension-ci-tools, and update the embedded cache_httpfs/httpfs dependencies.
- Use DuckDB identifiers and SQL quoting APIs for function parameters and column names.
- Share bind-replacement parsing and named-argument helpers, and avoid copies while trimming paths.
- Record extension versions and static dependencies in the build configuration.

## v0.1.2

### Changed

- Update DuckDB and extension-ci-tools to `v1.5.6` and synchronize the embedded cache_httpfs dependency.

### Added

- Added descriptions, examples, categories, and parameter names to `duckdb_functions()` for all Hugging Face
  functions.

## v0.1.1

### Fixed

- `cache_httpfs` needs to be loaded first ([#13])

[#13]: https://github.com/dentiny/duckdb-huggingface/pull/13

## v0.1.0

### Added

- `hf_files` for discovering Hugging Face Parquet files and reporting per-file Parquet, logical blob, and physical
  blob sizes.
- `hf_scan` for querying Hugging Face datasets through DuckDB's Parquet scanner.
- `hf_profile` for summarizing file, row, row-group, and storage metadata.
- `hf_dataset_estimate` for estimating total Parquet and external-blob storage with HyperLogLog and blob sampling.
- Configurable parallel external-blob size resolution.

### Documentation

- Added contribution guidelines and issue templates.
