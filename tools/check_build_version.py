"""Exercise version-header generation with CI's CP_VERSION environment values."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BuildVersionTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.repo = Path(temporary.name)
        self.git("init", "--quiet")
        self.git(
            "-c",
            "user.name=Version test",
            "-c",
            "user.email=test@example.invalid",
            "commit",
            "--allow-empty",
            "--quiet",
            "-m",
            "version fixture",
        )
        self.git("tag", "11.0.0-alpha.2")

    def git(self, *args):
        return subprocess.run(
            ["git", "-C", str(self.repo), *args], capture_output=True, text=True, check=True
        ).stdout.strip()

    def generate(self, override):
        env = dict(os.environ, SOURCE_DATE_EPOCH="0")
        env.pop("CP_VERSION", None)
        if override is not None:
            env["CP_VERSION"] = override
        destination = self.repo / "version.h"
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "py/makeversionhdr.py"),
                "--repo-path",
                str(self.repo),
                str(destination),
            ],
            env=env,
            capture_output=True,
            text=True,
        )
        return result, destination.read_text() if destination.exists() else None

    def assert_version(self, override, expected):
        result, header = self.generate(override)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('#define MICROPY_GIT_TAG "' + expected + '"', header)
        self.assertIn(
            '#define MICROPY_GIT_HASH "' + self.git("rev-parse", "--short", "HEAD") + '"', header
        )

    def test_unset_version_uses_git_tag(self):
        self.assert_version(None, "11.0.0-alpha.2")

    def test_empty_ci_version_uses_git_tag(self):
        self.assert_version("", "11.0.0-alpha.2")

    def test_nonempty_version_overrides_git_tag(self):
        self.assert_version("9.9.9-test", "9.9.9-test")

    def test_invalid_nonempty_override_is_not_silently_ignored(self):
        result, header = self.generate("invalid")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cannot determine version", result.stderr)
        self.assertIsNone(header)


if __name__ == "__main__":
    unittest.main()
