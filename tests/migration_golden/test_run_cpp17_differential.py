#!/usr/bin/env python3
import tempfile
import textwrap
import unittest
from pathlib import Path
import sys

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
import run_cpp17_differential as diff


class DifferentialFixturesParseTest(unittest.TestCase):
    def test_parse_fixtures_supports_gate_column(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            fixtures_path = Path(td) / "fixtures.tsv"
            fixtures_path.write_text(
                textwrap.dedent(
                    """
                    # id\tcategory\tmode\tksy\ttarget\tparity_criteria\tknown_deviation\tgate
                    req\tprimitives\tsuccess\ttests/formats/default_big_endian.ksy\tcpp_stl\tmatch_scala_vs_cpp17_ir\t\trequired
                    vis\tprimitives\tsuccess\ttests/formats/hello_world.ksy\tcpp_stl\tknown_mismatch_allowed\tnote\tvisibility
                    legacy\tprimitives\tsuccess\ttests/formats/hello_world.ksy\tcpp_stl\tknown_mismatch_allowed\tnote
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )

            fixtures = diff.parse_fixtures(fixtures_path)

        self.assertEqual([f.fixture_id for f in fixtures], ["req", "vis", "legacy"])
        self.assertEqual(fixtures[0].gate, "required")
        self.assertEqual(fixtures[1].gate, "visibility")
        self.assertEqual(fixtures[2].gate, "visibility")


if __name__ == "__main__":
    unittest.main()
