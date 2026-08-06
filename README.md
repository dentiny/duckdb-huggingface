# DuckDB Hugging Face

A DuckDB extension for discovering, inspecting, profiling, and querying Parquet datasets on the Hugging Face Hub.
It uses cache_httpfs, which wraps DuckDB HTTPFS and its `hf://` filesystem with remote-read caching, while retaining
Parquet projection and filter pushdown.

## Usage

Load the required extensions:

```sql
LOAD cache_httpfs;
LOAD parquet;
LOAD huggingface;
```

The default revision is Hugging Face's auto-converted `~parquet` branch. Repositories use the `owner/name` form.

### Discover Parquet files

List every matching Parquet file:

```sql
FROM hf_files(
    'ibm/duorc',
    config = 'ParaphraseRC',
    split = 'train'
);
```

`hf_files` returns repository, revision, config, split, file index, path, and physical Parquet `size_bytes`.

Count files and measure their physical storage:

```sql
SELECT
    count(*) AS parquet_file_count,
    sum(size_bytes) AS parquet_size_bytes
FROM hf_files('mlfoundations/MINT-1T-HTML');
```

### Inspect the schema

```sql
FROM hf_schema(
    'ibm/duorc',
    config = 'ParaphraseRC',
    split = 'train'
);
```

### Query a dataset

```sql
SELECT *
FROM hf_scan(
    'ibm/duorc',
    config = 'ParaphraseRC',
    split = 'train'
)
LIMIT 10;
```

`hf_scan` delegates to DuckDB's `parquet_scan`, preserving glob expansion, projection pushdown, filter pushdown,
revision handling, and Hugging Face authentication.

### Profile Parquet metadata

```sql
FROM hf_profile(
    'ibm/duorc',
    config = 'ParaphraseRC',
    split = 'train'
);
```

The profile reports file count, rows, row groups, and physical Parquet storage.

### Measure complete dataset storage

The physical Parquet size includes binary values stored inline, but it does not include blobs referenced by external
URLs. `hf_dataset_size` scans a `VARCHAR[]` URL column, deduplicates its URLs, resolves their object sizes in parallel,
and combines them with the Parquet size:

```sql
FROM hf_dataset_size(
    'mlfoundations/MINT-1T-HTML',
    blob_column = 'images',
    blob_concurrency = 64
);
```

For user-provided Parquet files:

```sql
FROM parquet_dataset_size(
    ['part-0000.parquet', 'part-0001.parquet'],
    blob_column = 'images',
    blob_concurrency = 64
);
```

Blob concurrency defaults to 32 and accepts values from 1 to 256. URLs that cannot be opened or do not expose a
size are counted in `unresolved_blob_count`. The exact `overall_size_bytes` is `NULL` unless every blob resolves;
`known_overall_size_bytes` always reports the sum of known storage.

External sizing performs one remote metadata request per distinct URL. It can be expensive for datasets containing
millions or billions of unique blobs.

## Supported functions

- `hf_files(repository, ...)`: discover Parquet files and physical object sizes.
- `hf_scan(repository, ...)`: query a Hugging Face dataset with DuckDB's Parquet scanner.
- `hf_schema(repository, ...)`: inspect the physical Parquet schema.
- `hf_profile(repository, ...)`: summarize file, row, row-group, and storage metadata.
- `hf_dataset_size(repository, ...)`: measure Hugging Face Parquet and external-blob storage.
- `parquet_dataset_size(files, ...)`: measure user-provided Parquet files and external-blob storage.
- `hf_blob_size(url, concurrency)`: resolve external object sizes through DuckDB's filesystem layer.

The Hugging Face functions accept `revision`, `config`, `split`, and `path` options where applicable.

## Authentication

Public datasets require no credentials. For private or gated datasets, configure a DuckDB Hugging Face secret:

```sql
CREATE SECRET hf_token (
    TYPE HUGGINGFACE,
    PROVIDER credential_chain
);
```

cache_httpfs delegates Hugging Face authentication to its bundled HTTPFS implementation when resolving `hf://` paths.

## Contributing

Bug reports, feature requests, and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for development,
testing, formatting, and review guidelines.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
