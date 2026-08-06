# Contributing

## Reporting bugs and requesting features

Search the existing GitHub issues before opening a new one. Bug reports should include:

- Operating system, DuckDB version, and extension version or commit
- A minimal SQL reproduction
- Expected and actual behavior
- Relevant errors, logs, or stack traces

Use the feature request template for new API or behavior proposals.

## Pull requests

- Work on a branch or fork; do not push directly to the main branch.
- Keep changes focused and split large changes into reviewable pull requests.
- Add SQL tests for fixes and new behavior whenever possible.
- Explain the problem and solution in the pull request description.
- Link the relevant issue when applicable.
- Run the formatter, build, and tests before requesting review.

## Building

Initialize the dependencies:

```sh
git submodule update --init --recursive
```

Build with parallel compilation:

```sh
CMAKE_BUILD_PARALLEL_LEVEL=<cores> make reldebug
```

## Testing

Run the extension SQL tests:

```sh
make test_reldebug
```

Tests that access Hugging Face should use small public datasets and avoid assumptions about unstable row counts or
remote file ordering.

## Formatting

Run all C++, SQL, and CMake formatters:

```sh
make format-all
```

The DuckDB formatter invokes `clang-format` for C++ files under `src` and `test`.

## C++ guidelines

- Use tabs for indentation and spaces for alignment.
- Keep lines at or below 120 columns.
- Put translation-unit-only declarations in an anonymous namespace.
- Prefer `unique_ptr`; use `shared_ptr` only when ownership is genuinely shared.
- Use `const` whenever possible.
- Do not import namespaces.
- Use fixed-width integer types and DuckDB's `idx_t` for indices, offsets, and counts.
- Prefer references over pointers for required arguments.
- Use range-based loops where practical.
- Use braces for conditionals and loops.
- Use `override` or `final` for overridden virtual methods.
- Replace magic numbers with named `constexpr` values.
- Return early instead of deeply nesting branches.
- Use `StringUtil::Format` when composing SQL strings.
- Do not include commented-out code in pull requests.

Follow the existing naming conventions:

- Files: `lowercase_with_underscores.cpp`
- Types: `CamelCase`
- Functions: `CamelCase`
- Variables: `lowercase_with_underscores`

## Error handling

- Throw exceptions only for errors that must terminate a query.
- Return status values or `NULL` for expected per-item failures, such as an unreachable external blob.
- Use `D_ASSERT` for internal invariants, never for invalid user input.
- Add tests for user-visible errors.

## DuckDB compatibility

DuckDB's internal C++ API is not stable. Keep the DuckDB and extension-ci-tools submodules pinned, and test extension
upgrades on all supported platforms before release.
