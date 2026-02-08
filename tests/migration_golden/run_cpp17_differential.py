#!/usr/bin/env python3
import argparse
import difflib
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
DEFAULT_FIXTURES = SCRIPT_DIR / "cpp17_differential_fixtures.tsv"
DEFAULT_OUTPUT = REPO_ROOT / "tests" / "test_out" / "migration_differential"
SCALA_BIN = REPO_ROOT / "compiler" / "jvm" / "target" / "universal" / "stage" / "bin" / "kaitai-struct-compiler"
KSCXX_BIN = REPO_ROOT / "compiler-cpp" / "build" / "kscpp"
NORMALIZER = SCRIPT_DIR / "normalize_compiler_output.py"


@dataclass
class Fixture:
    fixture_id: str
    category: str
    ksy: Path


class DifferentialFailure(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run Scala-vs-C++17(IR) differential compiler checks")
    parser.add_argument("--fixtures", type=Path, default=DEFAULT_FIXTURES, help="TSV fixture inventory")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT, help="Directory for artifacts and reports")
    parser.add_argument("--max-diff-lines", type=int, default=80, help="Max unified diff lines to include in report snippets")
    parser.add_argument("--informational", action="store_true", help="Always exit 0; report is informational only")
    return parser.parse_args()


def parse_fixtures(path: Path) -> list[Fixture]:
    fixtures: list[Fixture] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        row = stripped.split("\t")
        if len(row) != 5:
            raise DifferentialFailure(f"Invalid fixtures row in {path}: {line}")
        fixture_id, category, mode, ksy, _compiler_target = row
        if mode != "success":
            continue
        fixtures.append(Fixture(fixture_id=fixture_id, category=category, ksy=REPO_ROOT / ksy))
    return fixtures


def run_checked(cmd: list[str], cwd: Path, stdout_path: Path, stderr_path: Path) -> None:
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    stdout_path.write_text(proc.stdout, encoding="utf-8")
    stderr_path.write_text(proc.stderr, encoding="utf-8")
    if proc.returncode != 0:
        raise DifferentialFailure(
            f"Command failed ({proc.returncode}): {' '.join(cmd)}\n"
            f"See logs: {stdout_path} and {stderr_path}"
        )


def aggregate_generated_tree(out_dir: Path, fixture_id: str) -> str:
    chunks = [f"id={fixture_id}", "mode=success"]
    files = sorted([*out_dir.rglob("*.h"), *out_dir.rglob("*.cpp")], key=lambda p: p.as_posix())
    for file_path in files:
        rel = file_path.relative_to(out_dir)
        chunks.append(f"--- FILE:{rel.as_posix()}")
        chunks.append(file_path.read_text(encoding="utf-8"))
    return "\n".join(chunks) + "\n"


def normalize(raw_file: Path, out_file: Path) -> None:
    subprocess.run([sys.executable, str(NORMALIZER), str(raw_file), str(out_file)], check=True)


def summarize_diff(scala_text: str, cpp_text: str, max_lines: int) -> tuple[bool, dict]:
    if scala_text == cpp_text:
        return True, {"line_count": 0, "snippet": []}
    diff_lines = list(
        difflib.unified_diff(
            scala_text.splitlines(),
            cpp_text.splitlines(),
            fromfile="scala.norm",
            tofile="cpp.norm",
            lineterm="",
        )
    )
    snippet = diff_lines[:max_lines]
    return False, {
        "line_count": len(diff_lines),
        "snippet": snippet,
        "truncated": len(diff_lines) > max_lines,
    }


def ensure_tools() -> None:
    if not SCALA_BIN.exists():
        raise DifferentialFailure("Scala stage compiler missing; run tests/build-compiler first")
    if not KSCXX_BIN.exists():
        raise DifferentialFailure("C++ compiler missing; run cmake -S compiler-cpp -B compiler-cpp/build && cmake --build compiler-cpp/build")


