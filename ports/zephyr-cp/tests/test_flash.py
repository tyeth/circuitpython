# SPDX-FileCopyrightText: 2025 Scott Shawcroft for Adafruit Industries
# SPDX-License-Identifier: MIT

"""Test flash filesystem with various erase block sizes."""

import subprocess

import pytest

from .conftest import (
    DEFAULT_ERASE_PAGE_SIZE,
    LITTLEFS_FILESYSTEM_BOARDS,
    read_lfs_file_from_flash,
)


def read_file_from_flash(flash_file, path, board, erase_page_size=DEFAULT_ERASE_PAGE_SIZE):
    """Extract a file from the flash image using the board's filesystem tool."""
    if board in LITTLEFS_FILESYSTEM_BOARDS:
        return read_lfs_file_from_flash(flash_file, path, board, erase_page_size)
    result = subprocess.run(
        ["mcopy", "-i", str(flash_file), f"::{path}", "-"],
        capture_output=True,
    )
    if result.returncode != 0:
        raise FileNotFoundError(
            f"Failed to read ::{path} from {flash_file}: {result.stderr.decode()}"
        )
    return result.stdout.decode()


WRITE_READ_CODE = """\
import storage
storage.remount("/", readonly=False)
with open("/test.txt", "w") as f:
    f.write("hello flash")
with open("/test.txt", "r") as f:
    content = f.read()
print(f"content: {content}")
print("done")
"""


@pytest.mark.circuitpy_drive({"code.py": WRITE_READ_CODE})
def test_flash_default_erase_size(circuitpython, board):
    """Test filesystem write/read with default 4KB erase blocks."""
    circuitpython.wait_until_done()
    output = circuitpython.serial.all_output
    assert "content: hello flash" in output
    assert "done" in output

    content = read_file_from_flash(circuitpython.flash_file, "test.txt", board)
    assert content == "hello flash"


@pytest.mark.circuitpy_drive({"code.py": WRITE_READ_CODE})
@pytest.mark.flash_config(erase_block_size=65536)
def test_flash_64k_erase_blocks(circuitpython, board):
    """Test filesystem write/read with 64KB erase blocks (128 blocks per page)."""
    circuitpython.wait_until_done()
    output = circuitpython.serial.all_output
    assert "content: hello flash" in output
    assert "done" in output

    content = read_file_from_flash(
        circuitpython.flash_file, "test.txt", board, erase_page_size=65536
    )
    assert content == "hello flash"


@pytest.mark.circuitpy_drive({"code.py": WRITE_READ_CODE})
@pytest.mark.flash_config(erase_block_size=262144, total_size=4 * 1024 * 1024)
def test_flash_256k_erase_blocks(circuitpython, board):
    """Test filesystem write/read with 256KB erase blocks (like RA8D1 OSPI)."""
    circuitpython.wait_until_done()
    output = circuitpython.serial.all_output
    assert "content: hello flash" in output
    assert "done" in output

    content = read_file_from_flash(
        circuitpython.flash_file, "test.txt", board, erase_page_size=262144
    )
    assert content == "hello flash"


MULTI_FILE_CODE = """\
import storage
storage.remount("/", readonly=False)
for i in range(5):
    name = f"/file{i}.txt"
    with open(name, "w") as f:
        f.write(f"data{i}" * 100)
print("multi_file_ok")
print("done")
"""


@pytest.mark.circuitpy_drive({"code.py": MULTI_FILE_CODE})
@pytest.mark.flash_config(erase_block_size=262144, total_size=4 * 1024 * 1024)
def test_flash_256k_multi_file(circuitpython, board):
    """Test multiple file writes with 256KB erase blocks to exercise cache flushing."""
    circuitpython.wait_until_done()
    output = circuitpython.serial.all_output
    assert "multi_file_ok" in output

    for i in range(5):
        content = read_file_from_flash(
            circuitpython.flash_file,
            f"file{i}.txt",
            board,
            erase_page_size=262144,
        )
        assert content == f"data{i}" * 100, f"file{i}.txt mismatch"


EXISTING_DATA_CODE = """\
import storage
storage.remount("/", readonly=False)

# Write a file to populate the erase page.
original_data = "A" * 4000
with open("/original.txt", "w") as f:
    f.write(original_data)

# Force a flush so the data is written to flash.
storage.remount("/", readonly=True)
storage.remount("/", readonly=False)

# Now write a small new file. This updates filesystem metadata and
# directory entries that share an erase page with original.txt,
# exercising the read-modify-write cycle on the cached page.
with open("/small.txt", "w") as f:
    f.write("tiny")

# Read back original to check it survived.
with open("/original.txt", "r") as f:
    readback = f.read()
if readback == original_data:
    print("existing_data_ok")
else:
    print(f"MISMATCH: got {len(readback)} bytes")
print("done")
"""


