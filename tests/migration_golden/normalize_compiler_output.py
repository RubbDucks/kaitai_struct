#!/usr/bin/env python3
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def normalize_text(text: str) -> str:
    text = text.replace('\r\n', '\n').replace('\r', '\n')
    root = re.escape(str(ROOT))
    text = re.sub(root, '<REPO_ROOT>', text)
    text = re.sub(r"\b\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:?\d{2})?\b", '<TIMESTAMP>', text)
    text = re.sub(r"(?m)^\s*#\s*This is a generated file!.*$", "# <GENERATED_BANNER>", text)
    text = re.sub(r"(?m)^\s*//\s*This is a generated file!.*$", "// <GENERATED_BANNER>", text)
    text = re.sub(r"(?m)^\s*/\*\s*This is a generated file!.*$", "/* <GENERATED_BANNER>", text)
    text = re.sub(r"(?m)^\s*#include\s+<kaitai/exceptions\.h>\s*$\n?", "", text)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip() + "\n"


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: normalize_compiler_output.py <input> <output>", file=sys.stderr)
        return 2
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(normalize_text(src.read_text(encoding='utf-8')), encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
