#!/usr/bin/env python3
import argparse
import json
import shutil
import statistics
import subprocess
import sys
from dataclasses import dataclass
import textwrap
from datetime import datetime, timezone
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
DEFAULT_FIXTURES = SCRIPT_DIR / "benchmark_fixtures.tsv"
DEFAULT_OUTPUT = REPO_ROOT / "tests" / "test_out" / "migration_benchmarks"
SCALA_BIN = REPO_ROOT / "compiler" / "jvm" / "target" / "universal" / "stage" / "bin" / "kaitai-struct-compiler"
KSCXX_BIN = REPO_ROOT / "compiler-cpp" / "build" / "kscpp"
TIME_BIN = Path("/usr/bin/time")


@dataclass
class Fixture:
    fixture_id: str
    category: str
    ksy: Path | None
    target: str
    notes: str
    inline_template: str | None = None


class BenchmarkFailure(RuntimeError):
    pass



INLINE_TEMPLATES = {
    "expr_subset_a": textwrap.dedent("""
        meta:
          id: expr_subset_a
          endian: le
        seq:
          - id: a
            type: u1
          - id: b
            type: u1
        instances:
          lit:
            value: 7
          arith:
            value: a + b * 3 - 2
          logic:
            value: (a > b) and (lit == 7)
          ref_mix:
            value: lit + a
    """).strip() + "\n",
    "type_subset": textwrap.dedent("""
        meta:
          id: type_subset
          endian: le
        seq:
          - id: u
            type: u4
          - id: s
            type: s2
          - id: f
            type: f4
          - id: bytes_fixed
            size: 4
          - id: txt
            type: str
            size: 3
            encoding: UTF-8
    """).strip() + "\n",
}

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run Scala-vs-C++17 migration benchmark harness")
    parser.add_argument("--fixtures", type=Path, default=DEFAULT_FIXTURES, help="TSV fixture inventory")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT, help="Directory for benchmark artifacts")
    parser.add_argument("--iterations", type=int, default=5, help="Measured iterations per fixture/path")
    parser.add_argument("--warmup", type=int, default=1, help="Warmup iterations per fixture/path")
    parser.add_argument("--latency-ratio-max", type=float, default=2.0, help="Max allowed median cpp/scalar latency ratio")
    parser.add_argument("--memory-ratio-max", type=float, default=2.0, help="Max allowed median cpp/scalar rss ratio")
    parser.add_argument("--stability-cv-max", type=float, default=0.20, help="Max allowed coefficient of variation")
    parser.add_argument("--informational", action="store_true", help="Always return success, report only")
    parser.add_argument("--check-schema", type=Path, help="Validate an existing report.json schema and exit")
    return parser.parse_args()


def parse_fixtures(path: Path) -> list[Fixture]:
    fixtures: list[Fixture] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        row = stripped.split("\t")
        if len(row) != 6:
            raise BenchmarkFailure(f"Invalid fixtures row in {path}: {line}")
        fixture_id, category, mode, ksy, target, notes = row
        if mode != "success":
            continue
        if ksy.startswith("inline:"):
            tpl = ksy.split(":", 1)[1]
            if tpl not in INLINE_TEMPLATES:
                raise BenchmarkFailure(f"Unknown inline template: {tpl}")
            fixtures.append(Fixture(fixture_id=fixture_id, category=category, ksy=None, target=target, notes=notes, inline_template=tpl))
        else:
            fixtures.append(Fixture(fixture_id=fixture_id, category=category, ksy=REPO_ROOT / ksy, target=target, notes=notes))
    return fixtures


def ensure_tools() -> None:
    if not SCALA_BIN.exists():
        raise BenchmarkFailure("Scala stage compiler missing; run tests/build-compiler first")
    if not KSCXX_BIN.exists():
        raise BenchmarkFailure("C++ compiler missing; run cmake -S compiler-cpp -B compiler-cpp/build && cmake --build compiler-cpp/build")
    if not TIME_BIN.exists():
        raise BenchmarkFailure("/usr/bin/time is required for memory metrics")


def _safe_cv(values: list[float]) -> float:
    if len(values) < 2:
        return 0.0
    mean_val = statistics.mean(values)
    if mean_val == 0:
        return 0.0
    return statistics.stdev(values) / mean_val


def summarize_runs(runs: list[dict]) -> dict:
    latencies = [float(r["elapsed_sec"]) for r in runs]
    rss_values = [int(r["max_rss_kb"]) for r in runs]
    return {
        "iterations": len(runs),
        "latency_sec": {
            "median": statistics.median(latencies),
            "mean": statistics.mean(latencies),
            "min": min(latencies),
            "max": max(latencies),
            "cv": _safe_cv(latencies),
        },
        "max_rss_kb": {
            "median": int(statistics.median(rss_values)),
            "mean": statistics.mean(rss_values),
            "min": min(rss_values),
            "max": max(rss_values),
            "cv": _safe_cv([float(v) for v in rss_values]),
        },
    }


