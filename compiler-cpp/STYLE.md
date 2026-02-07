# C++17 style and linting (experimental compiler path)

This directory uses `clang-format` for baseline style consistency.

## Formatting

- Style source: `compiler-cpp/.clang-format`.
- Required standard: C++17.
- Run formatting before commit:

```sh
clang-format -i src/*.cpp
```

## Conventions

- Prefer standard library facilities over custom helpers when equivalent.
- Keep implementation portable and avoid compiler-specific extensions.
- Keep this path experimental and isolated from Scala compiler defaults.