@pytest.mark.circuitpy_drive({"code.py": EXISTING_DATA_CODE})
@pytest.mark.flash_config(erase_block_size=262144, total_size=4 * 1024 * 1024)
def test_flash_256k_existing_data_survives(circuitpython, board):
    """Test that existing data in an erase page survives when new data is written.

    With 256KB erase blocks (512 blocks per page), writing to any block in
    the page triggers an erase-rewrite of the entire page. Existing blocks
    must be preserved through the read-modify-write cycle.
    """
    circuitpython.wait_until_done()
    output = circuitpython.serial.all_output
    assert "existing_data_ok" in output

    # Verify both files survived on the actual flash image.
    original = read_file_from_flash(
        circuitpython.flash_file, "original.txt", board, erase_page_size=262144
    )
    assert original == "A" * 4000, f"original.txt corrupted: got {len(original)} bytes"

    small = read_file_from_flash(
        circuitpython.flash_file, "small.txt", board, erase_page_size=262144
    )
    assert small == "tiny"


OVERWRITE_CODE = """\
import storage
storage.remount("/", readonly=False)
with open("/overwrite.txt", "w") as f:
    f.write("first version")
with open("/overwrite.txt", "w") as f:
    f.write("second version")
print("overwrite_ok")
print("done")
"""


@pytest.mark.circuitpy_drive({"code.py": OVERWRITE_CODE})
@pytest.mark.flash_config(erase_block_size=262144, total_size=4 * 1024 * 1024)
def test_flash_256k_overwrite(circuitpython, board):
    """Test overwriting a file with 256KB erase blocks to exercise erase-rewrite cycle."""
    circuitpython.wait_until_done()
    output = circuitpython.serial.all_output
    assert "overwrite_ok" in output

    content = read_file_from_flash(
        circuitpython.flash_file, "overwrite.txt", board, erase_page_size=262144
    )
    assert content == "second version"


SCRATCH_PAGE_CODE = """\
import gc
import storage
storage.remount("/", readonly=False)

# Open the files and build their contents first: nothing that allocates can
# run once the heap is full.
names = ("/scratch0.txt", "/scratch1.txt", "/scratch2.txt", "/scratch3.txt")
chunks = (b"A" * 300, b"B" * 300, b"C" * 300, b"D" * 300)
files = [open(name, "wb") for name in names]

# Fill the heap so flash.c can't allocate its RAM cache and stages writes in
# the scratch page instead.
filler = []
for size in (65536, 4096, 512, 64):
    while True:
        try:
            filler.append(bytearray(size))
        except MemoryError:
            break

# Each flush() rewrites the directory block, and appends rewrite the FAT, so
# the same blocks are written again while their page is in the scratch page.
for _ in range(8):
    for i in range(4):
        files[i].write(chunks[i])
        files[i].flush()
for f in files:
    f.close()

filler = None
gc.collect()
ok = True
for i in range(4):
    with open(names[i], "rb") as f:
        if f.read() != chunks[i] * 8:
            ok = False
            print("MISMATCH", names[i])
if ok:
    print("scratch_page_ok")
print("done")
"""


def check_scratch_page_run(circuitpython, board, erase_page_size):
    """Check a SCRATCH_PAGE_CODE run, on the console and in the flash image."""
    circuitpython.wait_until_done()
    output = circuitpython.serial.all_output
    debug_output = circuitpython.debug_serial.all_output
    assert "Unable to allocate ram cache" in debug_output, "scratch page path not taken"
    assert "Erase of" not in debug_output
    assert "flash write failed" not in debug_output
    assert "scratch_page_ok" in output

    for i, letter in enumerate("ABCD"):
        content = read_file_from_flash(
            circuitpython.flash_file, f"scratch{i}.txt", board, erase_page_size=erase_page_size
        )
        assert content == letter * 300 * 8, f"scratch{i}.txt mismatch"


@pytest.mark.parametrize("board", ["native_native_sim", "native_native_sim_asan"], indirect=True)
@pytest.mark.circuitpy_drive({"code.py": SCRATCH_PAGE_CODE})
@pytest.mark.flash_config(erase_block_size=16384)
def test_flash_scratch_page_rewrites(circuitpython, board):
    """Test rewriting blocks while writes are staged in the scratch page.

    The flash simulator rejects writes to flash that isn't erased, so a block
    programmed twice without an erase fails instead of silently corrupting.
    16KB erase blocks make the RAM cache too big to allocate from a full heap,
    and evenly divide the 0x1fc000 filesystem partition. (littlefs doesn't
    stage writes in the scratch page.)
    """
    check_scratch_page_run(circuitpython, board, erase_page_size=16384)


@pytest.mark.parametrize("board", ["native_native_sim", "native_native_sim_asan"], indirect=True)
@pytest.mark.circuitpy_drive({"code.py": SCRATCH_PAGE_CODE})
@pytest.mark.flash_config(erase_block_size=65536)
def test_flash_scratch_page_partial_last_page(circuitpython, board):
    """Test the scratch page when the partition doesn't end on a page boundary.

    The 0x1fc000 filesystem partition isn't a multiple of 64KB, so its last
    page is partial and can't be erased. The scratch page must be the last
    whole page instead.
    """
    check_scratch_page_run(circuitpython, board, erase_page_size=65536)