def run_with_time(cmd: list[str], cwd: Path, metric_file: Path, stdout_log: Path, stderr_log: Path) -> dict:
    timed_cmd = [str(TIME_BIN), "-f", '{"elapsed_sec": %e, "user_sec": %U, "sys_sec": %S, "max_rss_kb": %M}', "-o", str(metric_file)]
    timed_cmd.extend(cmd)
    with stdout_log.open("w", encoding="utf-8") as stdout_f, stderr_log.open("w", encoding="utf-8") as stderr_f:
        proc = subprocess.run(timed_cmd, cwd=cwd, stdout=stdout_f, stderr=stderr_f, text=True)
    if proc.returncode != 0:
        raise BenchmarkFailure(f"Command failed ({proc.returncode}): {' '.join(cmd)}")
    return json.loads(metric_file.read_text(encoding="utf-8"))


def materialize_fixture_ksy(fixture: Fixture, fixture_dir: Path) -> Path:
    if fixture.inline_template:
        ksy_path = fixture_dir / f"{fixture.fixture_id}.ksy"
        ksy_path.write_text(INLINE_TEMPLATES[fixture.inline_template], encoding="utf-8")
        return ksy_path
    assert fixture.ksy is not None
    return fixture.ksy


def build_fixture_ir(fixture: Fixture, fixture_dir: Path, ksy_path: Path) -> Path:
    ir_path = fixture_dir / f"{fixture.fixture_id}.ksir"
    ir_out_dir = fixture_dir / "ir_scala_out"
    ir_out_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(SCALA_BIN),
        "-t",
        fixture.target,
        "--cpp-standard",
        "17",
        "--emit-ir",
        str(ir_path),
        "--",
        "-d",
        str(ir_out_dir),
        str(ksy_path),
    ]
    subprocess.run(cmd, cwd=REPO_ROOT, check=True, capture_output=True, text=True)
    return ir_path


def benchmark_fixture(fixture: Fixture, out_root: Path, iterations: int, warmup: int) -> dict:
    fixture_dir = out_root / fixture.fixture_id
    fixture_dir.mkdir(parents=True, exist_ok=True)
    ksy_path = materialize_fixture_ksy(fixture, fixture_dir)
    ir_path = build_fixture_ir(fixture, fixture_dir, ksy_path)

    def mk_scala_cmd(iter_no: int) -> tuple[list[str], Path]:
        out_dir = fixture_dir / "scala" / f"iter_{iter_no}"
        out_dir.mkdir(parents=True, exist_ok=True)
        cmd = [str(SCALA_BIN), "-t", fixture.target, "--cpp-standard", "17", "--", "-d", str(out_dir), str(ksy_path)]
        return cmd, out_dir

    def mk_cpp_cmd(iter_no: int) -> tuple[list[str], Path]:
        out_dir = fixture_dir / "cpp_from_ir" / f"iter_{iter_no}"
        out_dir.mkdir(parents=True, exist_ok=True)
        cmd = [str(KSCXX_BIN), "--from-ir", str(ir_path), "-t", fixture.target, "--cpp-standard", "17", "-d", str(out_dir)]
        return cmd, out_dir

    results: dict[str, list[dict]] = {"scala_full": [], "cpp_from_ir": []}

    for path_name, cmd_factory in (("scala_full", mk_scala_cmd), ("cpp_from_ir", mk_cpp_cmd)):
        for i in range(warmup + iterations):
            cmd, _ = cmd_factory(i)
            metric_file = fixture_dir / f"{path_name}.iter_{i}.metrics.json"
            stdout_log = fixture_dir / f"{path_name}.iter_{i}.stdout.log"
            stderr_log = fixture_dir / f"{path_name}.iter_{i}.stderr.log"
            metric = run_with_time(cmd, REPO_ROOT, metric_file, stdout_log, stderr_log)
            metric["iteration"] = i
            metric["phase"] = "warmup" if i < warmup else "measured"
            if i >= warmup:
                results[path_name].append(metric)

    scala_summary = summarize_runs(results["scala_full"])
    cpp_summary = summarize_runs(results["cpp_from_ir"])
    ratios = {
        "latency_median_ratio_cpp_over_scala": cpp_summary["latency_sec"]["median"] / scala_summary["latency_sec"]["median"],
        "max_rss_median_ratio_cpp_over_scala": cpp_summary["max_rss_kb"]["median"] / scala_summary["max_rss_kb"]["median"],
    }

    return {
        "id": fixture.fixture_id,
        "category": fixture.category,
        "target": fixture.target,
        "ksy": str(ksy_path.relative_to(REPO_ROOT) if ksy_path.is_relative_to(REPO_ROOT) else ksy_path),
        "notes": fixture.notes,
        "paths": {
            "scala_full": {"runs": results["scala_full"], "summary": scala_summary},
            "cpp_from_ir": {"runs": results["cpp_from_ir"], "summary": cpp_summary},
        },
        "ratios": ratios,
        "artifact_dir": str(fixture_dir.relative_to(REPO_ROOT)),
    }