def run_fixture(fixture: Fixture, out_root: Path, max_diff_lines: int) -> dict:
    fixture_dir = out_root / fixture.fixture_id
    scala_out = fixture_dir / "scala_out"
    cpp_out = fixture_dir / "cpp_out"
    scala_out.mkdir(parents=True, exist_ok=True)
    cpp_out.mkdir(parents=True, exist_ok=True)

    ir_path = fixture_dir / f"{fixture.fixture_id}.ksir"

    run_checked(
        [
            str(SCALA_BIN),
            "-t",
            "cpp_stl",
            "--cpp-standard",
            "17",
            "--emit-ir",
            str(ir_path),
            "--",
            "-d",
            str(scala_out),
            str(fixture.ksy),
        ],
        cwd=REPO_ROOT,
        stdout_path=fixture_dir / "scala.stdout.log",
        stderr_path=fixture_dir / "scala.stderr.log",
    )
    run_checked(
        [
            str(KSCXX_BIN),
            "--from-ir",
            str(ir_path),
            "-t",
            "cpp_stl",
            "--cpp-standard",
            "17",
            "-d",
            str(cpp_out),
        ],
        cwd=REPO_ROOT,
        stdout_path=fixture_dir / "cpp.stdout.log",
        stderr_path=fixture_dir / "cpp.stderr.log",
    )

    scala_raw = fixture_dir / "scala.raw"
    cpp_raw = fixture_dir / "cpp.raw"
    scala_raw.write_text(aggregate_generated_tree(scala_out, fixture.fixture_id), encoding="utf-8")
    cpp_raw.write_text(aggregate_generated_tree(cpp_out, fixture.fixture_id), encoding="utf-8")

    scala_norm = fixture_dir / "scala.norm"
    cpp_norm = fixture_dir / "cpp.norm"
    normalize(scala_raw, scala_norm)
    normalize(cpp_raw, cpp_norm)

    scala_text = scala_norm.read_text(encoding="utf-8")
    cpp_text = cpp_norm.read_text(encoding="utf-8")
    matched, diff_info = summarize_diff(scala_text, cpp_text, max_diff_lines)

    if matched:
        status = "match"
    else:
        status = "mismatch"
        (fixture_dir / "diff.patch").write_text("\n".join(diff_info["snippet"]) + "\n", encoding="utf-8")

    return {
        "id": fixture.fixture_id,
        "category": fixture.category,
        "ksy": str(fixture.ksy.relative_to(REPO_ROOT)),
        "status": status,
        "diff": diff_info,
        "artifact_dir": str(fixture_dir.relative_to(REPO_ROOT)),
    }


def write_human_summary(report: dict, summary_path: Path) -> None:
    lines = [
        "# C++17 migration differential report",
        "",
        f"fixtures: {report['summary']['fixtures_total']}",
        f"matches: {report['summary']['matches']}",
        f"mismatches: {report['summary']['mismatches']}",
        f"errors: {report['summary']['errors']}",
        "",
    ]
    for fixture in report["fixtures"]:
        lines.append(f"- {fixture['id']}: {fixture['status']} ({fixture['artifact_dir']})")
        if fixture["status"] == "mismatch":
            lines.append(f"  diff lines: {fixture['diff']['line_count']}")
            for line in fixture["diff"]["snippet"][:12]:
                lines.append(f"    {line}")
            if fixture["diff"].get("truncated"):
                lines.append("    ... (truncated)")
        elif fixture["status"] == "error":
            lines.append(f"  error: {fixture['error']}")
    summary_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    args.fixtures = args.fixtures.resolve()
    args.output_dir = args.output_dir.resolve()
    ensure_tools()

    fixtures = parse_fixtures(args.fixtures)
    if not fixtures:
        raise DifferentialFailure(f"No success fixtures found in {args.fixtures}")

    if args.output_dir.exists():
        shutil.rmtree(args.output_dir)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    report = {
        "schema_version": 1,
        "tool": "run_cpp17_differential.py",
        "fixtures": [],
        "summary": {
            "fixtures_total": len(fixtures),
            "matches": 0,
            "mismatches": 0,
            "errors": 0,
        },
    }

    for fixture in fixtures:
        try:
            result = run_fixture(fixture, args.output_dir, args.max_diff_lines)
        except DifferentialFailure as exc:
            result = {
                "id": fixture.fixture_id,
                "category": fixture.category,
                "ksy": str(fixture.ksy.relative_to(REPO_ROOT)),
                "status": "error",
                "error": str(exc),
                "artifact_dir": str((args.output_dir / fixture.fixture_id).relative_to(REPO_ROOT)),
            }
        report["fixtures"].append(result)
        if result["status"] == "match":
            report["summary"]["matches"] += 1
        elif result["status"] == "mismatch":
            report["summary"]["mismatches"] += 1
        else:
            report["summary"]["errors"] += 1

    json_path = args.output_dir / "report.json"
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    human_path = args.output_dir / "summary.txt"
    write_human_summary(report, human_path)

    print(human_path.read_text(encoding="utf-8"), end="")
    is_clean = report["summary"]["mismatches"] == 0 and report["summary"]["errors"] == 0
    if is_clean or args.informational:
        return 0
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except DifferentialFailure as exc:
        print(f"[migration-golden] {exc}", file=sys.stderr)
        raise SystemExit(1)
