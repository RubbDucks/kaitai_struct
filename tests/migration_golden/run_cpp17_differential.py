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
    mode: str
    ksy: Path
    target: str
    parity_criteria: str
    known_deviation: str
    gate: str


class DifferentialFailure(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run Scala-vs-C++17(IR) differential compiler checks")
    parser.add_argument("--fixtures", type=Path, default=DEFAULT_FIXTURES, help="TSV fixture inventory")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT, help="Directory for artifacts and reports")
    parser.add_argument("--max-diff-lines", type=int, default=80, help="Max unified diff lines to include in report snippets")
    parser.add_argument("--informational", action="store_true", help="Always exit 0; report is informational only")
    parser.add_argument(
        "--enforce-gate",
        choices=["none", "required", "all"],
        default="all",
        help=(
            "Which fixture gate to enforce when deciding failure exit code: "
            "none (always pass unless tooling crashes), required (block only required fixtures), all (block all fixtures)."
        ),
    )
    return parser.parse_args()


def parse_fixtures(path: Path) -> list[Fixture]:
    fixtures: list[Fixture] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        row = stripped.split("\t")
        if len(row) not in (7, 8):
            raise DifferentialFailure(f"Invalid fixtures row in {path}: {line}")
        if len(row) == 7:
            fixture_id, category, mode, ksy, target, parity_criteria, known_deviation = row
            gate = "visibility"
        else:
            fixture_id, category, mode, ksy, target, parity_criteria, known_deviation, gate = row

        if gate not in ("required", "visibility"):
            raise DifferentialFailure(f"Invalid fixture gate '{gate}' for fixture {fixture_id} in {path}")
        if mode not in ("success", "error"):
            raise DifferentialFailure(f"Invalid fixture mode '{mode}' for fixture {fixture_id} in {path}")
        fixtures.append(
            Fixture(
                fixture_id=fixture_id,
                category=category,
                mode=mode,
                ksy=REPO_ROOT / ksy,
                target=target,
                parity_criteria=parity_criteria,
                known_deviation=known_deviation,
                gate=gate,
            )
        )
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


def run_logged(cmd: list[str], cwd: Path, stdout_path: Path, stderr_path: Path) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    stdout_path.write_text(proc.stdout, encoding="utf-8")
    stderr_path.write_text(proc.stderr, encoding="utf-8")
    return proc


def aggregate_generated_tree(out_dir: Path, fixture_id: str) -> str:
    chunks = [f"id={fixture_id}", "mode=success"]
    files = sorted(
        [*out_dir.rglob("*.py"), *out_dir.rglob("*.rb"), *out_dir.rglob("*.lua"), *out_dir.rglob("*.h"), *out_dir.rglob("*.cpp")],
        key=lambda p: p.as_posix(),
    )
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


