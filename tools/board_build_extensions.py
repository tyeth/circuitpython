#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 CircuitPython contributors (https://github.com/adafruit/circuitpython/graphs/contributors)
#
# SPDX-License-Identifier: MIT

"""Print a board's CIRCUITPY_BUILD_EXTENSIONS, space separated.

CI needs these to name the firmware.<ext> make targets explicitly. Most ports
produce them via the default goal, but the zephyr-cp port's default goal is the
Zephyr ELF, so the firmware.* copies have to be asked for by name. This mirrors
how tools/build_release_files.py resolves the same setting.
"""

import sys
import tomllib
from pathlib import Path

TOP = Path(__file__).parent.parent
sys.path.append(str(TOP / "docs"))

from shared_bindings_matrix import get_board_mapping  # noqa: E402


def main(board: str) -> int:
    board_mapping = get_board_mapping()
    if board not in board_mapping:
        raise ValueError(f"Unknown board {board!r}")
    port = board_mapping[board]["port"]

    if port == "zephyr-cp":
        # Board ids are vendor_board and the vendor may itself contain an
        # underscore, so walk the separators until a circuitpython.toml exists.
        next_underscore = board.find("_")
        cp_toml = None
        while next_underscore != -1:
            vendor = board[:next_underscore]
            target = board[next_underscore + 1 :]
            candidate = TOP / f"ports/zephyr-cp/boards/{vendor}/{target}/circuitpython.toml"
            if candidate.exists():
                cp_toml = candidate
                break
            next_underscore = board.find("_", next_underscore + 1)
        if cp_toml is None:
            raise ValueError(f"No circuitpython.toml found for zephyr-cp board {board!r}")
        with cp_toml.open("rb") as f:
            extensions = tomllib.load(f)["CIRCUITPY_BUILD_EXTENSIONS"]
    else:
        # Imported lazily: build_board_info pulls in adabot, which zephyr-cp
        # boards do not need and which is not installed everywhere.
        sys.path.append(str(TOP / "tools"))
        from build_board_info import get_settings_from_makefile

        settings = get_settings_from_makefile(str(TOP / "ports" / port), board)
        extensions = [e.strip() for e in settings["CIRCUITPY_BUILD_EXTENSIONS"].split(",")]

    print(" ".join(extensions))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1]))
