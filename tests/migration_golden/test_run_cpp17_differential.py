#!/usr/bin/env python3
import tempfile
import textwrap
import unittest
from pathlib import Path
import sys
from unittest import mock

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
import run_cpp17_differential as diff


class DifferentialFixturesParseTest(unittest.TestCase):
    def test_parse_fixtures_supports_gate_and_mode_columns(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            fixtures_path = Path(td) / "fixtures.tsv"
            fixtures_path.write_text(
                textwrap.dedent(
                    """
                    # id\tcategory\tmode\tksy\ttarget\tparity_criteria\tknown_deviation\tgate
                    req_ok\tprimitives\tsuccess\ttests/formats/default_big_endian.ksy\tcpp_stl\tmatch_scala_vs_cpp17_ir\t\trequired
                    req_err\terrors\terror\ttests/formats_err/type_unknown.ksy\tcpp_stl\tscala_oracle_only\tnote\trequired
                    legacy\tprimitives\tsuccess\ttests/formats/hello_world.ksy\tcpp_stl\tknown_mismatch_allowed\tnote
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )

            fixtures = diff.parse_fixtures(fixtures_path)

        self.assertEqual([f.fixture_id for f in fixtures], ["req_ok", "req_err", "legacy"])
        self.assertEqual(fixtures[0].mode, "success")
        self.assertEqual(fixtures[1].mode, "error")
        self.assertEqual(fixtures[1].gate, "required")
        self.assertEqual(fixtures[2].gate, "visibility")


class DifferentialErrorFixtureTest(unittest.TestCase):
    def test_required_error_fixture_unexpected_scala_success_is_error(self) -> None:
        fixture = diff.Fixture(
            fixture_id="req_err",
            category="errors",
            mode="error",
            ksy=diff.REPO_ROOT / "tests/formats_err/type_unknown.ksy",
            target="cpp_stl",
            parity_criteria="scala_oracle_only",
            known_deviation="",
            gate="required",
        )
        with tempfile.TemporaryDirectory() as td:
            out_root = Path(td)
            success_proc = mock.Mock(returncode=0, stdout="", stderr="")
            with mock.patch.object(diff, "run_logged", return_value=success_proc):
                with self.assertRaises(diff.DifferentialFailure) as ctx:
                    diff.run_fixture(fixture, out_root, 20)

        self.assertIn("Expected Scala compiler failure", str(ctx.exception))


class DifferentialGateEnforcementTest(unittest.TestCase):
    def test_enforce_required_blocks_on_required_error_fixture_mismatch(self) -> None:
        fixture = diff.Fixture(
            fixture_id="req_err",
            category="errors",
            mode="error",
            ksy=diff.REPO_ROOT / "tests/formats_err/type_unknown.ksy",
            target="cpp_stl",
            parity_criteria="match_scala_vs_cpp17_ir",
            known_deviation="",
            gate="required",
        )

        with tempfile.TemporaryDirectory() as td:
            output_dir = Path(td) / "out"
            args = mock.Mock(
                fixtures=Path(td) / "fixtures.tsv",
                output_dir=output_dir,
                max_diff_lines=20,
                informational=False,
                enforce_gate="required",
            )
            args.fixtures.write_text("# stub\n", encoding="utf-8")

            mismatch_result = {
                "id": fixture.fixture_id,
                "category": fixture.category,
                "mode": fixture.mode,
                "target": fixture.target,
                "parity_criteria": fixture.parity_criteria,
                "known_deviation": fixture.known_deviation,
                "gate": fixture.gate,
                "ksy": "tests/formats_err/type_unknown.ksy",
                "status": "mismatch",
                "diff": {"line_count": 0, "snippet": []},
                "artifact_dir": "tests/test_out/migration_differential/req_err",
            }

            with mock.patch.object(diff, "parse_args", return_value=args), mock.patch.object(diff, "ensure_tools"), mock.patch.object(
                diff, "parse_fixtures", return_value=[fixture]
            ), mock.patch.object(diff, "run_fixture", return_value=mismatch_result):
                exit_code = diff.main()

        self.assertEqual(exit_code, 1)


if __name__ == "__main__":
    unittest.main()
