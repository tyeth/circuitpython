"""Regression checks for commands depending on state outside their path inputs."""
import asyncio
import atexit
import importlib.util
import pathlib
import sys

import pytest


@pytest.fixture
def build_command(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    monkeypatch.delenv("MAKEFLAGS", raising=False)
    path = pathlib.Path(__file__).resolve().parents[1] / "cpbuild.py"
    spec = importlib.util.spec_from_file_location("real_cpbuild_for_test", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    atexit.unregister(module.save_trace)
    script = tmp_path / "generate.py"
    script.write_text(
        "import os, pathlib, sys\n"
        "out=pathlib.Path(sys.argv[1])\n"
        "out.write_text(os.environ['TEST_VERSION'])\n"
        "out.touch()\n"
        "with out.with_name('calls.txt').open('a') as f: f.write('called\\n')\n"
    )
    output = tmp_path / "version.h"

    def run(always=False):
        # Each call represents a fresh build with the persisted timestamps.
        module.ALREADY_RUN.clear()
        asyncio.run(module.run_command(
            [sys.executable, script, output], tmp_path,
            check_hash=[output], always_run=always,
        ))
    return run, output


def test_external_state_refresh_bypasses_only_persistent_cache(build_command, monkeypatch):
    run, output = build_command
    monkeypatch.setenv("TEST_VERSION", "commit-a")
    run()
    monkeypatch.setenv("TEST_VERSION", "commit-b-dirty")
    run()
    assert output.read_text() == "commit-a"
    run(always=True)
    assert output.read_text() == "commit-b-dirty"
    assert output.with_name("calls.txt").read_text().splitlines() == ["called", "called"]


def test_refresh_preserves_unchanged_output_timestamp(build_command, monkeypatch):
    run, output = build_command
    monkeypatch.setenv("TEST_VERSION", "same-commit")
    run(always=True)
    before = output.stat().st_mtime_ns
    run(always=True)
    assert output.stat().st_mtime_ns == before
    assert output.with_name("calls.txt").read_text().splitlines() == ["called", "called"]
