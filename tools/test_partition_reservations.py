"""Offline regression tests for the UF2 filesystem reservations check_partitions.py checks.

    python tools/test_partition_reservations.py
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Reservations(unittest.TestCase):
    def check_config(self, text):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ("tools/check_partitions.py", "scripts/gen_partitions.py"):
                dest = root / name
                dest.parent.mkdir(exist_ok=True)
                shutil.copyfile(ROOT / name, dest)
            (root / "platformio.ini").write_text(text)
            return subprocess.run([sys.executable, str(root / "tools/check_partitions.py")],
                                  capture_output=True, text=True)

    def test_inherited_reservation(self):
        result = self.check_config("[env:base]\nboard_build.filesystem_size = 0.5m\n"
                                   "[env:derived]\nextends = env:base\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("env:derived: filesystem reservation 524288", result.stdout)

    def test_undersized_override(self):
        result = self.check_config("[env:base]\nboard_build.filesystem_size = 0.5m\n"
                                   "[env:derived]\nextends = env:base\n"
                                   "board_build.filesystem_size = 0.25m\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("env:derived: filesystem reservation 0.25m is below", result.stdout)

    def test_invalid_reservations(self):
        for size in ("garbage", "NaNm", "0.5001m"):
            with self.subTest(size=size):
                result = self.check_config("[env:board]\nboard_build.filesystem_size = " + size)
                self.assertNotEqual(result.returncode, 0)

    def test_cycle(self):
        result = self.check_config("[env:a]\nextends = env:b\n[env:b]\nextends = env:a\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("inheritance cycle", result.stdout)

    def test_the_shipped_pico_environments(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/check_partitions.py")],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for env in ("galactic_unicorn", "galactic_unicorn_2w"):
            self.assertIn("env:%s: filesystem reservation 524288" % env, result.stdout)


if __name__ == "__main__":
    unittest.main()