def validate_report_schema(report: dict) -> list[str]:
    errors: list[str] = []
    if report.get("schema_version") != 1:
        errors.append("schema_version must be 1")
    for key in ("tool", "generated_at_utc", "fixtures", "summary", "thresholds"):
        if key not in report:
            errors.append(f"missing top-level key: {key}")
    if not isinstance(report.get("fixtures", []), list):
        errors.append("fixtures must be a list")
        return errors
    for fixture in report.get("fixtures", []):
        if "paths" not in fixture:
            errors.append(f"fixture missing paths: {fixture.get('id', '<unknown>')}")
            continue
        for path_name in ("scala_full", "cpp_from_ir"):
            path_data = fixture["paths"].get(path_name)
            if not path_data:
                errors.append(f"fixture {fixture.get('id')} missing {path_name}")
                continue
            if "runs" not in path_data or "summary" not in path_data:
                errors.append(f"fixture {fixture.get('id')} path {path_name} missing runs/summary")
    return errors


def write_summary(report: dict, out_path: Path) -> None:
    lines = [
        "# C++17 migration benchmark report",
        "",
        f"fixtures: {report['summary']['fixtures_total']}",
        f"threshold breaches: {report['summary']['threshold_breaches']}",
        f"status: {report['summary']['status']}",
        "",
    ]
    for fixture in report["fixtures"]:
        lines.extend(
            [
                f"- {fixture['id']} ({fixture['category']}):",
                f"  latency ratio cpp/scalar median: {fixture['ratios']['latency_median_ratio_cpp_over_scala']:.3f}",
                f"  memory ratio cpp/scalar median: {fixture['ratios']['max_rss_median_ratio_cpp_over_scala']:.3f}",
                f"  artifact_dir: {fixture['artifact_dir']}",
            ]
        )
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    if args.check_schema:
        report = json.loads(args.check_schema.read_text(encoding="utf-8"))
        errors = validate_report_schema(report)
        if errors:
            for error in errors:
                print(f"[migration-bench] {error}", file=sys.stderr)
            return 1
        print(f"[migration-bench] schema ok: {args.check_schema}")
        return 0

    ensure_tools()
    fixtures = parse_fixtures(args.fixtures.resolve())
    if not fixtures:
        raise BenchmarkFailure(f"No fixtures found in {args.fixtures}")

    out_dir = args.output_dir.resolve()
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    report = {
        "schema_version": 1,
        "tool": "run_cpp17_benchmarks.py",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "thresholds": {
            "latency_ratio_max": args.latency_ratio_max,
            "memory_ratio_max": args.memory_ratio_max,
            "stability_cv_max": args.stability_cv_max,
        },
        "fixtures": [],
        "summary": {
            "fixtures_total": len(fixtures),
            "threshold_breaches": 0,
            "status": "pass",
        },
    }

    for fixture in fixtures:
        fixture_report = benchmark_fixture(fixture, out_dir, args.iterations, args.warmup)
        breaches: list[str] = []
        if fixture_report["ratios"]["latency_median_ratio_cpp_over_scala"] > args.latency_ratio_max:
            breaches.append("latency_ratio")
        if fixture_report["ratios"]["max_rss_median_ratio_cpp_over_scala"] > args.memory_ratio_max:
            breaches.append("memory_ratio")
        for path_name in ("scala_full", "cpp_from_ir"):
            lat_cv = fixture_report["paths"][path_name]["summary"]["latency_sec"]["cv"]
            if lat_cv > args.stability_cv_max:
                breaches.append(f"stability_cv:{path_name}")
        fixture_report["threshold_breaches"] = breaches
        if breaches:
            report["summary"]["threshold_breaches"] += 1
        report["fixtures"].append(fixture_report)

    if report["summary"]["threshold_breaches"]:
        report["summary"]["status"] = "warn"

    schema_errors = validate_report_schema(report)
    if schema_errors:
        raise BenchmarkFailure("; ".join(schema_errors))

    report_json = out_dir / "report.json"
    report_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    summary_txt = out_dir / "summary.txt"
    write_summary(report, summary_txt)

    print(summary_txt.read_text(encoding="utf-8"), end="")
    if report["summary"]["status"] == "pass" or args.informational:
        return 0
    return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BenchmarkFailure as exc:
        print(f"[migration-bench] {exc}", file=sys.stderr)
        raise SystemExit(1)
