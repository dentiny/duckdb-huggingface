# Changelog

## v0.1.0 - 2026-08-06

### Added

- `hf_files` for discovering Hugging Face Parquet files and their physical sizes.
- `hf_scan` for querying Hugging Face datasets through DuckDB's Parquet scanner.
- `hf_schema` for inspecting physical Parquet schemas.
- `hf_profile` for summarizing file, row, row-group, and storage metadata.
- `hf_dataset_size` for measuring Parquet storage and deduplicated external blobs.
- `parquet_dataset_size` for measuring user-provided Parquet file sets and their external blobs.
- Configurable parallel external-blob size resolution.

### Documentation

- Added contribution guidelines and issue templates.
