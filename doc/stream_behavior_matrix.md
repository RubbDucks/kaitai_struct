# Cross-runtime stream / EOF behavior matrix (cpp_stl, lua, python, ruby)

This document summarizes runtime stream semantics for EOF and short-read edge cases,
with a focus on parity between `cpp_stl`, `lua`, `python`, and `ruby`.

## Matrix

| Behavior | cpp_stl | lua | python | ruby |
|---|---|---|---|---|
| `read_bytes(n)` aligns to byte boundary before read | Yes | Yes | Yes | Yes |
| Short read raises EOF-style exception | `std::istream::failure` | Lua error (`requested N bytes, but only M bytes available`) | `EndOfStreamError` (`EOFError`) | `EOFError` |
| On short read, seekable streams are rewound to position before failed read | Yes | Yes | Yes | Yes |
| On short read, non-seekable streams may advance and cannot be rewound | Yes (cannot restore without random access) | Yes (if underlying stream cannot seek) | Yes | Yes |
| Bit read followed by byte read starts at next byte boundary | Yes (`align_to_byte`) | Yes (`align_to_byte`) | Yes (`align_to_byte`) | Yes (`align_to_byte`) |
| `is_eof` while buffered bit remainder exists | `false` | `false` | `false` | `false` |

## Notes on intentional differences

- Exception *types and wording* are language-idiomatic by design.
- Rewind behavior on failed reads is best-effort and requires a seekable stream. For non-seekable streams, all runtimes preserve the failure but cannot reliably restore previous position.

## Regression tests added

- `tests/spec/cpp_stl_17/test_runtime_stream_eof_semantics.cpp`
- `tests/spec/lua/test_runtime_stream_eof_semantics.lua`
- `tests/spec/python/spec/test_runtime_stream_eof_semantics.py`
- `tests/spec/ruby/runtime_stream_eof_semantics_spec.rb`

These tests validate:

1. short-read failure rewinds on seekable streams,
2. bit-to-byte alignment correctness,
3. non-seekable short-read handling (Python).
