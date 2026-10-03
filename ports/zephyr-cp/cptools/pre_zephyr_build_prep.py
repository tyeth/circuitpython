# Called by the Makefile before calling out to `west`.
import os
import pathlib
import subprocess
import sys

import board_tools

portdir = pathlib.Path(__file__).resolve().parent.parent

board = sys.argv[-1]

_, mpconfigboard = board_tools.load_mpconfigboard(portdir, board)
if not mpconfigboard:
    # Assume it doesn't need any prep.
    sys.exit(0)

blobs = mpconfigboard.get("BLOBS", [])
blob_fetch_args = mpconfigboard.get("blob_fetch_args", {})
for blob in blobs:
    args = blob_fetch_args.get(blob, [])
    subprocess.run(["west", "blobs", "fetch", blob, *args], check=True)

# Frozen modules need the host mpy-cross; build it up front, where make is
# already in use, rather than from inside the CMake-driven CircuitPython step.
if mpconfigboard.get("FROZEN_MPY_DIRS") and "MICROPY_MPYCROSS" not in os.environ:
    subprocess.run(
        ["make", "-C", str(portdir.parent.parent / "mpy-cross"), "USER_C_MODULES="],
        check=True,
    )

if board.endswith("bsim"):
    subprocess.run(
        ["make", "everything", "-j", "8"],
        cwd=portdir / "tools" / "bsim",
        check=True,
    )