def normalize_diagnostic(stderr_text: str, max_lines: int = 12) -> dict:
    normalized_lines: list[str] = []
    for line in stderr_text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        normalized_lines.append(stripped)
    if not normalized_lines:
        return {"class": "<empty>", "message": "", "lines": []}
    first = normalized_lines[0]
    diag_class, _, diag_message = first.partition(":")
    if not diag_message:
        diag_class = "<unclassified>"
        diag_message = first
    return {
        "class": diag_class.strip(),
        "message": diag_message.strip(),
        "lines": normalized_lines[:max_lines],
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

    scala_cmd = [str(SCALA_BIN), "-t", fixture.target]
    if fixture.target == "cpp_stl":
        scala_cmd.extend(["--cpp-standard", "17"])
    scala_stdout = fixture_dir / "scala.stdout.log"
    scala_stderr = fixture_dir / "scala.stderr.log"

    if fixture.mode == "success":
        ir_path = fixture_dir / f"{fixture.fixture_id}.ksir"
        scala_cmd.extend(["--emit-ir", str(ir_path), "--", "-d", str(scala_out), str(fixture.ksy)])
        run_checked(scala_cmd, cwd=REPO_ROOT, stdout_path=scala_stdout, stderr_path=scala_stderr)

        if fixture.parity_criteria in ("match_scala_vs_cpp17_ir", "known_mismatch_allowed"):

            run_checked(
                [
                    str(KSCXX_BIN),
                    "--from-ir",
                    str(ir_path),
                    "-t",
                    fixture.target,
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
                (fixture_dir / "diff.patch").write_text("\n".join(diff_info["snippet"]) + "\n", encoding="utf-8")
                if fixture.parity_criteria == "known_mismatch_allowed":
                    status = "gap"
                    diff_info["note"] = "Known migration mismatch accepted for this fixture (see known_deviation)."
                else:
                    status = "mismatch"
        elif fixture.parity_criteria == "scala_oracle_only":
            status = "gap"
            diff_info = {
                "line_count": 0,
                "snippet": [],
                "note": "Scala-only oracle check; C++17 backend coverage not available for this target.",
            }
        else:
            raise DifferentialFailure(f"Unknown parity criteria '{fixture.parity_criteria}' for fixture {fixture.fixture_id}")
    else:
        scala_cmd.extend(["--", "-d", str(scala_out), str(fixture.ksy)])
        scala_proc = run_logged(scala_cmd, cwd=REPO_ROOT, stdout_path=scala_stdout, stderr_path=scala_stderr)
        scala_diag = normalize_diagnostic(scala_proc.stderr)
        if scala_proc.returncode == 0:
            raise DifferentialFailure("Expected Scala compiler failure for mode=error fixture, but command succeeded")

        cpp_diag = None
        cpp_proc = None
        if fixture.target == "cpp_stl" and fixture.parity_criteria != "scala_oracle_only":
            cpp_cmd = [
                str(KSCXX_BIN),
                "file",
                "-t",
                fixture.target,
                "--cpp-standard",
                "17",
                "-d",
                str(cpp_out),
                str(fixture.ksy),
            ]
            cpp_proc = run_logged(
                cpp_cmd,
                cwd=REPO_ROOT,
                stdout_path=fixture_dir / "cpp.stdout.log",
                stderr_path=fixture_dir / "cpp.stderr.log",
            )
            cpp_diag = normalize_diagnostic(cpp_proc.stderr)

        if fixture.parity_criteria == "scala_oracle_only":
            status = "match"
            diff_info = {
                "line_count": 0,
                "snippet": [],
                "note": "Error fixture validated against Scala oracle only.",
                "scala": scala_diag,
            }
        elif fixture.parity_criteria in ("match_scala_vs_cpp17_ir", "known_mismatch_allowed"):
            if cpp_proc is None:
                raise DifferentialFailure("C++17 diagnostic parity requested, but C++17 path is not applicable")
            if cpp_proc.returncode == 0:
                raise DifferentialFailure("Expected C++17 compiler failure for mode=error fixture, but command succeeded")

            classes_match = scala_diag["class"] == cpp_diag["class"]
            message_contract_match = scala_diag["message"] == cpp_diag["message"]
            if classes_match and message_contract_match:
                status = "match"
            elif fixture.parity_criteria == "known_mismatch_allowed":
                status = "gap"
            else:
                status = "mismatch"
            diff_info = {
                "line_count": 0,
                "snippet": [],
                "scala": scala_diag,
                "cpp": cpp_diag,
                "contract": {
                    "class_match": classes_match,
                    "message_match": message_contract_match,
                },
            }
            if status == "gap":
                diff_info["note"] = "Known diagnostic mismatch accepted for this fixture (see known_deviation)."
        else:
            raise DifferentialFailure(f"Unknown parity criteria '{fixture.parity_criteria}' for fixture {fixture.fixture_id}")

    return {
        "id": fixture.fixture_id,
        "category": fixture.category,
        "mode": fixture.mode,
        "target": fixture.target,
        "parity_criteria": fixture.parity_criteria,
        "known_deviation": fixture.known_deviation,
        "gate": fixture.gate,
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
        f"required fixtures: {report['summary']['fixtures_required']}",
        f"matches: {report['summary']['matches']}",
        f"mismatches: {report['summary']['mismatches']}",
        f"gaps: {report['summary']['gaps']}",
        f"errors: {report['summary']['errors']}",
        f"required mismatches: {report['summary']['required_mismatches']}",
        f"required errors: {report['summary']['required_errors']}",
        "",
        "per-target:",
    ]
    for target in sorted(report["summary"]["by_target"].keys()):
        t = report["summary"]["by_target"][target]
        lines.append(f"- {target}: pass={t['pass']} fail={t['fail']} gap={t['gap']} error={t['error']}")
    lines.append("")

    for fixture in report["fixtures"]:
        lines.append(
            (
                f"- {fixture['id']} [{fixture['target']}/{fixture['parity_criteria']}] "
                f"gate={fixture['gate']}: {fixture['status']} ({fixture['artifact_dir']})"
            )
        )
        if fixture.get("known_deviation"):
            lines.append(f"  known deviation: {fixture['known_deviation']}")
        if fixture["status"] == "mismatch":
            lines.append(f"  diff lines: {fixture['diff']['line_count']}")
            for line in fixture["diff"]["snippet"][:12]:
                lines.append(f"    {line}")
            if fixture["diff"].get("truncated"):
                lines.append("    ... (truncated)")
        elif fixture["status"] == "gap":
            lines.append(f"  gap rationale: {fixture['diff'].get('note', 'coverage gap')}")
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
        raise DifferentialFailure(f"No fixtures found in {args.fixtures}")

    if args.output_dir.exists():
        shutil.rmtree(args.output_dir)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    report = {
        "schema_version": 1,
        "tool": "run_cpp17_differential.py",
        "fixtures": [],
            "summary": {
                "fixtures_total": len(fixtures),
                "fixtures_required": 0,
                "matches": 0,
                "mismatches": 0,
                "gaps": 0,
                "errors": 0,
                "required_mismatches": 0,
                "required_errors": 0,
                "by_target": {},
            },
    }

    for fixture in fixtures:
        try:
            result = run_fixture(fixture, args.output_dir, args.max_diff_lines)
        except DifferentialFailure as exc:
            result = {
                "id": fixture.fixture_id,
                "category": fixture.category,
                "mode": fixture.mode,
                "target": fixture.target,
                "parity_criteria": fixture.parity_criteria,
                "known_deviation": fixture.known_deviation,
                "gate": fixture.gate,
                "ksy": str(fixture.ksy.relative_to(REPO_ROOT)),
                "status": "error",
                "error": str(exc),
                "artifact_dir": str((args.output_dir / fixture.fixture_id).relative_to(REPO_ROOT)),
            }
        report["fixtures"].append(result)

        target_stats = report["summary"]["by_target"].setdefault(
            result["target"], {"pass": 0, "fail": 0, "gap": 0, "error": 0}
        )
        if result["gate"] == "required":
            report["summary"]["fixtures_required"] += 1
        if result["status"] == "match":
            report["summary"]["matches"] += 1
            target_stats["pass"] += 1
        elif result["status"] == "mismatch":
            report["summary"]["mismatches"] += 1
            target_stats["fail"] += 1
            if result["gate"] == "required":
                report["summary"]["required_mismatches"] += 1
        elif result["status"] == "gap":
            report["summary"]["gaps"] += 1
            target_stats["gap"] += 1
        else:
            report["summary"]["errors"] += 1
            target_stats["error"] += 1
            if result["gate"] == "required":
                report["summary"]["required_errors"] += 1

    json_path = args.output_dir / "report.json"
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    human_path = args.output_dir / "summary.txt"
    write_human_summary(report, human_path)

    print(human_path.read_text(encoding="utf-8"), end="")
    if args.enforce_gate == "required":
        enforce_mismatches = report["summary"]["required_mismatches"]
        enforce_errors = report["summary"]["required_errors"]
    elif args.enforce_gate == "all":
        enforce_mismatches = report["summary"]["mismatches"]
        enforce_errors = report["summary"]["errors"]
    else:
        enforce_mismatches = 0
        enforce_errors = 0

    is_clean = enforce_mismatches == 0 and enforce_errors == 0
    if is_clean or args.informational:
        return 0
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except DifferentialFailure as exc:
        print(f"[migration-golden] {exc}", file=sys.stderr)
        raise SystemExit(1)
